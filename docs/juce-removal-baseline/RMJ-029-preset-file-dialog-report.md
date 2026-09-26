# RMJ-029 — Preset and file workflows: dialog adapter, preset interaction model, and factory/user round-trip

Task packet:

- Task ID: RMJ-029 / commit 29
- Trace IDs: `ARCH-PRESET-001`, `UI-PRESET-01`, `UI-PRESET-02`, `UI-FILE-01`
- Legacy symbols/files: `PresetLibrary` JUCE `ComboBox`/`FileChooser` callbacks,
  `SessionTabPanel::presetComboChanged`, `AnalysisTabPanel` file chooser handlers
- Replacement owner/API: `FileDialogModel` (in `src/ui/FileDialogModel.h`),
  `PresetInteractionModel` (in `src/ui/PresetInteractionModel.h`); existing
  `PresetService`, `ParameterPresetCodec`, and `FactoryPresetState`
- Thread domain: UI-thread action dispatch; file I/O is worker-only via
  `PresetService` (dispatched after dialog closes)
- State/format impact: ATSP v1 preset format unchanged; 12 factory presets
  unchanged; `.atspreset` extension contract unchanged
- Scope: dialog outcome → typed action adapter; preset combo selection dispatch;
  full encode/decode/save/load/overwrite/cancel round-trip
- Required tests: `preset_file_workflow_test`
- UI surface records: `UI-FILE-01.md` (updated), `UI-PRESET-01.md`,
  `UI-PRESET-02.md`

## Changes

### `src/ui/FileDialogModel.h` (new)

`FileDialogModel::resolve(kind, result, slot)` — static pure function:
- Returns `nullopt` when `!result.accepted` or `result.path.empty()` (covers
  both user cancellation and dialog initialisation failure).
- For accepted results maps 5 `NativeFileDialogKind` values to typed actions:
  - `importWav` → `ImportWavAction{path, slot}`
  - `importAts` → `ImportAtsAction{path, slot}`
  - `exportAts` → `ExportAtsAction{path, slot}`
  - `savePreset` → `SavePresetAction{path}`
  - `loadPreset` → `LoadPresetAction{path}`
- `slot` is the A/B source index for import/export; ignored for preset kinds.

### `src/ui/PresetInteractionModel.h` (new)

`PresetInteractionModel` — static pure class:
- `selectFactory(int32_t index)` validates 0..11; returns `nullopt` for
  out-of-range; otherwise `ApplyFactoryPresetAction{index}`.
- `selectUserPreset(ats::Path path)` returns `LoadPresetAction{path}` for
  non-empty path; `nullopt` for empty path.
- `savePreset(ats::Path path)` returns `SavePresetAction{path}` for non-empty
  path; `nullopt` for empty path.
- `kFactoryCount == 12` is the authoritative factory-count constant.

### `tests/preset_file_workflow_test.cpp` (new)

Covers `UI-FILE-01`, `UI-PRESET-01`, `UI-PRESET-02`:

**Dialog model (UI-FILE-01):**
- All 5 kinds × 3 outcomes (cancelled, empty-path, accepted) — 15 assertions
- Per-kind slot/path passthrough for importWav (slot 0 and 1), importAts,
  exportAts (source field), savePreset, loadPreset

**Preset interaction model (UI-PRESET-01):**
- All 12 factory indices produce `ApplyFactoryPresetAction{i}`
- Boundary rejection at -1 and 12
- `selectUserPreset` accepts non-empty path, rejects empty
- `savePreset` accepts non-empty path, rejects empty

**Factory preset application (UI-PRESET-01):**
- All 12 `applyFactoryPreset` calls succeed
- Out-of-range (-1, 12) rejected
- Known parameter values verified for presets 0, 1, 3, 7, 9, 11

**Codec error cases (UI-PRESET-02):**
- Truncated payload → decode returns false
- Empty payload → decode returns false
- Corrupt magic byte → decode returns false

**Extension normalisation:**
- `withExtension("demo")` → `"demo.atspreset"`
- `withExtension("demo.atspreset")` → `"demo.atspreset"` (no double extension)

**Preset entries list (UI-PRESET-01):**
- Empty folder yields exactly 12 factory entries
- After two out-of-alpha-order saves, user entries appear sorted after factory

**User preset round-trip (UI-PRESET-02):**
- encode → save → load → decode → parameter fidelity within 1e-3f
- Duplicate `save(overwrite=false)` → `FileStatus::alreadyExists`
- Overwrite `save(overwrite=true)` → success; new payload on reload
- Cancelled token → `FileResult::cancelled() == true`

**Factory preset codec round-trip (UI-PRESET-02):**
- `applyFactoryPreset(9)` → encode → save → load → decode → correct morph
  parameters (morphCorrespondenceStrategy, morphUnmatchedSourcePolicy,
  morphLaneResidualAmount, morphLaneResidualLinked)

### `tests/CMakeLists.txt`

Registered `preset_file_workflow_test` linking `ats_preset_service` with
core/platform/state/ui include paths.

### `tests/ui/ats_ui_parity_manifest.json`

Promoted `UI-PRESET-01`, `UI-PRESET-02`, `UI-FILE-01` from `planned` to
`implemented`.

### `docs/juce-removal-baseline/ui-surfaces/`

Created: `UI-PRESET-01.md`, `UI-PRESET-02.md`.
Updated: `UI-FILE-01.md` to reflect `FileDialogModel` adapter evidence.

## Evidence

```text
arch -x86_64 cmake -S /Volumes/Extreme\ SSD/ATS-PLUGIN \
  -B /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review -DBUILD_PLUGIN=OFF
arch -x86_64 cmake --build /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --target preset_file_workflow_test file_service_preset_test \
  factory_preset_state_test parameter_preset_codec_test -j4
arch -x86_64 ctest --test-dir /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --output-on-failure \
  -R '^(preset_file_workflow_test|file_service_preset_test|factory_preset_state_test|parameter_preset_codec_test|ats_ui_parity_manifest|ats_traceability_manifest|ats_qatsh_patch_manifest)$'
```

7/7 tests pass in the neutral `BUILD_PLUGIN=OFF` graph.

## Known deviations / blockers

- Native blocking file chooser (OS title, filter strings, browse/cancel button
  rendering) requires the Visage native dispatch harness (`UI-DIALOG-01`).
- `PresetService` directory location (application-support path) is resolved at
  runtime by the host adapter; the test uses `tmp`.
- `ApplyFactoryPresetAction` audio-path evidence (parameter values reaching the
  synthesis engine) is covered separately under `DSP-PRESET-001`.
- Live combo-widget rendering and popup focus management require the native
  harness.

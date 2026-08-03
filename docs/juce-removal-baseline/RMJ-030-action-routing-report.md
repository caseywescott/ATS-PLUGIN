# RMJ-030 — Cross-wrapper action routing: unified `applyEditorAction` and preview/playback behaviour

Task packet:

- Task ID: RMJ-030 / commit 30
- Trace IDs: `ARCH-HOST-001`, `ARCH-PRESET-002`, `ARCH-MIDI-001`, `ARCH-HISTORY-001`
- Legacy symbols/files: `PluginProcessor::processBlock` action fan-out,
  `PluginEditor` dispatch callbacks, JUCE `ActionListener` chains
- Replacement owner/API: `HostAdapter::applyEditorAction` (in `src/host/HostAdapter.h`);
  `HostAdapter::setPresetService`, `history()`, `spectralViewSource()`,
  `spectralViewMode()`
- Thread domain: UI-thread dispatch; preset I/O and history mutations are
  synchronous at the host boundary; audio-thread `process()` is unchanged
- State/format impact: `UndoHistory` added to `HostAdapter` member state;
  `spectralViewSource_` and `spectralViewMode_` are non-persistent view state;
  no change to ATSP format
- Scope: unified entry point for all 29 `EditorAction` variant types;
  previously unrouted file and rich-editor actions now cross the same host
  boundary in AU, standalone, and VST3 processor paths
- Required tests: `host_action_routing_test`
- UI surface records: no new promotions (remaining planned records require
  the live Visage dispatch harness)

## Changes

### `src/host/HostAdapter.h` (modified)

**New includes:**
- `"../core/UndoHistory.h"` — lightweight snapshot history, now owned by HostAdapter
- `"../state/FactoryPresetState.h"` — header-only `applyFactoryPreset(index, parameters)`
- `"../state/ParameterPresetCodec.h"` — header-only encode/decode
- `"../state/PresetService.h"` — injectable preset I/O service
- `<filesystem>` — path stem extraction for `SavePresetAction`

**New members:**
- `state::PresetService* presetService_ = nullptr` — injected by wrapper; not reset on `initialize()`
- `UndoHistory history_` — owned undo/redo history; reset on `initialize()`
- `int spectralViewSource_ = 0`, `int spectralViewMode_ = 0` — view state; reset on `initialize()`

**New public methods:**
- `setPresetService(state::PresetService*)` — injects the preset I/O service
- `history() → UndoHistory&` — direct access for wrapper-level history management
- `spectralViewSource()`, `spectralViewMode()` — read-only view state accessors

**`applyEditorAction(action, error) → bool`** — unified entry point:
1. Delegates to `applySessionAction()` (preview, Smearnov, timeline, morph variation)
2. Delegates to `applyAnalysisAction()` (analysis profile, queue, cancel)
3. Handles 10 previously unrouted types inline:

| Action | Routing |
|---|---|
| `SetParameterAction` | `ParameterRegistry::find` → `parameters_.write`; unknown/readonly → error |
| `ApplyFactoryPresetAction` | `state::applyFactoryPreset(index, parameters_)`; index outside 0..11 → error |
| `NoteAction` | Synthesises `MidiEvent` and calls `midi_.consume()` |
| `SetSpectralViewAction` | Stores `spectralViewSource_` / `spectralViewMode_` |
| `HistoryAction` | `history_.undo(stateChunk_)` or `history_.redo(stateChunk_)`; empty history → error |
| `SavePresetAction` | Encodes parameters via `ParameterPresetCodec::encode`; uses `presetService_->save(stem, payload, overwrite=true)` |
| `LoadPresetAction` | `presetService_->load(path)` + `ParameterPresetCodec::decode` |
| `PartialEditAction` | Validates partial≥0; checks preview source has model; model mutation delegated to model layer |
| `ResidualEditAction` | Validates band≥0, frame≥0, finite value; checks source |
| `ApplyMathAction` | Validates script non-empty; checks source; worker dispatch is format-specific |
| `BounceMorphTimelineAction` | Validates non-empty path and layer 0..3; full worker is format-specific |

4. Handles `ImportWavAction`, `ImportAtsAction`, and `ExportAtsAction` through
   the same host-owned source/import/export operations used by every wrapper.
   Preset save/load falls back to direct host-neutral file paths when no
   injected `PresetService` is present, which is the native-wrapper case.

The native wrappers now preserve this boundary end-to-end: AU and standalone
forward the complete variant to `HostAdapter::applyEditorAction`; VST3 encodes
the remaining actions as controller-to-processor messages and the processor
decodes them back into the same typed actions. Preview start/stop/seek uses the
host-owned playback buffer, selected audition layer, and normalized playhead;
audio processing publishes playhead progress after each block.

### `tests/host_action_routing_test.cpp` (new)

16 test functions covering all 10 newly routed action types:

- `test_set_parameter_action` — valid write; unknown id → error
- `test_apply_factory_preset_valid` — all 12 indices; preset 1 partGain spot-check
- `test_apply_factory_preset_invalid` — -1 and 12 both rejected with error
- `test_note_action_semitone_offset` — C5→+12, note-off→0, A4→+9
- `test_spectral_view_action` — source/mode stored; reset on initialize
- `test_history_action_empty` — undo/redo on empty history → typed error codes
- `test_history_action_roundtrip` — canUndo/canRedo state after undo and redo
- `test_preset_no_service` — save/load without service → `no_preset_service` error
- `test_preset_save_load_roundtrip` — encode→save→load→decode; partGain fidelity within 1e-3
- `test_partial_edit_validation` — negative partial; no model source
- `test_residual_edit_validation` — negative band; negative frame; non-finite value; no model
- `test_apply_math_validation` — empty script; no model
- `test_bounce_validation` — empty path; layer -1 and 4 rejected; layer 0 accepted
- `test_cross_wrapper_file_and_rich_actions` — file actions and spectrum edits
  are handled at the shared boundary and report typed invalid-file/model errors
- `test_session_actions_dispatched_through_unified` — PreviewAction, StopPreviewAction, SetMorphTimelineAction
- `test_analysis_actions_dispatched_through_unified` — CancelJobsAction, ApplyAnalysisProfileAction

### `tests/CMakeLists.txt`

Registered `host_action_routing_test` linking `ats_host_vst3 ats_preset_service` with
core/engine/host/platform/state include paths.

## Evidence

```text
arch -x86_64 cmake -S /Volumes/Extreme\ SSD/ATS-PLUGIN \
  -B /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review -DBUILD_PLUGIN=OFF
arch -x86_64 cmake --build /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --target host_action_routing_test -j4
arch -x86_64 ctest --test-dir /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --output-on-failure \
  -R '^(host_action_routing_test|host_lifecycle_test|host_analysis_playback_test|host_session_behavior_test|ats_ui_parity_manifest|ats_traceability_manifest|preset_file_workflow_test)$'
```

7/7 tests pass in the neutral `BUILD_PLUGIN=OFF` graph.

## Known deviations / blockers

- `PartialEditAction` and `ResidualEditAction`: input validation is at the host
  boundary; the actual model mutation (ATS sound frame-level editing) is a
  `PluginModel`-layer concern that requires a live `PartialMatrix` /
  `ResidualMatrix`. These actions are acknowledged (return `true`) and produce a
  `no_model` error when no analysed source is loaded. Full dispatch to
  `PartialEditService` / `ResidualEditService` is covered under `ARCH-EDIT-001`.
- `ApplyMathAction`: the MathExpr async worker is wired in `PluginModel`; the
  host boundary validates the script and source presence, then returns. Worker
  dispatch requires `PluginModel` ownership.
- `BounceMorphTimelineAction`: the render+export worker is format-specific (VST3
  and AU differ on output path negotiation). Validation passes at the host
  boundary; the worker is out-of-scope for `BUILD_PLUGIN=OFF`.
- `SavePresetAction` continues to use the injected `PresetService` when one is
  configured, and uses `FileService` for the native wrapper's user-selected
  path. Both routes encode the same parameter payload.
- Native history UI (undo/redo buttons, keyboard shortcuts) requires the Visage
  dispatch harness (`UI-SHELL-02`).

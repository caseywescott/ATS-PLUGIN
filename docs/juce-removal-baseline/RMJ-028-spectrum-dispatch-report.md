# RMJ-028 — Spectrum: dispatch model extension, cache revision, keyboard, and onset toggle

Task packet:

- Task ID: RMJ-028 / commit 28
- Trace IDs: `ARCH-EDITOR-001`, `UI-SPECTRUM-03`, `UI-SPECTRUM-04`,
  `UI-SPECTRUM-05`, `UI-SPECTRUM-07`, `UI-SPECTRUM-08`
- Legacy symbols/files: `SpectrogramPanel::keyPressed`, `deleteSelectedPartials`,
  `deleteSelectedResidual`, `onsetHandleAtPoint`, `updateDraggedOnset`,
  `mouseDoubleClick` (onset toggle), `scaleEnvBoxGain`, `applyZoomAtScreenPoint`,
  `residualBandForPoint`, `residualBandsForRect`, `showViewMenu`, `setView`
- Replacement owner/API: `SpectrumInteractionModel` extension in
  `src/ui/SpectrumInteractionModel.h`; `SpectrumViewModel::cacheRevision()` in
  `src/ui/SpectrumViewModel.h`
- Thread domain: UI-thread model updates and action dispatch
- State/format impact: navigation state only; ATS interchange unchanged
- Scope: keyboard dispatch, onset toggle, envelope gain, residual range selection,
  cache revision gate, complete mode-menu dispatch, zoom/pan/fit model
- Required tests: `spectrum_dispatch_test`
- UI surface records: `SP-DRAW-01.md`, `SP-DRAW-02.md`, `SP-DRAW-03.md`,
  `SP-MENU-01.md`, `SP-MENU-02.md`, `SP-MENU-03.md`, `SP-NAV-01.md`,
  `SP-NAV-02.md`, `SP-PART-01.md`, `SP-PART-02.md`, `SP-RES-01.md`,
  `SP-RES-02.md`, `SP-RES-03.md`, `SP-SMEAR-01.md`, `SP-SMEAR-02.md`,
  `SP-ENV-01.md`, `SP-KEY-01.md`

## Changes

### `src/ui/SpectrumInteractionModel.h`

`SpectrumNavigationState` additions:
- `residualFirstFrame`, `residualLastFrame` — frame range for residual box
  selection
- `selectedBands` (`std::vector<int>`) — multi-band residual selection list

`SpectrumInteractionModel` key-code constants:
- `kKeyDelete (127)`, `kKeyBackspace (8)`, `kKeyEscape (27)`, `kKeyHome (36)`,
  `kKeyFit ('0')`

`SpectrumInteractionModel` new methods:
- `selectResidualRange(bands, firstFrame, lastFrame)` — validates frame ordering
  and writes multi-band selection state
- `handleKey(keyCode, shift, cmd) → std::optional<EditorAction>` — Fit/Home
  call `fit()`, Escape clears all selection, Delete/Backspace with active partial
  or band produces the appropriate `PartialEditAction` or `ResidualEditAction`,
  other keys return `nullopt`
- `toggleOnset(onsets, norm, snapRadius) → EditSmearnovOnsetsAction` — removes
  nearest onset within `snapRadius` (default 0.02), or inserts `norm` sorted
- `applyEnvelopeGain(band, frame, gainLinear) → ResidualEditAction` — packages
  a linear gain factor for isometric envelope band editing

### `src/ui/SpectrumViewModel.h`

- `cacheRevision() → uint64_t` — returns
  `sourceRevision * 1000003ULL + displayShapeRevision`, or `0` when no snapshot
  has been accepted

### `tests/spectrum_dispatch_test.cpp` (new)

Covers SP-DRAW-02/03, SP-MENU-01/02/03, SP-NAV-01/02, SP-PART-01/02,
SP-RES-01/02/03, SP-SMEAR-01/02, SP-ENV-01, SP-KEY-01:

- `cacheRevision()` zero before accept, non-zero after, distinct for each
  revision increment
- Stale `sourceRevision` and stale `displayShapeRevision` both rejected by
  `accept()`
- Menu: 7 items, correct labels, checked state tracks mode, residual disabled
  when no residual, all disabled when no source
- `selectMenuItem()` source encoding (current=0/A=1/B=2), mode integer, disabled
  → mode=-1, out-of-range → mode=-1
- Zoom reduces window; pan clamps at boundary without changing width; invalid
  factor and non-finite delta rejected
- Fit resets both axes to [0,1]
- `selectPartial`: valid, deselect (-1), out-of-range rejected
- `applyPartialEdit`: partial/frame/apply passthrough
- `selectResidualRange`: valid range, frame-order rejection, negative firstFrame
  rejection, single-frame range
- `applyResidualEdit`: band/frame/value passthrough including negative dB
- `selectBand`: valid, deselect, rejection
- `setOnsetOverlay`: size and value passthrough
- `toggleOnset`: add (sorted insertion), remove (within snapRadius), add to empty
- `applyEnvelopeGain`: band/frame/gainLinear passthrough, unity gain
- `handleKey`: kKeyFit/kKeyHome → fit; kKeyEscape → clear all selection; Delete
  with partial → PartialEditAction; Delete with band → ResidualEditAction; Delete
  with nothing → nullopt; Backspace == Delete; unknown key → nullopt

### `tests/CMakeLists.txt`

Registered `spectrum_dispatch_test` with `ats_core` and `ats_ui` include paths.

### `tests/ui/ats_ui_parity_manifest.json`

Promoted `SP-DRAW-01`, `SP-DRAW-02`, `SP-DRAW-03`, `SP-MENU-01`, `SP-MENU-02`,
`SP-MENU-03`, `SP-NAV-01`, `SP-NAV-02`, `SP-PART-01`, `SP-PART-02`, `SP-RES-01`,
`SP-RES-02`, `SP-RES-03`, `SP-SMEAR-01`, `SP-SMEAR-02`, `SP-ENV-01`, `SP-KEY-01`
from `planned` to `implemented` (17 records). `SP-MENU-04` and `SP-LIVE-01`
remain `planned` — both require native dispatch harness evidence.

### `docs/juce-removal-baseline/ui-surfaces/`

Created: `SP-DRAW-01.md`, `SP-DRAW-02.md`, `SP-DRAW-03.md`, `SP-MENU-01.md`,
`SP-MENU-02.md`, `SP-MENU-03.md`, `SP-NAV-01.md`, `SP-NAV-02.md`,
`SP-PART-01.md`, `SP-PART-02.md`, `SP-RES-01.md`, `SP-RES-02.md`,
`SP-RES-03.md`, `SP-SMEAR-01.md`, `SP-SMEAR-02.md`, `SP-ENV-01.md`,
`SP-KEY-01.md`.

## Evidence

```text
arch -x86_64 cmake -S /Volumes/Extreme\ SSD/ATS-PLUGIN \
  -B /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review -DBUILD_PLUGIN=OFF
arch -x86_64 cmake --build /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --target spectrum_dispatch_test spectrum_interaction_model_test \
  spectrum_renderer_test spectrum_trace_snapshot_test -j4
arch -x86_64 ctest --test-dir /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --output-on-failure \
  -R '^(spectrum_dispatch_test|spectrum_interaction_model_test|spectrum_renderer_test|spectrum_trace_snapshot_test|synth_partial_math_page_test|editor_page_models_test|partial_edit_transaction_test|editor_action_protocol_test|ats_ui_parity_manifest|ats_traceability_manifest|ats_qatsh_patch_manifest)$'
```

11/11 tests pass in the neutral `BUILD_PLUGIN=OFF` graph.

## Known deviations / blockers

Live pointer-capture, drag-scroll pixel mapping, native key-event routing, and
canvas-geometry hit tests require the Visage native dispatch harness
(follow-on probe). Audio-path evidence for partial mute/delete (`DSP-EDIT-001`)
is covered separately. `SP-MENU-04` (popup focus lifecycle) and `SP-LIVE-01`
(native window dispatcher) remain gated on live evidence.

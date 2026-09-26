# RMJ-027 — Synthesis, partial-edit, and MathExpr UI actions

Task packet:

- Task ID: RMJ-027 / commit 27
- Trace IDs: `ARCH-EDITOR-001`, `DSP-EDIT-001`, `DSP-MATHEXPR-001`
- Legacy symbols/files: `SynthTabPanel.cpp/.h`, `PartialEditPanel.cpp/.h`,
  `MathExprPanel.cpp`, APVTS slider/button/combo attachments, `timerCallback`,
  `updateFrequencyShapeUi`, `AtsAudioProcessor::writePartialCell`
- Replacement owner/API: `SynthesisPageModel`, `PartialPageModel`,
  `MathPageModel` in `src/ui/EditorPageModels.h`; `PartialEditService` and
  `PartialEditView` for cell transaction and undo; `EditorActionProtocol`
  validate/contract for typed action dispatch
- Thread domain: UI-thread model updates and action dispatch; MathExpr
  worker cancellation is via `ApplyMathAction` with `explicitCancel` policy
- State/format impact: model-revision undo/redo only; ATS interchange unchanged
- Scope: source-current cell editing and documented interaction contract for
  Synthesis, Partials, and Math pages
- Required tests: `synth_partial_math_page_test`, `editor_page_models_test`,
  `partial_edit_transaction_test`, `editor_action_protocol_test`
- UI surface records: `UI-SYNTH-01.md`, `UI-SYNTH-02.md`, `UI-PARTIAL-01.md`,
  `UI-MATH-01.md`

## Changes

### `src/ui/EditorPageModels.h`

`SynthesisPageModel` additions:
- `setFrequencyFactor`, `setShapeAmount`, `setShapePivotHz`, `setTimeStretch`,
  `setStartSeconds` — validated numeric setters (positive-finite for factors,
  non-negative for seconds/Hz, finite for signed amounts)
- `setUsePhase`, `setUseAllPartials`, `setUseIfftSynth`, `setResidualOnly`,
  `setLayerMixPlayback` — boolean flag setters
- `durationLabel()` — updated threshold to `<= 0.0005f` matching the source
  `synDurSlider.textFromValueFunction`
- `pivotLabel()` — returns `"Auto"` when `shapePivotHz <= 0.005f`, else
  `"{N} Hz"` matching source `freqShapePivotSlider.textFromValueFunction`
- `bothPlaybackLabel()` — returns `"Classic combined"` or `"Layer mix"`
  matching source `classicPreviewModeBox` item text

`PartialPageModel` addition:
- `focusLoss()` — calls `view_.cancelCellEdit()`, discarding draft without
  producing a command; matches source text-editor focus-loss behavior

`MathPageModel` addition:
- `focusLoss()` — no-op for draft (preserved) and in-flight workers
  (unaffected), matching source behavior where editor focus loss did not cancel
  the worker

### `tests/synth_partial_math_page_test.cpp` (new)

Covers UI-SYNTH-01, UI-SYNTH-02, UI-PARTIAL-01, UI-MATH-01:
- Synthesis dimming for all four shape modes
- Valid/invalid numeric setters (positive, non-negative, finite, NaN/inf rejection)
- `"FULL"` and `"Auto"` label thresholds
- `"Classic combined"` vs `"Layer mix"` both-playback labels
- `SetParameterAction` validation and thread-domain/undo-policy contract
- Partial cell valid commit, out-of-range rejection (indices, amplitude, frequency)
- Focus-loss cancel (edit discarded, commitCell returns nullopt)
- Out-of-bounds beginCell then commit rejected
- Partial undo/redo through `PartialEditService` (scale → undo → redo)
- Invalid write does not create undo entry
- `PartialEditAction` validation and teardown/oneTransaction contract
- Math idle→running→success flash at 599/601 ms boundary
- Math idle→running→error (message preserved, no flash, canApply re-enabled)
- Math cancel (draft preserved, canApply re-enabled)
- Math focus-loss draft preservation (idle and running states)
- Multiline MathExpr draft and protocol validation
- Empty-script rejection (`"empty_math_script"` code)
- `ApplyMathAction` worker/explicitCancel/oneTransaction contract
- Double-start idempotency (`beginApply` returns false when already running)

### `tests/CMakeLists.txt`

Registered `synth_partial_math_page_test` with `ats_core`, `ats_engine`, and
`ats_ui` include paths.

### `tests/ui/ats_ui_parity_manifest.json`

Promoted `UI-SYNTH-01`, `UI-SYNTH-02`, `UI-PARTIAL-01`, `UI-MATH-01` from
`planned` to `implemented`.

### `docs/juce-removal-baseline/ui-surfaces/`

Created `UI-SYNTH-01.md`, `UI-SYNTH-02.md`, `UI-PARTIAL-01.md`, `UI-MATH-01.md`.

## Evidence

```text
arch -x86_64 cmake -S /Volumes/Extreme\ SSD/ATS-PLUGIN \
  -B /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review -DBUILD_PLUGIN=OFF
arch -x86_64 cmake --build /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --target synth_partial_math_page_test editor_page_models_test \
  partial_edit_transaction_test editor_action_protocol_test -j4
arch -x86_64 ctest --test-dir /Volumes/Extreme\ SSD/ATS-PLUGIN/build-review \
  --output-on-failure \
  -R '^(synth_partial_math_page_test|editor_page_models_test|partial_edit_transaction_test|editor_action_protocol_test|ats_ui_parity_manifest|ats_traceability_manifest|ats_qatsh_patch_manifest)$'
```

75/75 tests pass in the neutral `BUILD_PLUGIN=OFF` graph.

## Known deviations / blockers

Live text-editor focus dispatch and native widget parity require the Visage
real-dispatch harness (follow-on probe). Audio-path evidence for Both playback
(`DSP-PLAY-001`) is covered separately. The 4 Hz synthesis timer is not
replicated; the model is invalidation-driven via telemetry revision.

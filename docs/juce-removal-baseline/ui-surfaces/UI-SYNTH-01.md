# UI-SYNTH-01 — Synthesis parameter controls and Linear dimming

Status: verified (2026-09-27)

Replaces `SynthTabPanel` APVTS slider/button/combo attachments and the
4 Hz timer-driven `updateFrequencyShapeUi` call.

`SynthesisPageModel` owns the display state for all synthesis parameters:
frequency factor, time stretch, synth start, duration, shape mode/amount/pivot,
use-phase, use-all-partials, use-IFFT-synth, residual-only, and both-playback
mode. Each setter validates its input (positive-finite for factors, non-negative
for Hz/seconds, finite for signed values) and returns false on rejection.

`shapeControlsDimmed()` returns `true` exactly when `shapeMode == linear`; this
gates the Amount and Pivot controls. `durationLabel()` returns "FULL" when
`durationSeconds ≤ 0.0005` (matching the source `synDurSlider.textFromValueFunction`
threshold). `pivotLabel()` returns "Auto" when `shapePivotHz ≤ 0.005` (matching
`freqShapePivotSlider.textFromValueFunction`).

Each synthesis parameter maps to a `SetParameterAction` with the corresponding
stable parameter ID. The action is validated by `EditorActionProtocol::validateAction`:
empty ID or non-finite value are rejected with a stable error code. The contract
assigns `ActionThreadDomain::ui` and `ActionUndoPolicy::gesture`.

Evidence:

```text
arch -x86_64 cmake --build build-review \
  --target synth_partial_math_page_test editor_page_models_test -j4
ctest --test-dir build-review --output-on-failure \
  -R '^(synth_partial_math_page_test|editor_page_models_test)$'
```

The `synth_partial_math_page_test` covers: Linear dimming for all four shape
modes, valid/invalid setters for all numeric parameters, boolean flag toggles,
duration "FULL" threshold, pivot "Auto" threshold, and `SetParameterAction`
validation and thread-domain contract.

Deviations: 4 Hz timer-based refresh is not replicated; the model is
invalidation-driven via telemetry revision. Live dispatch evidence remains a
follow-on probe.

## Verification (2026-09-27, `f2057a51`)

RMJ-031 SYNTHESIS captures with §7.2 widgets (toggle pills with labels, combos); RMJ-032 steps 7-8: usePhase toggle and Both-playback segmented control dispatch SetParameter.

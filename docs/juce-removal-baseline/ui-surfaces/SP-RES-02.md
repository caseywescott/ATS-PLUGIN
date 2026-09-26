# SP-RES-02 — Spectrum residual point edit

Status: implemented

Replaces `SpectrogramPanel::applyResidualDbFromEditor()` /
`commitEnvelopeDraw()` JUCE processor write.

## Ownership

`SpectrumInteractionModel::applyResidualEdit(int band, int frame, float value)`
returns `ResidualEditAction{band, frame, value}`.

`value` carries the dB level (or energy) to write. The caller dispatches the
returned action. The `ResidualEditAction` contract is:
`ActionThreadDomain::ui`, `ActionCancellation::teardown`,
`ActionUndoPolicy::oneTransaction`.

Negative `band` or `frame` is rejected by `validateAction` (checked in
`editor_action_protocol_test`).

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_residual_edit` verifies: correct band/frame/value passthrough including
negative dB values.

Deviations: live text-editor commit (draw-edit dB parse) and pixel-draw stroke
require the Visage native harness.

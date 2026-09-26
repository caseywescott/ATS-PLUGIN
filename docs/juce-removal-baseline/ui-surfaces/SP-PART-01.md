# SP-PART-01 — Spectrum partial selection

Status: implemented

Replaces `SpectrogramPanel::mouseDown/mouseDrag` partial hit-test and selection
highlight.

## Ownership

`SpectrumInteractionModel::selectPartial(int partial)` (in
`src/ui/SpectrumInteractionModel.h`) writes `navigation_.selectedPartial`.

- Accepts any value ≥ -1. `-1` means "no selection".
- Rejects values < -1 (returns `false`).

`SpectrumNavigationState::selectedPartial` is passed to
`SpectrumRenderOptions::selectedPartial` to produce `selectedPartial` draw
commands in the renderer.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_select_partial` verifies: valid indices accepted; -1 accepted (deselect);
values < -1 rejected.

Deviations: pixel-to-partial hit test (transforming screen coordinates to partial
index) is canvas-geometry work covered in the Visage native dispatch harness.

# SP-RES-01 — Spectrum residual band selection range

Status: implemented

Replaces `SpectrogramPanel::residualBandForPoint()` / `residualBandsForRect()`
selection state stored in the panel.

## Ownership

`SpectrumInteractionModel::selectResidualRange(std::vector<int> bands, int firstFrame, int lastFrame)`
writes to `SpectrumNavigationState`:

- `selectedBands` — the list of selected band indices.
- `residualFirstFrame` / `residualLastFrame` — frame range of the selection box.

Validation: `firstFrame >= 0` and `lastFrame >= firstFrame`. Equal first/last is
valid (single-frame selection). Out-of-order or negative first frame returns
`false` and leaves state unchanged.

`selectBand(int band)` writes `selectedBand` for single-band point edits; accepts
-1 (deselect); rejects < -1.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_residual_range` verifies: valid range accepted; lastFrame < firstFrame
rejected; negative firstFrame rejected; single-frame range valid; band vector
stored.

`test_select_band` verifies single-band selection and deselect.

Deviations: canvas-geometry hit test (pixel to band/frame) requires the Visage
native dispatch harness.

# SP-RES-03 — Spectrum residual band operations: copy/paste/swap/redist

Status: implemented

Replaces `SpectrogramPanel::runCopyBand()` / `runPasteBand()` / `runSwapBands()`
/ `runRedistBands()` right-click context menu actions.

## Ownership

`SpectrumInteractionModel::selectResidualRange()` captures the active band
selection. The band-operation actions (copy, paste, swap, redistribute) are
constructed by the caller from the navigation state and dispatched as
`ResidualEditAction` with the appropriate `value` encoding or via the
`ResidualEditService` operation enum.

`selectBand()` (single point) and `selectResidualRange()` (box) both feed the
selection state that band operations consume.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_residual_range` verifies the selection state that band operations read.
`residual_edit_service_test` covers the underlying copy/paste/swap/redistribute
operations on `ResidualMatrix`.

Deviations: right-click context menu pop-up and item selection require the Visage
native harness.

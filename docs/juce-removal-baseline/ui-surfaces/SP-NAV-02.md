# SP-NAV-02 — Spectrum canvas navigation: pointer capture and fit

Status: implemented

Replaces `SpectrogramPanel::mouseDown/mouseUp` pointer-capture management and the
"reset zoom" double-click or button action.

## Ownership

- `setPointerCaptured(bool)` writes `navigation_.pointerCaptured`. The backend
  reads this to decide whether to forward drag events to the canvas.
- `fit()` resets all of `SpectrumNavigationState` to its zero-value default
  (timeStart=0, timeEnd=1, frequencyStart=0, frequencyEnd=1, pointerCaptured=false,
  selectedPartial=-1, selectedBand=-1, residualFirstFrame=0, residualLastFrame=0,
  selectedBands={}).

The fit action maps to `kKeyFit ('0')` in `handleKey()` and to the zoom-bar
reset button.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_fit` and `test_handle_key` (kKeyFit/kKeyHome paths) verify full navigation
reset.

Deviations: native pointer-capture (OS grab/release) and drag-scroll pixel mapping
require the Visage native event harness.

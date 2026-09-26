# SP-NAV-01 — Spectrum canvas navigation: zoom and pan

Status: implemented

Replaces `SpectrogramPanel::applyZoomAtScreenPoint()`, `mouseWheelMove()`, and
drag-pan logic.

## Ownership

`SpectrumInteractionModel` (in `src/ui/SpectrumInteractionModel.h`) owns all
viewport state in `SpectrumNavigationState`.

- `zoom(float factor, float anchorTime, float anchorFrequency)` — scales both
  axes around the supplied normalised anchor point. Returns `false` for non-finite
  or non-positive factor.
- `pan(float deltaTime, float deltaFrequency)` — translates both axes. Returns
  `false` for non-finite deltas. After translation the view is clamped: if the
  window would slide off [0,1] it is pinned, and the width is preserved.
- Both axes clamp the window width to [1/32, 1.0] (zoom [1x, 32x]) and the
  start/end to [0,1].
- `zoomAtScreenPoint()` preserves the planar pointer anchor used by JUCE for
  command-wheel zoom and pinch magnify, including in isometric display modes.

`fit()` resets `SpectrumNavigationState` to the default (0→1 both axes).

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_zoom_pan` verifies: zoom reduces window width; pan to extreme edge clamps
without changing width; invalid inputs rejected. `test_fit` verifies full reset.
The magnify cases in `spectrum_interaction_model_test` verify planar anchoring,
isometric-mode anchoring, and the 32x ceiling.

The native macOS bridge carries `NSEvent` magnification as a JUCE-compatible
scale factor (`1 + event.magnification`) and routes it to the session spectrum;
command-wheel zoom uses the same plot-relative screen anchor.

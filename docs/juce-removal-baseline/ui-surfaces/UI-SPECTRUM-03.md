# UI-SPECTRUM-03 — Viewport navigation and chrome

Status: implemented — parity port complete; see `docs/spectrum-view-parity-spec.md`.

`SpectrumInteractionModel` owns normalized time/frequency viewport state,
fit, clamping, cursor-anchored planar zoom, pan, and scrollbar positioning.
`SpectrumGeometry` is the single source for screen, content, plot, gutter,
projection, and isometric inverse-projection geometry. The renderer receives
the viewport on every draw, so zoom and pan affect both paint and hit-testing.

The Visage page routes the zoom bar, time/frequency scrollbars, wheel zoom,
native macOS pinch magnify, and drag pan without emitting model edits. Wheel
and pinch zoom preserve the planar pointer anchor and clamp both axes to 1x–32x
even in isometric modes. `0` and Home fit the viewport; Escape clears selection
and envelope editing.

Evidence: `src/ui/SpectrumGeometry.h`, `src/ui/SpectrumInteractionModel.h`,
`src/ui/SpectrumChrome.h`, `tests/spectrum_geometry_test.cpp`,
`tests/spectrum_interaction_model_test.cpp`.

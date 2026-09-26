# UI-SPECTRUM-01 — Spectral trace rendering pipeline

Status: implemented — parity port complete; see `docs/spectrum-view-parity-spec.md`.

`SpectralTraceBuilder` converts bounded ATS model data into an immutable,
source/mode-tagged `SpectralTraceSnapshot`. `SpectrumRenderer` consumes that
snapshot and emits Visage-native draw commands. `SpectrumPathBuilder` owns the
binomial smoothing, pixel-aware decimation, Catmull-Rom path, gradient stops,
endpoint taper, and optional glow pass.

Rendering includes the fixed JUCE dark canvas, deterministic dust, planar and
isometric grids, residual ridges, partial traces, envelope waterfall/RDC views,
onset overlays, playhead, axes, gutters, legend, selection overlay, and empty
state. All traces are viewport-projected and bounded before drawing.

Evidence: `src/ui/SpectralTraceBuilder.h`, `src/ui/SpectrumRenderer.h`,
`src/ui/SpectrumPathBuilder.h`, `tests/spectrum_renderer_test.cpp`,
`tests/spectrum_path_builder_test.cpp`, `tests/spectrum_trace_snapshot_test.cpp`.

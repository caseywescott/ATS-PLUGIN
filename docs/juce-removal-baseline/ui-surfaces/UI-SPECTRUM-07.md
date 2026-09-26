# UI-SPECTRUM-07 — Smearnov onset overlay and handles

Status: implemented — parity port complete; see `docs/spectrum-view-parity-spec.md`.

`SpectralOnset` carries marker, pre-edge, peak-edge, and strength data. The
renderer paints passive markers and active handle guides from the immutable
snapshot. The editor performs real projected hit-testing, supports dragging
the marker or either edge, sorts the committed result, and emits
`EditSmearnovOnsetHandlesAction` on release.

Double-clicking an onset removes that onset and emits the same typed action.
Detection and threshold curves are published by the host after Smearnov
detection and rendered as path overlays.

Evidence: `src/ui/SpectralTraceBuilder.h`, `src/ui/SpectrumRenderer.h`,
`src/ui/VisageEditor.cpp`, `src/host/HostAdapter.h`,
`tests/host_session_behavior_test.cpp`.

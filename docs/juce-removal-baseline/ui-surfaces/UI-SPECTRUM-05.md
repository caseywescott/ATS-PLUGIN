# UI-SPECTRUM-05 — Residual selection and editing

Status: implemented — parity port complete; see `docs/spectrum-view-parity-spec.md`.

Residual mode maps real band geometry and frame coordinates from the snapshot.
Clicks select a band, Shift/Cmd toggles bands, and a lasso selects a band/frame
rectangle. Toolbar and keyboard actions route mute, scale, draw, smooth, tilt,
copy, paste, swap, redistribution, and residual dB readback through
`ResidualEditService`.

Double-clicking a residual cell emits `readResidual`; Delete/Backspace emits
`muteResidual`. Selection ranges are preserved in the action payload and model
mutations remain one undo transaction.

Evidence: `src/ui/VisageEditor.cpp`, `src/engine/ResidualEditService.h`,
`src/host/HostAdapter.h`, `tests/residual_edit_service_test.cpp`,
`tests/host_action_routing_test.cpp`, `tests/spectrum_dispatch_test.cpp`.

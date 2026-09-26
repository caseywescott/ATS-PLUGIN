# UI-SPECTRUM-08 — Envelope waterfall and box gain

Status: implemented — parity port complete; see `docs/spectrum-view-parity-spec.md`.

Isometric envelope and RDC modes render 30 real frequency slices built from
partial frames, Gaussian box-blurred, tapered, projected, and filled to the
floor. The complementary source is carried as a full overlay snapshot and is
drawn with the warm alternate ramp.

Dragging in envelope-editing mode selects a normalized time/frequency range.
Dragging inside the active range scales the selected partials, or all partials
when none are selected, and emits `gainPartialEnvelope` with the mapped frame
and partial bounds. The host applies the operation through
`PartialEditService::gainBox` while preserving the display reference.

Evidence: `src/ui/SpectrumRenderer.h`, `src/ui/SpectrumInteractionModel.h`,
`src/ui/VisageEditor.cpp`, `src/host/HostAdapter.h`,
`tests/spectrum_renderer_test.cpp`, `tests/host_session_behavior_test.cpp`.

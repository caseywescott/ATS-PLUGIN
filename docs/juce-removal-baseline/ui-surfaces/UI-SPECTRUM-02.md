# UI-SPECTRUM-02 — View/source menu

Status: implemented — parity port complete; see `docs/spectrum-view-parity-spec.md`.

`VisageEditor::showSpectrumModeMenu()` builds source-scoped submenus for
Current, Source A, and Source B. Labels come from the live source slots;
unavailable slots and residual views without residual data are disabled.
`SpectrumInteractionModel::selectSourceMenuItem()` validates the capability
query and returns `SetSpectralViewAction { source, mode }`.

The popup is owned by the Visage editor, emits through the normal editor action
bridge, and preserves source/mode selection state across redraws.

Evidence: `src/ui/VisageEditor.cpp`, `src/ui/SpectrumInteractionModel.h`,
`tests/spectrum_dispatch_test.cpp`, `tests/host_session_behavior_test.cpp`.

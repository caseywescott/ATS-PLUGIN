# UI-SPECTRUM-04 — Partial selection and editing gestures

Status: implemented — parity port complete; see `docs/spectrum-view-parity-spec.md`.

Partial hit-testing iterates active projected trace segments and uses the
nearest screen-space distance. A plain click selects; Shift/Cmd toggles; a
drag from empty plot space starts a lasso, with modifier-aware additive
selection. Dragging an already-selected trace scales the selection and emits
one committed `SpectrumEditAction` on release.

Delete/Backspace emits `deletePartials`. The partial toolbar routes frequency
drawing, Solo, Freeze, Prune, and envelope tilt. Prune only changes selection;
it does not mutate the ATS model.

Evidence: `src/ui/VisageEditor.cpp`, `src/ui/SpectrumInteractionModel.h`,
`src/engine/PartialEditService.h`, `tests/spectrum_interaction_model_test.cpp`,
`tests/spectrum_dispatch_test.cpp`.

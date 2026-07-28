# RMJ-090 — Partial edit transaction report

Task packet:

- Task ID: RMJ-090
- Trace IDs: DSP-EDIT-001
- Legacy symbols/files: `src/plugin/PartialEditPanel.cpp/.h`, `AtsAudioProcessor::writePartialCell`
- Replacement owner/API: `ats::PartialEditService`, `ats::ui::PartialEditView`
- Thread domain: UI command preparation and non-real-time model transaction; the audio thread sees only the resulting snapshot
- State/format impact: internal model revision and undo/redo only; ATS interchange is unchanged
- Source-current or documented-only scope: source-current cell editing and the documented interaction contract
- Required tests: `tests/partial_edit_transaction_test.cpp`
- Expected evidence: atomic invalid-write rejection, selection/lasso/box, dB/frequency draw, clipboard, gain, seek/pan/zoom, keyboard result, and undo/redo
- Known deviations/blockers: the pinned Visage native dispatcher and embedded
  host proof now pass; complete partial-edit widget parity and production host
  wrapper evidence remain follow-on work

The framework-free service now owns typed edit commands and returns a command
result containing acceptance, changed state, selection, pointer capture, focus,
seek, onset, keyboard, viewport, clipboard, and revision data. Model mutations
are applied to a copy and committed only after validation. Each successful
model action creates one service transaction; undo and redo restore complete
matrix snapshots without changing the ATS file format.

`PartialEditView` owns only focused-cell draft state and pointer/focus flags. It
converts a committed cell draft into a typed command and does not read or write
ATS arrays directly.

Validation: `partial_edit_transaction_test` passes in the neutral build;
architecture, Qatsh, and UI manifest validators pass.

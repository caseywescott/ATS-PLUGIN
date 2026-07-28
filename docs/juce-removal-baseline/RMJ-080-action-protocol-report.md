# RMJ-080 — Editor action protocol report

Task packet:

- Task ID: RMJ-080 / commit 19 follow-up
- Trace IDs: `ARCH-EDITOR-001`, `ARCH-DIAG-001`
- Legacy symbols/files: direct panel calls into `AtsAudioProcessor`, timer/worker callbacks, and implicit widget completion
- Replacement owner/API: `ats::ui::EditorActionProtocol`, `EditorAction`, `ActionContract`, `ActionResult`, and `TelemetrySnapshot`
- Thread domain: explicit UI, worker, audio-RT, or host owner per action
- State/format impact: none
- Scope: source-current typed action/result/telemetry boundary
- Required tests: `tests/editor_action_protocol_test.cpp`, `tests/plugin_model_test.cpp`
- Expected evidence: every current action variant has stable naming, owner thread, cancellation rule, undo policy, and invalid-payload result
- Known blocker: real native event dispatch and backend-specific completion remain gated by RMJ-010

The protocol metadata makes action ownership and completion policy explicit for
parameter, file, analysis, preview, timeline, spectral view, preset, MIDI,
history, partial/residual edit, MathExpr, Smearnov, and timbre actions.
Validation is UI-independent and rejects missing paths/IDs, non-finite values,
invalid normalized positions, out-of-range edits, empty scripts, and oversized
bounded payloads before a model service is invoked.

Validation: focused action protocol and existing plugin-model tests pass in the
neutral CMake graph.

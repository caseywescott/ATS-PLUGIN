# RMJ-060 — Generation and refresh ownership report

Task packet:

- Task ID: RMJ-060 / commit 17 follow-up
- Trace IDs: `ARCH-SCHED-001`, `ARCH-SCHED-002`, `ARCH-THREAD-001`
- Legacy symbols/files: JUCE timers/async updates and superseded worker callbacks
- Replacement owner/API: `ats::engine::JobCoordinator`, `ats::JobScheduler`, `ats::ui::UiScheduler`
- Thread domain: worker completion and UI refresh domains; no audio-thread callback or join
- State/format impact: none
- Scope: source-current framework-free scheduler contract
- Required tests: `tests/job_coordinator_test.cpp`, `tests/ui_refresh_ownership_test.cpp`
- Expected evidence: superseded jobs cancel, stale completions are rejected, cadence gates honor focused text/pointer capture/pending gestures/source readiness, cancellation and shutdown stop callbacks
- Known blocker: real native-window dispatch remains gated by the backend ADR

`JobCoordinator` now owns the active generation and cancellation source. Every
new submission cancels its predecessor, and a completion that arrives after a
new generation or cancellation is converted to a typed cancelled result.
`UiScheduler` remains the refresh owner and its focused-text, pointer-capture,
pending-gesture, cadence, cancellation, and shutdown rules are covered by an
explicit ownership test.

Validation: focused tests pass in the neutral CMake graph.

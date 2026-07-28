# RMJ-060 UI scheduler contract

This slice adds the renderer-neutral `UiScheduler` contract. Each registration
owns an explicit cadence gate and is driven by a source snapshot revision. A
refresh is suppressed while its surface is hidden, text-focused, pointer
captured, gesturing, or has no source snapshot. Cancellation and shutdown
disable future callbacks.

`tests/ui_scheduler_test.cpp` covers the 100 ms editor cadence, suppression
and retry behavior, missing-source protection, 16 ms playhead cadence setup,
cancellation, and teardown. The remaining live-backend dispatch harness and
deterministic event trace are still required before the traceability row can
be promoted beyond `mapped`.

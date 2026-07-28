# RMJ-060 scheduler implementation report

Task ID: RMJ-060 / commit 17

## Contract packet

- Trace IDs: `ARCH-SCHED-001`, `ARCH-SCHED-002` (cadence policy portion).
- Legacy symbols/files: `juce::Timer`, `juce::AsyncUpdater`, worker and
  debounce paths in `src/plugin/PluginProcessor.cpp`.
- Replacement owner/API: `ats::JobScheduler`,
  `ats::CancellationSource`/`ats::CancellationToken`,
  `ats::SchedulerCadence`, and `ats::CadenceGate`.
- Thread domain: workers own jobs; callers own completion dispatch. The
  scheduler never invokes UI code or joins from `audio_rt`.
- State/format impact: none.
- Scope: source-current neutral scheduling contract. Native backend timer and
  live editor dispatch evidence now use the pinned Visage probe; full page
  refresh parity remains pending.
- Required tests: queued-token lifetime, pre-start cancellation, mid-work
  cancellation, and cadence/debounce boundary checks.
- Expected evidence: focused scheduler executable and headless CMake graph.
- Remaining gate: `ARCH-SCHED-002` still requires the complete page/widget
  refresh matrix and release evidence; backend selection is no longer a
  blocker.

## Evidence

```text
cmake -S . -B build-review -DBUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-review --target job_scheduler_test -j2
./build-review/tests/job_scheduler_test
job_scheduler_test: typed completion and cancellation ok
```

The scheduler now copies cancellation state into queued tasks. A caller may
release its source token immediately after submission without leaving a worker
with a dangling reference; cancellation during work is reported as a typed
cancelled result with the original generation.

# RMJ-080 neutral model/actions implementation report

Task ID: RMJ-080 / commit 19

## Contract packet

- Trace IDs: `ARCH-EDITOR-001`, `ARCH-DIAG-001`.
- Legacy symbols/files: `AtsAudioProcessor` command callbacks, editor-owned
  parameter writes, progress/error callbacks, and mutable widget truth.
- Replacement owner/API: `ats::engine::PluginModel`,
  `UiActionDispatcher`, `ActionResultDispatcher`, and `TelemetryPublisher`.
- Thread domain: action consumption and telemetry publication are non-real-time;
  worker scheduling is owned by `JobScheduler`.
- State/format impact: parameter writes update the model revision and preview
  invalidation; unsupported services leave state unchanged.
- Scope: neutral composition root and typed action/result ownership. File,
  ATS, analysis, morph, edit, and UI-backend services remain separate tasks.
- Required tests: valid/unknown/non-finite parameter actions, cancellation,
  unsupported-action failure, exactly one result per action, and telemetry
  revision update.
- Expected evidence: `plugin_model_test` on the headless graph.
- Known blocker: native editor lifecycle and real dispatch still require the
  pinned UI backend and host wrappers.

## Evidence

```text
cmake --build build-review --target plugin_model_test -j2
./build-review/tests/plugin_model_test
plugin_model_test: typed action ownership and results ok
```

The composition root now has a single model-facing action owner. It rejects
unbound operations explicitly, preventing a partial migration from silently
claiming that preview, file, or DSP actions completed.

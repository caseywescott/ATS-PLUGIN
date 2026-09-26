# RMJ-050 engine and parameter boundary report

Card and commit: `RMJ-050`, framework-free engine/service boundary slice

Implemented owners:

- `src/core/ParameterRegistry.*` — 81 stable descriptors, raw/normalized
  conversion, snapping, text conversion, and atomic parameter store;
- `src/ui/ParameterBinding.h` — typed gesture begin/update/end actions and the
  single parameter commit dispatcher;
- `src/engine/AtsEngine.*` — worker-domain ATSA analysis and synthesis result
  contracts with progress, cancellation, finite-value, and render statistics;
- `src/core/JobScheduler.h` — bounded worker ownership, typed results, and
  cancellation/generation propagation.

Evidence command:

```text
ctest --test-dir /tmp/ats-juce-removal-headless -R 'parameter_registry_test|parameter_binding_dispatch_test|ats_engine_test|job_scheduler_test' --output-on-failure
```

The neutral targets compile with `BUILD_PLUGIN=OFF` and contain no JUCE
dependency. Host lifecycle, editor backend, and real audio callback wiring are
still deliberately outside this report.

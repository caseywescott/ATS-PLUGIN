# RMJ-080 diagnostics contract

`tests/diagnostics/telemetry_test.cpp` covers bounded typed action-result
delivery and progress lifecycle publication. It verifies generation identity,
finite/clamped progress, cancellation state, bounded queue behavior, and
structured error codes/text without a logger or UI object crossing the neutral
boundary.

Evidence:

```text
cmake --build build-review --target telemetry_test -j2
ctest --test-dir build-review --output-on-failure -R telemetry_test
```

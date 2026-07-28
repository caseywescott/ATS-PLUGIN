# RMJ-010 backend contract report

Task ID: RMJ-010 / neutral prerequisite

## Contract packet

- Trace ID: `ARCH-UI-001`.
- Legacy symbols/files: JUCE component dispatch, popup/focus, coordinates,
  scale, dialogs, text entry, resize, embedding, and teardown.
- Replacement owner/API: `UiBackend`, `UiBackendCapabilities`,
  `UiPointerEvent`, `UiDispatchResult`, `UiScale`, and `UiRect`.
- Thread domain: UI/backend domain; model truth remains behind typed actions.
- State/format impact: none.
- Scope: capability and event contract only. No fake backend or synthetic live
  evidence is introduced.
- Required tests: coordinate round-trip, capability completeness, target-local
  position/delta and stable event identity.
- Expected evidence: `ui_backend_contract_test`.
- Selected backend: Visage pinned to commit
  `20de59464243447816d142e9d38e9723d068f755` in `ATS_VISAGE_VERSION`.
- The real native probe now proves target-window embedding, resize, scale,
  focus, mouse/text dispatch, hide, and teardown. The headless contract test
  remains supplemental coverage rather than native evidence.
- Remaining work is the full widget/popup/dialog parity port and production
  VST3/AU/standalone wrapper integration; those rows are unblocked but not
  promoted to verified.

## Evidence

```text
cmake --build build-review --target ui_backend_contract_test -j2
./build-review/tests/ui_backend_contract_test

cmake --build /tmp/ats-visage-probe-arm64-release --target visage_editor_probe -j2
ctest --test-dir /tmp/ats-visage-probe-arm64-release --output-on-failure -R visage_editor_probe
```

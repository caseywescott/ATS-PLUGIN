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
- Known blocker: no concrete JUCE-free backend revision is pinned in the
  workspace, so target-window dispatch, dialogs, text entry, embedding, and
  teardown cannot yet be proven.

## Evidence

```text
cmake --build build-review --target ui_backend_contract_test -j2
./build-review/tests/ui_backend_contract_test
```

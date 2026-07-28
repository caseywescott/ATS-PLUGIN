# RMJ-080 renderer-free editor seam report

Task ID: RMJ-080 / follow-up to commit 19

## Contract packet

- Trace ID: `ARCH-EDITOR-001`.
- Legacy symbols/files: `AudioProcessorEditor`, page selection, popup/focus,
  resize, scale, and editor teardown paths in `src/plugin/`.
- Replacement owner/API: `HeadlessEditorHost` over `EditorHost`, with stable
  page, focus, popup, lifecycle, logical-size, and action-forwarding state.
- Thread domain: UI/backend domain; telemetry is copied from the bounded
  publisher and no model arrays are exposed.
- State/format impact: none; editor state is intentionally not persisted.
- Scope: renderer-free editor contract. Native windows, text services,
  pointer capture, and backend event dispatch remain separate.
- Required tests: attach rejection/success, page and popup state, focus
  restoration, requested resize, telemetry access, action forwarding, and
  close teardown.
- Expected evidence: `plugin_model_test`.
- Known blocker: `ARCH-UI-001` and real editor lifecycle evidence require a
  pinned JUCE-free backend.

## Evidence

```text
cmake --build build-review --target plugin_model_test -j2
./build-review/tests/plugin_model_test
plugin_model_test: typed action ownership and results ok
```

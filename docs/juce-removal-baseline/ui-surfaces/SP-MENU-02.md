# SP-MENU-02 — Spectrum view menu: checked state and enabled gate

Status: implemented

Replaces `SpectrogramPanel::showViewMenu()` item ticking and graying logic.

## Ownership

`SpectrumInteractionModel::menu()` sets `checked = (modes[i] == mode_)` and
`enabled = hasSource && (modes[i] != residual || hasResidual)`.

- When `hasSource == false` all 7 items are disabled.
- When `hasSource == true` but `hasResidual == false`, only the `residual` item
  (id=2) is disabled; all others are enabled.
- Exactly one item is checked at a time (the current mode).
- When `hasSource == false`, no item is checked (all `enabled` is false, though
  `checked` is still computed; the UI suppresses the popup via `hasSource` guard).

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_menu_checked_and_enabled` verifies: partials mode checked, residual
disabled without residual data, all items disabled with no source.

Deviations: live menu rendering and native focus dispatch require the Visage
native harness.

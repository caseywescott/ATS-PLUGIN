# SP-MENU-03 — Spectrum view menu: selectMenuItem dispatch

Status: implemented

Replaces `SpectrogramPanel::setView()` APVTS write triggered by
`PopupMenu::showMenuAsync()` result.

## Ownership

`SpectrumInteractionModel::selectMenuItem(int id, bool hasSource, bool hasResidual)`
returns a `SetSpectralViewAction{source, mode}`.

- `source` is derived from `sourceIndex(source_)`: 0=current, 1=sourceA,
  2=sourceB.
- `mode` is `static_cast<int>(items[id].mode)` for a valid, enabled item.
- Returns `{source, -1}` when `id` is out of range or the item is disabled.

The caller dispatches the returned action via `EditorHost::dispatch()`.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_menu_dispatch` verifies: sourceA → source=1; partials → correct mode
integer; disabled residual → mode=-1; out-of-range id → mode=-1; sourceB →
source=2.

Deviations: APVTS write path evidence (SetSpectralViewAction reaching the
processor) is covered in the `editor_action_protocol_test` dispatch-contract
matrix.

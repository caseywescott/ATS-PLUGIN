# UI-SHELL-03 — Theme selector

Status: implemented

Replaces the `ComboBox` theme selector in `PluginEditor.cpp`.  Implemented as
an `AtsCombo`-style popup that offers Ember and Porcelain themes.

## Ownership

`EditorShellModel::state().theme` (`ThemeId::ember` or `ThemeId::porcelain`)
is the single authority.  The selector calls `shell_.setTheme(...)` on
selection; no widget stores secondary theme state.

ADR-0004 decision: `uiTheme` is kept as editor/session UI state; no versioned
parameter is added.  Theme is NOT written into `LegacyState` or the 81-parameter
preset format.  Persistence is editor-layer only (DAW UI state where supported).

## Geometry (§10.2 right cluster)

| Property | Value |
|---|---|
| Width | 118 px |
| Height | 30 px |
| Position | Far right of right cluster, right edge |

## Interaction

`ProbeFrame::onMouseDown` checks `point.y < 48 && point.x >= width() - 220`
and opens a `visage::PopupMenu` with options "Ember" (id 1) and "Porcelain"
(id 2).  On selection, `shell_.setTheme(...)` is called and the frame
re-draws.  The `EditorEventSink::themeSelected` callback fires so the
host harness can observe theme changes.

## Action protocol

No `EditorAction` is emitted.  Theme change is UI state only; the selector
drives `ThemeTokens::forTheme(theme)` in every subsequent `draw()` call.

Evidence:

```text
real_dispatch_harness_test: popupShown / popupDismissed sink callbacks exercise
the popup lifecycle.
```

Deviation: none (`UI-SHELL-03` proceeds without a parameter binding per ADR-0004).

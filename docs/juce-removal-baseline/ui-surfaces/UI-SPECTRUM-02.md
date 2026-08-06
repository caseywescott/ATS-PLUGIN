# UI-SPECTRUM-02 — View button and mode popup

Status: implemented

Replaces `SpectrogramPanel::showViewMenu()` + JUCE `PopupMenu::showMenuAsync`.

## Ownership

`SpectrumInteractionModel::menu(hasSource, hasResidual)` builds the 7-item list.
`selectMenuItem(id, hasSource, hasResidual)` returns `SetSpectralViewAction`.
The Visage `PopupMenu` runner in `VisageEditor.cpp` shows the popup on VIEW
button click and emits the action via `emitAction()`.

## Geometry (§10.4)

| Element | x | y | w | h |
|---|---|---|---|---|
| VIEW button | 18 | 216 | 60 | 18 |
| Popup (shown) | at button x | below button | 200 | 7×24 |

## Actions

| Gesture | Action emitted |
|---|---|
| Click VIEW | (shows popup) |
| Select item n | `SetSpectralViewAction { source, mode }` |
| Dismiss | (none) |

Evidence: SP-MENU-01, SP-MENU-02, SP-MENU-03, SP-MENU-04.

Deviation: live popup render and focus lifecycle require native harness.

# UI-SESSION-04 — Variation buttons

Status: implemented

Replaces the three `ToggleButton` variation slots in `SessionTabPanel.cpp`.
Three equal-width buttons (VAR 1 / VAR 2 / VAR 3) sit in the variation row
below the transport row.

## Ownership

`SessionPageModel::state().morph.variationStored[0..2]` (bool array) is the
authority.  `shell_.session().setVariationStored(n, stored)` is the write path;
`shell_.session().variationAction(n, stored)` produces the action.

## Geometry (§10.4 — `variationY` = `transportY + 40`)

| Slot | x | y | w | h |
|---|---|---|---|---|
| VAR 1 | 18 | variationY | 92 | 28 |
| VAR 2 | 118 | variationY | 92 | 28 |
| VAR 3 | 218 | variationY | 92 | 28 |

Each slot is a rounded rect.  Filled with `tokens.accent` when stored,
`tokens.surface` when empty.  Text is `tokens.background` when stored,
`tokens.text` when empty.

## Actions

| Gesture | Action emitted |
|---|---|
| Click slot n | `MorphVariationAction { slot: n, store: !variationStored[n] }` |

`MorphVariationAction` toggles the stored flag; `store: true` saves the
current morph state, `store: false` clears it.

Evidence:

```text
session_page_model_test: variation store / retrieve state machine.
```

Deviation: none.

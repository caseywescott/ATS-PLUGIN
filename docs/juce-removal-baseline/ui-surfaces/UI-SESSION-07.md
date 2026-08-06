# UI-SESSION-07 — Lane children / parameter store

Status: implemented

Replaces the morph lane selector and band policy rows in `SessionTabPanel.cpp`.
Three lane tabs (MORPH / HYBRID / ENVELOPE) gate a set of per-band policy
controls below.

## Ownership

`SessionPageModel::state().morph.selectedLane` (`MorphLane` enum) and
`.policies[0..3]` (`MorphPolicy` enum) are the authority.
`SetParameterAction` is the write path for all controls rendered here.

## Geometry (§10.4 centre column, below band strip)

| Control | x | y | w | h | notes |
|---|---|---|---|---|---|
| MORPH tab | 18 | bandStripY + 28 | 96 | 24 | index 0 |
| HYBRID tab | 120 | bandStripY + 28 | 96 | 24 | index 1 |
| ENVELOPE tab | 222 | bandStripY + 28 | 96 | 24 | index 2 |
| morphAmount slider | 18 | bandStripY + 60 | 604 | 20 | full-width |
| Policy row (4 cells) | 18 | bandStripY + 88 | 604 | 24 | per-band |
| discardOptIn toggle | 18 | bandStripY + 120 | 120 | 20 | gated by DISCARD policy |

Lane tabs are `AtsSegmented`-style: filled with `tokens.accent` when selected.
`morphAmount` is an `AtsSlider` bound to parameter `"morphAmount"`.
Policy cells are four equal-width `AtsCombo` controls bound to
`"bandPolicy0"` through `"bandPolicy3"`.  `discardOptIn` is an `AtsToggle`
bound to `"discardOptIn"`, visible only when at least one policy is `discard`.

## Actions

| Gesture | Action emitted |
|---|---|
| Click lane tab | `SetParameterAction { "morphLane", n }` |
| Drag morphAmount | `SetParameterAction { "morphAmount", v, begin/end }` |
| Change policy | `SetParameterAction { "bandPolicyN", v }` |
| Toggle discardOptIn | `SetParameterAction { "discardOptIn", v }` |

Evidence:

```text
session_page_model_test: setPolicy rejects discard without discardOptIn.
```

Deviation: none.

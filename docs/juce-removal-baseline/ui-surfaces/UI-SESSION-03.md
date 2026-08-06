# UI-SESSION-03 — Audition selector and Smear FX toggle

Status: implemented

Replaces the `auditionSelector` combo and `smearToggle` in `SessionTabPanel.cpp`.
Both controls are implemented inline in `ProbeFrame`; they sit in the transport
row at `transportY` (computed from session control grid height).

## Ownership

`SessionPageModel::state().transport.layer` (a `SessionLayer` enum: both /
partials / residual / smear) and `.smearEnabled` are the authority.
`shell_.session().setTransport(source, layer)` and `smearAction()` are the
write paths.

## Geometry (§10.4 left column transport row)

| Control | x | y | w | h | notes |
|---|---|---|---|---|---|
| BOTH segment | 18 | transportY | 96 | 28 | index 0 |
| PART segment | 120 | transportY | 96 | 28 | index 1 |
| RES segment | 222 | transportY | 96 | 28 | index 2 |
| SMEAR segment | 324 | transportY | 96 | 28 | index 3 |
| SMEAR FX pill | 450 | transportY | 188 | 28 | separate from layer |

`transportY` = `sessionContentY(float((kSessionControlIds.size() + 1) / 2) * 42.0f + 16.0f)`

Layer segments are rendered as an `AtsSegmented`-style group inside
`ProbeFrame::draw()`.  The SMEAR FX pill is a rounded rect that changes color
on `smearEnabled`.

## Actions

| Gesture | Action emitted |
|---|---|
| Click layer segment | (none directly — transport update, then `PreviewAction` if playing) |
| Click SMEAR FX pill | `SetPreviewSmearAction { !smearEnabled }` via `shell_.session().smearAction()` |

Dispatch uses `emitAction(shell_.session().smearAction())`.

Evidence:

```text
session_page_model_test: layer and smear state transitions.
real_dispatch_harness_test: full-stack mouse event routing.
```

Deviation: none.

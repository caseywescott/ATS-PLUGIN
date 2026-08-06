# UI-SPECTRUM-03 — Zoom bar and canvas navigation

Status: implemented

Replaces `TraceZoomBar` JUCE component + `applyZoomAtScreenPoint`.

## Ownership

`SpectrumInteractionModel::zoom(factor, anchorT, anchorF)` and `pan(dT, dF)`
and `fit()` own the viewport state (`timeStart`, `timeEnd`,
`frequencyStart`, `frequencyEnd`).  Mouse wheel / pinch events on the canvas
call `zoom()`; middle-drag calls `pan()`; `0` key calls `fit()`.

## Geometry (§10.4)

| Element | x | y | w | h | notes |
|---|---|---|---|---|---|
| Zoom bar track | 24 | 440 | 608 | 6 | time axis only |
| Zoom thumb (left) | 24 + timeStart*608 | 438 | 4 | 10 | draggable |
| Zoom thumb (right) | 24 + timeEnd*608 - 4 | 438 | 4 | 10 | draggable |

Dragging a thumb updates `timeStart`/`timeEnd`; dragging the track interior
pans.  The frequency axis zoom is driven by pinch gesture only.

## Actions

| Gesture | Action emitted |
|---|---|
| Mouse wheel on canvas | (navigation, no action) |
| Pinch on canvas | (navigation, no action) |
| Zoom thumb drag | (navigation, no action) |
| `0` key | (fit, no action) |

Navigation state changes trigger `SpectrumViewModel::invalidateCache()` →
redraw.

Evidence: SP-NAV-01, SP-NAV-02, `spectrum_interaction_model_test`.

Deviation: live mouse-wheel scaling measured in native harness only.

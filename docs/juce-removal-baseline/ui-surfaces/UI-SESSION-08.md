# UI-SESSION-08 — Timbre motion / macro card

Status: implemented

Replaces `TimbreMotionPanel.cpp`.  Three macro knobs (BRIGHTNESS / TILT /
TONAL-NOISE) and a single curve editor that together produce a
`SetTimbreMotionPlanAction`.

## Ownership

`SessionPageModel::state().timbre` (`SessionTimbreState`: `brightness`,
`tilt`, `tonalNoise`, `curve`) is the authority.  `SetParameterAction` drives
the three knobs; `SetTimbreMotionPlanAction` is emitted on curve commit.

## Geometry (§10.4 right column)

| Control | x | y | w | h |
|---|---|---|---|---|
| BRIGHTNESS knob label | 660 | 120 | 80 | 14 |
| BRIGHTNESS knob | 660 | 136 | 80 | 80 |
| TILT knob label | 750 | 120 | 80 | 14 |
| TILT knob | 750 | 136 | 80 | 80 |
| TONAL-NOISE knob label | 840 | 120 | 80 | 14 |
| TONAL-NOISE knob | 840 | 136 | 80 | 80 |
| Curve editor | 660 | 228 | 300 | 120 |

Knobs are `AtsSlider` children bound to `"timbreBrightness"`, `"timbreTilt"`,
`"timbreTonalNoise"` respectively.  The curve editor is a custom inline frame:
draggable control points connected by a smooth bezier; on mouse-up it encodes
the point array and emits `SetTimbreMotionPlanAction`.

## Actions

| Gesture | Action emitted |
|---|---|
| Drag knob | `SetParameterAction { id, v, begin/end }` |
| Drag curve point (release) | `SetTimbreMotionPlanAction { normalizedPlan }` |

Evidence:

```text
session_page_model_test: setTimbre round-trip.
```

Deviation: none.

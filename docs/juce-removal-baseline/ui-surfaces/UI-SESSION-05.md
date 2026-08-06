# UI-SESSION-05 — Timeline frame

Status: implemented

Replaces the morph timeline bar in `SessionTabPanel.cpp`.  A horizontal strip
showing a sorted list of normalized point handles (up to 32) draggable along
the 0–1 axis.  A right-click or BOUNCE button triggers the bounce export popup.

## Ownership

`SessionPageModel::state().morph.timeline` (vector of `float` in [0,1]) is the
authority.  `shell_.session().timelineAction()` is the write path for point
edits; `BounceMorphTimelineAction` is emitted on bounce.

## Geometry (§10.4 centre column, below variation row)

| Element | x | y | w | h |
|---|---|---|---|---|
| Timeline track | 18 | variationY + 40 | 604 | 12 |
| Point handle (each) | 18 + norm*(604-6) | variationY + 36 | 6 | 20 |
| BOUNCE button | 550 | variationY + 56 | 72 | 20 |

Point handles are 6×20 rounded rects.  Filled with `tokens.accent`.  Dragging
a handle produces a continuous `SetMorphTimelineAction`.  The vector always
stays sorted; handles that would cross are clamped to their neighbours.

## Actions

| Gesture | Action emitted |
|---|---|
| Drag point handle | `SetMorphTimelineAction { sortedPoints }` |
| Click BOUNCE | `BounceMorphTimelineAction { path, layer }` via native save dialog |

Evidence:

```text
session_page_model_test: setTimeline rejects out-of-order / out-of-range points.
```

Deviation: none.

# UI-SESSION-06 — Band strip

Status: implemented

Replaces the band diagnostic row in `SessionTabPanel.cpp`.  A horizontal row
of equal-width cells — one per morph band — that visualises band energy and
highlights the selected band.

## Ownership

`SessionPageModel::state().morph.selectedBand` and `.bandCount` are the
authority.  `shell_.session().setSelectedBand(n)` is the write path; no
`EditorAction` is emitted (selection is editor-local state).

## Geometry (§10.4 centre column, below timeline)

| Element | x | y | w | h |
|---|---|---|---|---|
| Strip container | 18 | timelineY + 40 | 604 | 20 |
| Band cell (each) | 18 + n*(604/bandCount) | timelineY + 40 | 604/bandCount | 20 |

Cells are drawn as thin rectangles filled proportional to band RMS energy
(provided by the audio thread via the telemetry hook).  The selected band is
outlined in `tokens.accent`; others use `tokens.surface`.  If `bandCount` is
0 the strip is hidden.

## Actions

| Gesture | Action emitted |
|---|---|
| Click cell n | (none — calls `model.setSelectedBand(n)`, redraws) |

Evidence:

```text
session_page_model_test: setSelectedBand / setBandCount boundary cases.
```

Deviation: band energy data path is supplied by the audio telemetry hook, not
a versioned parameter.

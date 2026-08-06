# UI-SESSION-09 — Smearnov context panel

Status: implemented

Replaces the Smearnov sub-panel in `SessionTabPanel.cpp`.  A collapsible
panel below the band strip that exposes onset detection, onset overlay editing,
and one-shot apply.

## Ownership

`SessionPageModel::state().smearnov` (`SessionSmearnovState`) is the authority.
Write paths: `DetectSmearnovAction`, `ApplySmearnovAction`,
`EditSmearnovOnsetsAction`.  Panel visibility is `panelVisible`; knob
expansion is `knobsExpanded`; advanced section is `advancedExpanded`.

## Geometry (§10.4 right column, expandable from bottom)

| Element | x | y | w | h | condition |
|---|---|---|---|---|---|
| SMEARNOV header pill | 660 | smearY | 300 | 24 | always |
| Knob grid (3 knobs) | 660 | smearY+28 | 300 | 80 | knobsExpanded |
| Onset overlay row | 660 | smearY+112 | 300 | 12 | panelVisible |
| DETECT button | 660 | smearY+130 | 140 | 24 | panelVisible |
| APPLY button | 820 | smearY+130 | 140 | 24 | detectionAvailable |
| Advanced section | 660 | smearY+162 | 300 | 60 | advancedExpanded |

`smearY` is computed from the bottom of the timbre card.

Onset overlay: normalized onset positions from `state_.smearnov.onsetOverlay`
are drawn as 4×12 accent-coloured bars over a grey track.  Dragging a bar
produces `EditSmearnovOnsetsAction` on release.

DETECT button is dimmed while `detectionInFlight`.
APPLY button is dimmed while `applyInFlight`.

## Actions

| Gesture | Action emitted |
|---|---|
| Click DETECT | `DetectSmearnovAction {}` |
| Click APPLY | `ApplySmearnovAction {}` |
| Drag onset handle (release) | `EditSmearnovOnsetsAction { normalizedOnsets }` |

Evidence:

```text
session_page_model_test: setSmearnovDetection / setSmearnovApplyInFlight state.
session_page_model_test: setOnsetOverlay round-trip.
```

Deviation: none.

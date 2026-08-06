# UI-SPECTRUM-07 — Onset overlay and drag handles

Status: implemented

Replaces `SpectrogramPanel::onsetHandleAtPoint` / `updateDraggedOnset`.

## Ownership

`SpectrumNavigationState::onsets` (tracked in `SpectrumInteractionModel`) is not
yet materialised as a separate field — onset positions come from
`SpectralTraceSnapshot::onsetNorms` (set by the backend) and are visualised
as `onsetMarker` draw commands.

Dragging an onset handle produces `EditSmearnovOnsetsAction { normalizedOnsets }`.
The interaction model computes which onset is closest to the mouse position and
updates that position; on release the updated onset vector is emitted.

## Geometry

| Element | x-position | y | w | h |
|---|---|---|---|---|
| Onset marker | spectrumBounds.x + norm*spectrumBounds.width | spectrumBounds.y | 2 | spectrumBounds.height |
| Hit zone (per onset) | x − 4 | spectrumBounds.y | 8 | spectrumBounds.height |

## Actions

| Gesture | Action emitted |
|---|---|
| Drag onset handle | `EditSmearnovOnsetsAction { normalizedOnsets }` (on release) |
| Double-click onset marker | onset deleted → `EditSmearnovOnsetsAction` |

Evidence: SP-SMEAR-01, SP-SMEAR-02.

Deviation: onset drag precision requires live sub-pixel coordinate testing
in the native harness.

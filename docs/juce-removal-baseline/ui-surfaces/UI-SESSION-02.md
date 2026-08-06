# UI-SESSION-02 — Transport / play-preview controls

Status: implemented

Replaces the play-button, source selector, and playhead scrubber from
`SessionTabPanel.cpp`.  Implemented in `ProbeFrame` at fixed y-ranges below
the source cards.

## Ownership

`SessionPageModel::state().transport` is the authority (`SessionTransport`
containing `source`, `layer`, `playing`, `smearEnabled`,
`normalizedPlayhead`).  `shell_.session().setPlaying(...)` is the only write
path; widgets do not maintain secondary playing state.

## Geometry (§10.4 left column, below source cards)

| Control | x | y | w | h |
|---|---|---|---|---|
| PLAY PREVIEW / STOP PREVIEW pill | 18 | 190 | 128 | 20 |
| Source selector (A/B/CURRENT) + layer label | 160 | 193 | 360 | 14 |
| Playhead track (accent bar) | 18 | 204 | 380 | 3 |
| Playhead thumb (accent rect) | 160 + 238*pos | 201 | 3 | 9 |
| Playhead scrubber hit area | 160 | 190 | 238 | 20 |

Source A / B card-top area (y < 148): clicking the card top selects that
source and re-starts preview if playing.

## Actions

| Gesture | Action emitted |
|---|---|
| Click play pill | `PreviewAction { source, layer, true }` or `StopPreviewAction {}` |
| Click playhead scrubber | `SeekPreviewAction { normalized }` |
| Click source card header | `PreviewAction { source, layer, true }` (if playing) |
| Click transport layer segment | `PreviewAction { source, layer, true }` (if playing) |

Layer segment (four 102 px slots: BOTH / PART / RES / SMEAR) at `transportY`.
SMEAR FX pill at (450, transportY, 188, 28) toggles `SetPreviewSmearAction`.

Evidence:

```text
session_page_model_test: transport state machine (play/seek/stop, layer, smear).
real_dispatch_harness_test: exercises playback via sendEmbeddedMouse at
known coordinates.
```

Deviation: none.

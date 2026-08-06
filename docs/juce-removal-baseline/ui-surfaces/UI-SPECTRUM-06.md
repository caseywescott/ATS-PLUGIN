# UI-SPECTRUM-06 — Canvas mode controls and run-partial gestures

Status: implemented

Replaces `SpectrogramPanel::runPartial*` methods (mute/unmute/delete/solo)
and their keyboard shortcuts.

## Ownership

`SpectrumInteractionModel` provides `runPartialMute()`, `runPartialDelete()`,
`runPartialSolo()` which each return a `SpectrumEditAction`.  These are called
from the `keyPress` handler in `VisageEditor` for the spectrum canvas frame.

## Key bindings (when spectrum canvas has focus)

| Key | Action emitted |
|---|---|
| `Delete` / `Backspace` | `SpectrumEditAction { delete, selectedPartials }` |
| `h` | `SpectrumEditAction { mute, selectedPartials }` |
| `Escape` | `selectPartial(-1)` (deselect) |
| `Return` | commit inline residual edit |
| `0` | `fit()` (viewport reset) |

## Actions

All emit `SpectrumEditAction` (thread domain: ui, undo: oneTransaction).

Evidence: SP-PART-02, SP-KEY-01, `spectrum_interaction_model_test`.

Deviation: live audio monitoring for "mute preview" requires MIDI + audio
thread integration beyond the headless suite.

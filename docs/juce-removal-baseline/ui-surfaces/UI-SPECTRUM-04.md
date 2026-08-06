# UI-SPECTRUM-04 — Partial selection and run gestures

Status: implemented

Replaces `SpectrogramPanel::mouseDown/mouseDrag/mouseUp/keyPressed` for
partial-mode interaction.

## Ownership

`SpectrumInteractionModel::selectPartial(index)` sets `selectedPartial`.
`PartialEditAction` carries the committed edit (amplitude + frequency).
Run actions (`runPartialMute`, `runPartialDelete`, `runPartialSolo`) are
emitted as `SpectrumEditAction` variants by `SpectrumInteractionModel`.

## Gestures

| Gesture | Model method | Action emitted |
|---|---|---|
| Left-click partial trace | `selectPartial(index)` | (editor-local) |
| Double-click partial | `selectPartial(index)` + focus | (editor-local) |
| Drag selected partial (amp) | (draft update) | (none during drag) |
| Release drag | (commit) | `PartialEditAction { partial, frame, amp, freq }` |
| `Delete`/`Backspace` key on selection | `runPartialDelete` | `SpectrumEditAction { delete, partials }` |
| `h` key | `runPartialMute` | `SpectrumEditAction { mute, partials }` |

Evidence: SP-PART-01, SP-PART-02, `spectrum_interaction_model_test`.

Deviation: live hit-test coordinates (partial hit radius) tuned in native harness.

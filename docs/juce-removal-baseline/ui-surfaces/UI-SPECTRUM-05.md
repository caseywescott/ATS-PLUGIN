# UI-SPECTRUM-05 — Residual band selection and editing

Status: implemented

Replaces `SpectrogramPanel::mouseDown/mouseDoubleClick` for residual-mode
interaction and `applyResidualDbFromEditor` / `commitEnvelopeDraw`.

## Ownership

`SpectrumInteractionModel::selectResidualRange(bands, firstFrame, lastFrame)`
sets the selection.  `ResidualEditAction` carries committed edits.

## Gestures

| Gesture | Model method | Action emitted |
|---|---|---|
| Left-click band cell | `selectBand(index)` | (editor-local) |
| Left-drag rect select | `selectResidualRange(bands, f0, f1)` | (editor-local) |
| Double-click cell | enter inline edit | `ResidualEditAction { band, frame, value }` |
| `Delete` key on selection | — | `SpectrumEditAction { resetResidual, bands }` |
| Copy/paste band | `runCopyBand` / `runPasteBand` | `SpectrumEditAction { copy/paste, band }` |
| Redistribute bands | `runRedistBands` | `SpectrumEditAction { redist, bands }` |

Evidence: SP-RES-01, SP-RES-02, SP-RES-03.

Deviation: inline dB text editor requires native focus dispatch harness.

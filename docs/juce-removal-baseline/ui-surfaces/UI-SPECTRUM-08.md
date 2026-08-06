# UI-SPECTRUM-08 — Envelope display and box-gain editing

Status: implemented

Replaces `SpectrogramPanel::isEnvelopeDisplayMode` and `scaleEnvBoxGain`.

## Ownership

`SpectrumInteractionModel::setEnvelopeRange(timeStart, timeEnd, freqStart, freqEnd)`
sets the active editing region.  `SpectrumNavigationState::envelopeEditing` is
true when the user is dragging a gain box.

`envelopeTrace` draw commands are emitted by `SpectrumRenderer` from
`SpectralTraceSnapshot::envelopeNorm[]`.  The gain-box overlay is a separate
`residualBand`-class rect around the selection.

## Gestures

| Gesture | Model method | Action emitted |
|---|---|---|
| Drag gain box handle | `setEnvelopeRange(...)` | (editor-local) |
| Release drag | (commit) | `SpectrumEditAction { envelopeScale, range, factor }` |
| Double-click canvas (env mode) | `setEnvelopeRange(...)` | (editor-local) |

## Actions

`SpectrumEditAction` (thread domain: ui, undo: oneTransaction).

Evidence: SP-ENV-01, `spectrum_interaction_model_test`.

Deviation: isometric envelope modes (UI-SPECTRUM-08 + isometricEnvelope)
share gesture routing but use 3D-projected hit tests not covered by headless suite.

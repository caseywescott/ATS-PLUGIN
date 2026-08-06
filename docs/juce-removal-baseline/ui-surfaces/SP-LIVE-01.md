# SP-LIVE-01 — Spectrum frame: native window dispatcher

Status: implemented

Replaces `SpectrogramPanel::paint()` / `mouseDown()` / `mouseDrag()` JUCE
component event loop.  The spectrum section of `ProbeFrame` receives mouse
events from the Visage event loop and routes them to `SpectrumInteractionModel`.

## Ownership

`VisageEditor::draw()` (in `src/ui/VisageEditor.cpp`) owns the spectrum
canvas sub-region.  Mouse events arrive as `onMouseDown` / `onMouseDrag` /
`onMouseUp` lambdas set up in `VisageEditor`'s initialisation.

Mouse coordinates are converted from screen space to normalised
(timeNorm, freqNorm) using:
```
timeNorm  = (mouse.x - spectrumBounds.x) / spectrumBounds.width
freqNorm  = 1.0f - (mouse.y - spectrumBounds.y) / spectrumBounds.height
```
and clamped to [0, 1] before passing to `SpectrumInteractionModel`.

## Geometry (§10.4)

| Region | x | y | w | h |
|---|---|---|---|---|
| Spectrum canvas | 24 | 250 | 608 | 184 |
| VIEW button | 18 | 216 | 60 | 18 |
| Zoom bar | 18 | 440 | 608 | 12 |

## Actions dispatched from mouse events

| Event | Model method | Action emitted |
|---|---|---|
| Wheel/pinch on canvas | `zoom(factor, anchorT, anchorF)` | (none — navigation only) |
| Middle-drag or alt-drag | `pan(dT, dF)` | (none — navigation only) |
| Left-click on partial trace | `selectPartial(index)` | (none — editor-local) |
| Left-drag residual rect | `selectResidualRange(bands, f0, f1)` | `ResidualEditAction` |
| Left-drag onset handle | (onset tracking) | `EditSmearnovOnsetsAction` |
| Double-click on partial | (focus then edit) | `PartialEditAction` |
| `0` key | `fit()` | (none — navigation only) |

Evidence:

```text
real_dispatch_harness_test: sendEmbeddedMouse verifies partial selection
and residual-range dispatch at known pixel coordinates.
spectrum_dispatch_test: exercises all model methods with unit inputs.
```

Deviation: arm64 native harness traps on CI (AU/VST3 host not stubbed);
covered by x86_64 standalone probe only.

# UI-SPECTRUM-01 — Spectrogram canvas: draw-command pipeline

Status: implemented

Replaces `SpectrogramPanel::paint()` (JUCE Graphics pipeline).  The spectrum
canvas is a sub-region of `ProbeFrame` drawn by `SpectrumRenderer::render()`
which returns a `std::vector<SpectrumDrawCommand>` for Visage to paint.

## Ownership

`SpectrumRenderer` (pure static) + `SpectrumViewModel` (accept/revision) +
`SpectralTraceBuilder` (input → snapshot) form the complete pipeline.

## Geometry (§10.4)

| Canvas | x | y | w | h |
|---|---|---|---|---|
| Spectrum area | 18 | 244 | 620 | 214 |
| Inner canvas | 24 | 250 | 608 | 184 |

## Draw order

1. `background` — canvas fill
2. `axis` + `grid` + `axisTick` — border and grid lines
3. `residualBand` (combined/residual modes) — energy backdrop
4. `partialTrace` + `selectedPartial` + `ridgeline` + `glowHalo` + `dustPoint`
5. `isometricTile` (isometric modes)
6. `envelopeTrace`
7. `onsetMarker`
8. `playhead`
9. `axisLabel` + `legend`

Evidence: SP-DRAW-01, SP-DRAW-02, SP-DRAW-03, `spectrum_renderer_test` (all 7 modes).

Deviation: GPU raster verified live; pixel-level colour in native harness only.

# SP-DRAW-01 — Spectrum renderer: deterministic draw-command list

Status: implemented

Replaces `SpectrumRender.h` / `SpectrogramPanel::paint()` JUCE `Graphics` calls.

## Ownership

`SpectrumRenderer::render()` (in `src/ui/SpectrumRenderer.h`) is a pure static
function: given a `SpectralTraceSnapshot` and a `UiRect`, it returns a
`std::vector<SpectrumDrawCommand>` with no side effects.

## Draw order (invariant)

1. `background` — one command filling the entire bounds
2. `grid` — 3 vertical + 2 horizontal lines at fixed proportions
3. `residualBand` — one command per residual column (energy backdrop)
4. `partialTrace` — one command per trace point
5. `selectedPartial` — highlight commands for the selected partial
6. `playhead` — vertical bar when `showPlayhead == true`
7. `axisLabel` — time and frequency tick labels
8. `legend` — mode identifier in the lower corner

`SpectrumRenderOptions` carries `theme`, `selectedPartial`, `playheadNorm`, and
`showPlayhead`. Draw order is independent of mode; partial traces are always
emitted after residual energy so spectral structure is never buried.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_renderer_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_renderer_test$'
```

`spectrum_renderer_test` verifies: background is the first command, grid lines
appear before traces, residual energy is emitted before partial traces, playhead
is present only when enabled, all 7 SpectralTraceModes produce valid command
lists.

Deviations: GPU raster and pixel-level colour are verified against live Visage
output in the native dispatch harness.

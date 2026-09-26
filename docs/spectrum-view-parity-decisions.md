# Spectrum view parity decisions

This is the review record for the 19 parity traps in §8 of
`docs/spectrum-view-parity-spec.md`.

| # | Decision | Implementation/evidence |
|---:|---|---|
| 1 | Reproduced the shipped header reservation mismatch. | `SpectrumChromeLayout` anchors to the five-pixel panel inset while `SpectrumGeometry` keeps the JUCE 30/50-pixel screen subtraction. The four 30-pixel modes therefore retain the documented overlap. |
| 2 | Deliberately did not port the dead residual-toolbar constant. | No `kResidualToolbarHeight` equivalent exists; toolbar height is derived from the live layout. |
| 3 | Reproduced planar-only zoom anchoring. | `SpectrumInteractionModel` uses the planar normalized anchor path for every mode, including isometric views. |
| 4 | Reproduced non-preserving mode changes. | `setMode` fits the viewport and clears selection/hover state. |
| 5 | Reproduced edit preservation rules. | Mute/delete rebuild non-preservingly; scale, draw, smoothing, tilt, copy/paste, swap, redistribution, solo, freeze, and prune retain the display reference. |
| 6 | Replaced the manual latch with revision-keyed snapshot publication. | `SpectrumViewModel` and `HostAdapter` key acceptance/cache invalidation by source and display-shape revisions. |
| 7 | Fixed stale residual hover repaint. | Mouse exit clears residual hover state and requests redraw; snapshot rebuilds also clear transient hover state. |
| 8 | Reproduced the looser playhead wake cull. | Wake lines use the fixed six-pixel depth and are emitted independently of trace culling. |
| 9 | Reproduced projection-specific residual time mapping. | Planar columns use cell centres; isometric columns use endpoint-normalized positions. |
| 10 | Reproduced unbounded source normalization, with painter clamps. | Snapshot amplitudes remain above 1.0; planar/isometric projection clamps are applied only at paint time. |
| 11 | Reproduced the descending truncate / ascending draw sort. | Builder truncates loudest-first, then reverses to draw low-to-high. |
| 12 | Reproduced max residual-column energy. | Residual construction takes the maximum sample in each column, not an average. |
| 13 | Reproduced the Cmd/Ctrl+0 dispatch quirk while keeping fit reachable. | The plain `0` path handles both modifier states and resets the viewport. |
| 14 | Reproduced passive Smearnov overlays. | Curves, markers, and handles are emitted only while onset editing is enabled. |
| 15 | Fixed independent secondary-envelope normalization. | The complementary snapshot receives the same resolved display reference before rendering. |
| 16 | Reproduced rebuild-cleared hover state. | `setSnapshot` clears partial, residual-band, and residual-frame hover values on every accepted snapshot. |
| 17 | Replaced recursive relayout with draw-time geometry. | Onset visibility is passed into `SpectrumChromeLayout`; no snapshot callback mutates component bounds. |
| 18 | Replaced the physical-pixel cache with logical vector commands. | Spectrum geometry is rebuilt in logical coordinates and painted as paths/rects by Visage. |
| 19 | Reproduced the exact deterministic dust stream. | `SpectrumRenderer` uses JUCE's 48-bit `Random` recurrence, seed `0x5eed`, and unchanged per-dot call order. |

The executable checks for the layout and renderer regressions are
`spectrum_chrome_test`, `spectrum_renderer_test`, and
`spectrum_renderer_perf_test`.

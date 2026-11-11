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

## 2026-11-10 display review: fixes and deliberate departures

A review of the Visage spectrum painter against the deleted JUCE
`SpectrogramPanel.cpp` (recoverable with `git show 1a64f030^:src/plugin/SpectrogramPanel.cpp`)
found port regressions, plus two legacy defects that are now fixed on purpose.
Entries 8 and 9 above are superseded.

| Area | Before | Now | Kind |
|---|---|---|---|
| Playhead | Planar line at the unzoomed position in every mode; six 1-px wake rects | Mapped through the time zoom and culled outside it; 24-px gradient wake, two-weight line and head. Isometric modes draw the legacy floor line, wake quad and front dot | port regression |
| Onset overlay | Markers, handles and hit test ignored zoom; Smearnov curves filled the whole plot height, unnormalised | Spans, edge lines, three-weight markers with heads, strength-scaled; curves in a bottom strip normalised to their joint peak, threshold dashed; markers and hit test use the viewport | port regression |
| Frequency gutter | Five labels, min..max of the full range, ignoring frequency zoom | 1-2-5 log ticks that follow the zoom, `k` suffix, 13-px declutter | port regression |
| Band gutter | Evenly spaced `Bnn` labels that matched neither the bands nor the gutter hit test | Label at each band's centre, through the zoom, `B<n>` plus low edge, declutter, hover/selection highlight | port regression |
| Partials | One opacity per run from the run's peak | Per-stop alpha from each stop's amplitude (`TraceStop::alpha`); hover drawn in the hover colour | port regression |
| Isometric partials | No culling: off-range points projected beside the floor when zoomed; no depth cue | Data-space culling (2.5% / 4% pads), depth shade, back-to-front order, floor shadows, three smoothing passes; picking skips culled points | port regression |
| Residual (planar) | Line only, same alpha in Combined and Residual | Filled ridge per band, per-stop alpha, brighter in Residual with a baseline | port regression |
| Isometric residual | Per-column coloured segments, no culling | Filled ridge with perpendicular fade, energy gate, depth shade, culling, hover/selection halo and baseline; no planar selection box | port regression |
| Envelope waterfall | Bin split could exceed 1 at the top edge (negative weight); RDC had no edge taper and fell back to the Gaussian curve when a fit failed; B drawn over A | Clamped bin split; RDC clamped to [0,1], tapered, slice skipped on a failed fit or fewer than 3 points; depth alpha, halo, crest tint; source B behind A on its own ramp | port regression |
| Envelope edit range | Planar rectangle ignoring the viewport | Isometric slab with a time-range label | port regression |
| Readouts | None | Footer (`n/m partials` or `n residual bands`, Hz range), hover readout, frequency-shape pivot line and badge | port regression |
| Residual columns | `ceil(frames / 220)` grouping: tail columns silent (a 100-frame file drew residual over the first 45% of the axis) | `min(220, frames)` columns partition frames proportionally; one column-time mapping (centre frame on the partial axis) for planar, isometric and hit tests | **legacy defect, fixed** |
| Time labels | Drawn in isometric modes too | Planar only (the isometric time axis is diagonal) | legacy behaviour restored |

| Header / tool row | JUCE reserved 30 px outside the residual modes, so the tool row covered the screen title; residual tools overflowed a design-size panel; the zoom bar covered the envelope A/B buttons | Every mode with a tool row reserves 50 px; residual buttons narrower, tilt slider fits the remaining width, the redundant "dB" label dropped; zoom bar takes the space left of a fixed button reservation | legacy defect, fixed |
| Line joints | Each translucent segment had round caps, so joints doubled alpha and read as beads | Round caps only at path ends | painter fix |
| Fills | Strips and polygons drawn as triangles; anti-aliased shared edges showed as hatching | One `visage::Path` fill each, not counted in the quad budget | painter fix |
| Band gutter declutter | Bands under 12 px were unlabelled, i.e. all 25 bands at the design size | Labels spaced 11 px apart bottom-up; `k` suffix above 1 kHz | improvement |
| Envelope kernel | Density floor 0.08 widened sparse slices up to ~3.5x into a plateau | Floor 0.25 (at most 2x) | improvement |
| Envelope at playhead | — | An extra slice at the playhead time, drawn on top with a playhead-tinted halo | new |
| Scroll / zoom thumbs | Near-white slabs at full zoom | Teal, inset, dim when there is nothing to scroll | improvement |
| Isometric captions | Fixed panel corners: AMP under the zoom bar, TIME under the footer | FREQ under the front edge, AMP above the origin post, TIME left of the origin (in the wide well every spot beside the floor is crossed by envelope ridges) | improvement |

Regression checks are in `spectrum_renderer_test`, `spectrum_trace_snapshot_test` and
`spectrum_chrome_test`. `ui_screenshot_matrix --ats <file>` captures a real model.

## 2026-11-11 session layout and view selection

User report: the spectrum view could not be switched to the residual or
envelope views, and the view was too small to work in.

| Area | Before | Now | Kind |
|---|---|---|---|
| View selection | Every view sat in a submenu per source (Current / A / B). Visage swallows the first mouse-up in each list, so a view took two clicks inside a submenu | A wide well shows the seven views as one-click buttons (`SpectrumChromeLayout::modeButtons`) and a source chip whose flat menu lists Current / A / B. A narrow well keeps one chip whose menu is flat: the seven views, a break, the three sources | deliberate departure |
| Selected view lost | The host built every spectrum snapshot as Trace + Residual, and the editor copied the snapshot's mode on every UI tick, so any selection snapped back at once | The host publishes `spectralViewMode_`; the editor adopts the host's mode only when the host's value changes (`hostSpectrumMode_`), so a snapshot published before the host applied a selection cannot undo it | bug fix |
| Envelope A/B buttons | Shown in the envelope views | Only in the narrow header; the wide header's source chip replaces them | deliberate departure |
| Source chip label | `Current: <file>` centred in a 132-px chip, clipped at both ends | Cut with an ellipsis and left-aligned when it does not fit | layout fix |
| SESSION layout | Spectrum well in the centre column (about 570 x 290 at 1280 x 940) above the timeline and engine panel; MORPH, LANES and TIMBRE filled the right column | The spectrum panel spans the centre and right columns (well about 945 x 315 at 1280 x 940: 44% of the page height, capped so the right panel fits). The timeline and engine panel sit under it in the centre; under it on the right, a MORPH / LANES / TIMBRE strip shows one group above the bottom-anchored output group. The three groups do not fit together under the spectrum even at the largest window | deliberate departure |
| Engine grid | Three columns; SMEARNOV showed 18 of its 20 controls at 1280 x 940 | Four columns when three would drop cells; all 20 show at 1280 x 940. Segmented labels shrink (to 70%) to fit narrow cells | layout fix |

Checks: `spectrum_chrome_test` (strip and chip geometry, narrow fallback),
`page_column_layout_test` (panel spans, every tab and the output group fit,
both engine grids complete), `host_action_routing_test`
(`test_spectrum_snapshot_carries_selected_view`), and
`popup_focus_retention_test` (flat source menu selects with one click; the
Partials button switches without a popup).

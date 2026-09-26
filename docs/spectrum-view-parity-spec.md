# Spectrum Overview — JUCE Parity Port Spec

**Goal:** bring the Visage (JUCE-free) spectrum overview to complete parity with the
retired JUCE `SpectrogramPanel`, at pixel fidelity, including every editing operation.

**Source of truth:** the retired JUCE sources, not the design docs.

```
git show 1a64f030^:src/plugin/SpectrogramPanel.cpp   # 5522 lines — the panel
git show 1a64f030^:src/plugin/SpectrogramPanel.h     #  276 lines — member/API inventory
git show 1a64f030^:src/plugin/SpectrumRender.h       #  368 lines — theme, LUT, path pipeline
git show 1a64f030^:src/plugin/PluginProcessor.h      #  L225-300 data model, L383-514 ops
```

All `cpp:NNNN` citations below are line numbers in `SpectrogramPanel.cpp` at `1a64f030^`.
Dump it once (`git show 1a64f030^:src/plugin/SpectrogramPanel.cpp > /tmp/juce_spectrogram.cpp`)
and work against that.

### Why the docs are *not* the source of truth here

This reverses the project's usual "Ember spec docs win over the JUCE build's pixels" rule,
by explicit decision on 2026-08-27. The rule still holds for every other surface.

A related correction: `docs/spectrum-view-overhaul-spec.md` was **already implemented in
JUCE** — `SpectrumRender.h` carries its `// spec §2.1` / `§2.2` / `§3.2` markers, and
`paintScreen` has the vignette (cpp:842) and filled residual ridges (cpp:972). So the
blue→lime amplitude LUT, the glow passes and the vignette in the current Visage build are
*correct in kind* and must be kept. What differs is that JUCE's constants evolved past the
draft in that doc, and the Visage port copied the draft. Where the two disagree, JUCE wins.

`docs/ats-mearcstapa-ui-spec.md` §8.1 (accent-coloured 1px traces, log-freq 40 Hz–14 kHz)
describes a *different, older* design that the JUCE build never adopted. It is superseded
for this surface. Do not implement it.

---

## 1. Current state

Files in play:

| File | Role | Verdict |
|---|---|---|
| `src/ui/SpectralTraceBuilder.h` | input → `SpectralTraceSnapshot` | close to parity; needs fields added |
| `src/ui/SpectrumViewModel.h` | accept/revision/cache-epoch | broadly sound |
| `src/ui/SpectrumRenderer.h` | snapshot → `SpectrumDrawCommand[]` | **major rewrite** |
| `src/ui/SpectrumInteractionModel.h` | viewport + selection + keys | **major rewrite** |
| `src/ui/VisageEditor.cpp` (~L455-850) | command painting, hit-test, input | **major rewrite** |
| `src/engine/PartialEditService.h` | partial ops | present, unused by this surface |
| `src/engine/ResidualEditService.h` | residual ops | present, unused by this surface |

The eight `docs/juce-removal-baseline/ui-surfaces/UI-SPECTRUM-0*.md` documents all read
`Status: implemented`. That status is wrong for every one of them and must be corrected as
part of this work (§9). They describe a design roughly a third the size of the JUCE panel.

### 1.1 Verified defects

**D1 — the viewport is not applied.** `SpectrumInteractionModel` maintains `timeStart`,
`timeEnd`, `frequencyStart`, `frequencyEnd`, but `SpectrumRenderer::render()`
(`SpectrumRenderer.h:117`) accepts no viewport and maps `timeNorm`/`freqNorm` across the
full bounds. Wheel-zoom and drag-pan mutate state that never reaches the renderer, so the
image never changes. This is the single largest defect.

**D2 — hit-testing is fabricated.** `VisageEditor.cpp:831-849`:

```cpp
spectrumFrameAt   → clamp(normX * 255.0f, 0, 255)
spectrumPartialAt → clamp((1 - normY) * 31.0f, 0, 31)
spectrumBandAt    → clamp((1 - normY) * 7.0f, 0, 7)
spectrumValueAt   → 1 - normY
```

None consult the snapshot. Clicking selects an arbitrary index and `applyResidualEdit`
writes an arbitrary value to the model. JUCE used `findNearestPartial(pos, maxDistancePx)`
over real projected geometry with `distanceToSegment`.

**D3 — the envelope waterfall is fabricated.** `SpectrumRenderer.h:513-548` redraws the
*same* `snapshot.envelopeNorm` for all 30 slices at increasing depth offsets. JUCE builds
genuine per-frequency slices (`makeSliceTimes`, cpp:1801; Gaussian box-blur `boxBlurInPlace`,
cpp:1779) and renders them as ridgelines via `drawRidgeSlices`.

**D4 — residual mode is unreachable.** `VisageEditor.cpp:643` calls `menu(true, false)`,
hardcoding `hasResidual = false`, so the Residual item is permanently disabled.

**D5 — single-selection only.** JUCE kept `std::vector<int> selectedPartials` plus
`selectedResidualBands` + a frame range. The renderer takes one `selectedPartial` int.
No lasso, no scale-drag, no draw modes, no envelope range box.

**D6 — chrome is absent.** Missing entirely: band-label gutter (34 px), freq-label gutter,
amp legend, isometric grid and axis posts, zoom bar, two scrollbars (13 px), the residual /
partial / envelope toolbars, `"ATS TRACE DISPLAY"` title, `"NO ATS MODEL"` empty state,
time-axis second labels, harmonic guides. `axisLabel` and `legend` are emitted as empty
placeholder rects and dropped by the painter (`VisageEditor.cpp:532-536`).

**D7 — two theme constants are wrong.** `SpectralCanvasTokens` took its values from the
draft overhaul spec rather than the shipped JUCE theme:

| Token | Visage now | JUCE (`SpectrumRender.h:17-26`) | |
|---|---|---|---|
| `background` | `0xff05080d` | `0xff0a1220` | **fix** |
| `bgVignetteEdge` | `0xff030407` | `0xff050a12` | **fix** |
| `gridMinor` | `0xff0e2a33` | `0xff0e2a33` @0.22 | ok |
| `gridMajor` | `0xff14454f` | `0xff14454f` @0.35 | ok |
| `axisFrame` | `0xff1e6e6e` | `0xff1e6e6e` @0.55 | ok |
| `selection` | `0xffffe28a` | `0xffffe28a` | ok |
| `hover` | `0xffeafffb` | `0xffeafffb` | ok |
| `playhead` | `0xff7affd4` | `0xff7affd4` | ok |
| `residualLow` / `residualHigh` | `0xff123b4a` / `0xff1e7a6a` | same | ok |

Note `spectrum_renderer_test.cpp:46` asserts the wrong value (`0xff05080d`) and must be
updated with the fix.

**D8 — the trace pipeline is structurally different.** JUCE: binomial `[1,2,1]` smooth ×2 →
`decimateInPlace(tol 0.75 px, maxSkip 24 px, amp delta 0.08)` → one Catmull-Rom cubic-Bézier
`juce::Path` → **a single gradient stroke** with per-stop colours, endpoint taper and one
optional bloom pass. Visage: crude 1.5 px decimation → Catmull-Rom resampled at 3 steps →
**one draw command per segment**. Different vertex counts, different joins, visible seams.

**D9 — the editing backend is missing.** All 17 JUCE processor ops are absent from `src/`.
Residual ops were reimplemented as `engine::ResidualEditService` and partial ops as
`engine::PartialEditService`, but the spectrum canvas uses neither. `solo`, `freeze`,
`prune` and partial tilt/smooth-envelope exist nowhere in the tree.

**D10 — the data model cannot express onsets.** JUCE drove three drag handles per onset from
`AtsOnset { frame, preFrame, peakFrame, strength }` (`src/core/AtsTransientSmear.h:46`).
`SpectralTraceSnapshot` carries only `std::vector<float> onsetNorms`, which cannot represent
the pre/peak edges. Also missing: `smearnovDetectionCurve`, `smearnovThresholdCurve`, and
the four `frequencyShape*` fields.

**D11 — tests assert presence, not correctness.** `spectrum_renderer_test.cpp` only checks
that a command of a given kind exists. Every assertion passes with D1, D2 and D3 intact.

---

## 2. Data model

Extend `SpectralTraceSnapshot` (`src/ui/SpectralTraceBuilder.h`) to match
`AtsSpectralTraceSnapshot`:

```cpp
struct SpectralOnset {                 // replaces the flat onsetNorms vector
    float norm = 0.0f;                 // frame       → normalized time
    float preNorm = 0.0f;              // preFrame    → left edge handle
    float peakNorm = 0.0f;             // peakFrame   → right edge handle
    float strength = 0.0f;
};
std::vector<SpectralOnset> onsets;
std::vector<float> smearnovDetectionCurve;
std::vector<float> smearnovThresholdCurve;
FrequencyShapeMode frequencyShapeMode = FrequencyShapeMode::Linear;
double frequencyShapeAmount = 1.0;
double frequencyShapePivotHz = 0.0;
double resolvedFrequencyShapePivotHz = 0.0;
```

Keep `onsetNorms` only if a caller still needs it; prefer deleting it outright.

**Normalization must match** `resolveSpectralTraceDisplayScaling`
(`PluginProcessor.cpp:8185-8207` at `1a64f030^`). The current builder is close but differs
in two respects that matter:

- `ampNorm` must be **clamped below at 0 and left unbounded above**. The builder currently
  calls `clampUnit`, which caps at 1.0. After a gain-up edit on a preserved display
  reference, `ampNorm` legitimately exceeds 1.0 — that is why planar paint clamps to 2.0
  (cpp:1169) and the isometric amp axis clamps to 1.25 (cpp:4402). Capping at 1.0 silently
  flattens loud content.
- Residual column energy is a **max** over the frames in the column, never a mean, so
  decimation cannot dim a transient. The current builder already does this — keep it.

Frequency bounds are log-padded by `pad = max(0.04, (logMax - logMin) * 0.06)` at both ends.

**Partial ordering is load-bearing.** Sort by peak amplitude **descending**, truncate to
`kMaxVisiblePartials = 900`, then re-sort **ascending** so the loudest draw last and land on
top. The current builder does this; do not "simplify" it. Limits: 900 partials, 720
points/partial (`pointStep = max(1, ceil(frames / max(32, maxPointsPerPartial)))`, and the
final frame is always appended regardless of step), 220 residual columns.

---

## 3. Geometry and viewport

Port verbatim from cpp:4314-4560.

```
getScreenBounds()        = localBounds.reduced(5), then removeFromTop(isResidual ? 50 : 30)
getTraceContentBounds()  = screen.reduced(14, 12), removeFromTop(18),
                           removeFromBottom(18 + 13), removeFromRight(13)
getBandLabelGutter()     = content.removeFromLeft(34)      // when either gutter shows
getPlotBounds()          = content, minus 34 left if a gutter shows, minus 10 right

visibleTimeRange() = { timeCenter - 0.5/max(1,timeZoom), timeCenter + 0.5/max(1,timeZoom) }
visibleFreqRange() = { freqCenter - 0.5/max(1,freqZoom), freqCenter + 0.5/max(1,freqZoom) }

kMinZoom 1.0   kMaxZoom 32.0   kZoomBarWidth 204.0
kMaxVisiblePartials 900   kMaxPointsPerPartial 720   kResidualColumns 220
kWheelPanPixelsPerDelta 14.0 * 16.0
traceScrollBarThickness 13   kBandLabelGutterWidth 34
```

Gutter predicates (cpp:4321-4331): the band gutter shows only in `Mode::Residual` with a
non-empty band list and `frameCount > 0`; the freq gutter shows in non-isometric,
non-residual modes with `frameCount > 0`. Isometric residual labels its floor edges instead.

Planar projection (cpp:4372):

```
xNorm = (timeNorm - timeRange.start) / max(1e-6, timeRange.length)
yNorm = (freqNorm - freqRange.start) / max(1e-6, freqRange.length)
pos   = { plot.x + plot.w * xNorm, plot.bottom - plot.h * yNorm }
```

Isometric projection (cpp:4391), an oblique axonometric with three axis vectors:

```
ampNorm  = clamp(pointAmpNorm, 0.0, 1.25)
origin   = { plot.x + plot.w * 0.075, plot.bottom - plot.h * 0.085 }
freqAxis = { plot.w * 0.66,  -plot.h * 0.055 }
timeAxis = { plot.w * 0.255, -plot.h * 0.44  }
ampAxis  = { 0.0,            -plot.h * 0.30  }
pos = origin + freqAxis*freqNorm + timeAxis*timeNorm + ampAxis*ampNorm
```

The axis-height budget is a real constraint: `0.085 + 0.055 + 0.44 + 0.30*1.25 = 0.955 ≤ 1.0`.
Changing any constant without re-checking this clips peaks at the back of the plot.

The current `isoProject` in `SpectrumRenderer.h:462-468` is an unrelated cabinet-oblique
approximation and must be deleted.

`timeNormFromIsoPoint` (cpp:4419) inverts the projection for time only, by projecting the
cursor onto the front-bottom edge (freq = front, amp = 0) where screen position is linear in
time. `isoTimeFreqFromPoint` (cpp:4436) recovers both axes. Both are needed for isometric
hit-testing.

---

## 4. Rendering

Port `SpectrumRender.h` wholesale into a Visage-native `src/ui/SpectrumPathBuilder.h`. It is
pure geometry with only `juce::Point`/`juce::Path`/`juce::Colour` as dependencies; all three
have direct Visage equivalents. Keep the function names so the diff against JUCE stays
reviewable: `smoothInPlace`, `decimateInPlace`, `buildGradientTrace`, `strokeGradientTrace`,
`drawRidgeSlices`, `ampColour`.

### 4.1 The draw-command model must change

`SpectrumDrawCommand` is a flat rect + colour + thickness. It cannot express a
Catmull-Rom path stroked with a multi-stop gradient, which is what JUCE draws. Emitting one
command per segment (what the code does today) is precisely D8.

Add a path-capable command carrying a vertex list plus its gradient stops, and stroke it
once in `VisageEditor.cpp` via Visage's path/gradient API:

```cpp
struct SpectrumPathCommand {
    SpectrumDrawKind kind;
    std::vector<UiPoint> vertices;   // post-smooth, post-decimate, screen space
    std::vector<TraceStop> stops;    // proportion, amp, shade, fade
    UiPoint gradStart, gradEnd;
    float width = 1.0f;
    bool flat = false;               // axis under 2px — single flat colour
    bool glow = false;               // maxAmp > 0.6 → extra pass at width*2.4 @12% alpha
};
```

If Visage cannot stroke a gradient path directly, fall back to per-stop path splitting — but
split on the **stop list** (typically 3-8 stops after the `buildGradientTrace` dedupe), never
per vertex.

### 4.2 Partial traces (cpp:1053-1178)

Two passes: unselected first, then selected, so selection always draws on top.

Per contiguous active run (a run breaks at any `!point.active`):

1. Cull: if `run.size() <= 4 && peakAmp < 0.3 && !selected && !hovered`, drop the run.
   Prevents frame-scale confetti.
2. `smoothInPlace(run, 2)` — binomial `[1,2,1]`, endpoints pinned, over both position and amp.
3. `decimateInPlace(run)` — keep a vertex when deviation > 0.75 px, or span > 24 px, or
   `|amp delta| > 0.08`.
4. `buildGradientTrace(run)` — Catmull-Rom with tangent `(next - prev) / 6`, cubic Bézier.
   Endpoint taper `fade = 0.18 + 0.82 * in * out` over 7 px. Stops are deduped: skip when
   `p <= last.p + 0.01` or `p >= 0.995`, or when amp delta < 0.02 **and** shade delta < 0.05
   **and** fade delta < 0.08. Runs whose axis is under 2 px (`len2 < 4.0`) go `flat` with
   averaged amp/shade and `shortFade = 0.18 + 0.82*min(1, total/14)`.
5. Skip if `path.bounds` does not intersect `plot`.
6. Stroke:
   - selected: halo 6.0 px `selection` @0.18, then core 1.7 px `selection`
     @`min(1, 0.72 + maxAmp*0.2)`
   - hovered: halo 4.0 px `hover` @0.18, then core 1.7 px `hover` @ same formula
   - normal: `strokeGradientTrace(width 1.0)`, colour
     `ampColour(min(1, amp)).withAlpha((0.28 + ampVis*0.62) * fade)`.
     Constant width — loudness lives in colour and alpha only.
     Bloom pass only when `maxAmp > 0.6`, at `width * 2.4` and 12% of the stop alpha.

**Harmonic guides** (cpp:1059-1100): with **exactly one** partial selected, draw dashed
horizontals at integer multiples `k = 2..32` of its `meanFrequencyHz`, dashes `{5,5}`,
0.8 px, `selection` @0.14, labelled `"{k}f"` at 10 pt `selection` @0.38, 4 px in from
`plot.x`. Skip multiples outside the visible range or above `maxHz`.

### 4.3 Backdrop (cpp:827-962)

Draw order, all inside `reduceClipRegion(plot)`:

1. `fillRoundedRectangle(screen, 5.0)` in `background`; `drawRoundedRectangle(screen.reduced(0.5), 5.0, 1.0)`
   in `axisFrame`; a white @0.08 top-edge highlight — `screen.reduced(1.5)` trimmed to a 3 px
   tall strip, radius 4, 1 px.
2. Radial vignette `background` at `plot.centre` → `bgVignetteEdge` at `plot.topLeft`.
3. **Dust.** `juce::Random dustRng(0x5eed)`, `dots = plot.w * plot.h / 2600`. Per dot:
   `x, y` uniform in plot; `r = 0.4 + rand*0.9`; `a = 0.02 + rand*0.07`; colour
   `rand < 0.3 ? 0xff58c9a8 : 0xff4f7fd9`; filled ellipse. **The seed and the draw order
   are part of the image** — the RNG is consumed in exactly this sequence, so reordering the
   `nextFloat()` calls changes every dot. Port a deterministic LCG matching `juce::Random`,
   or precompute the dot table once and store it.
4. Grid — non-isometric: 17 verticals `i = 0..16` and 13 horizontals `i = 0..12`, `gridMajor`
   when `i % 4 == 0` else `gridMinor`, snapped with `roundToInt`. Isometric: `paintIsometricGrid`
   (cpp:1219).
5. Time labels along the bottom, every 4th vertical, at 8.5 pt: tick `axisFrame` @0.35,
   0.8 px, 5 px tall; text `0xff6fa3b0` @0.75 in a 44×10 box 17 px above `plot.bottom`.
   Decimals: `visSpanSec < 2 → 2`, `< 20 → 1`, else `0`. First label left-aligned at `x+3`,
   last right-aligned at `x-47`, others centred at `x-22`.
6. `"ATS TRACE DISPLAY"` at 12 pt `0xff6fa3b0`, 14 px in from `screen.x`, 6 px down, 15 px tall.
7. If `frameCount <= 0`: `"NO ATS MODEL"` centred in `plot` at 15 pt `0xff5bffbb`, and
   **nothing else** is drawn. Otherwise the mode painters, then `paintAmpLegend`,
   `paintSmearnovOverlay`, `paintSelectionOverlay`.
8. Outside the clip: the band gutter, else the freq gutter.

### 4.4 Amp legend (cpp:1180-1217)

`area = { plot.right - 140, plot.y + 8, 132, 38 }`; suppressed when `area.x < plot.x + 4`.
Background `background` @0.78 radius 4; border `axisFrame` @0.45. Title at 9 pt `0xff6fa3b0`,
letter-spaced `"A M P"`, or `"E N E R G Y"` in `Residual`/`IsometricResidual`. Ramp bar
`{ area.x+8, area.y+16, area.w-16, 6 }` radius 3, `ampColour(0)` → `ampColour(1)` with extra
stops at 0.25/0.5/0.75. Labels at 8.5 pt `0xff6fa3b0` @0.85 on a row 25 px down:
`"-72 dB"` / `"0"`, or `"lo"` / `"hi"` for the residual legend.

### 4.5 Residual, isometric and envelope modes

Port directly; each is self-contained:

| Painter | Lines | Notes |
|---|---|---|
| `paintResidualBackdrop` | 964-1052 | filled ridges per band, `foreground` flag distinguishes Combined (backdrop) from Residual |
| `paintIsometricGrid` | 1219-1344 | floor plane, axis posts, depth-faded grid |
| `paintIsometricPartials` | 1345-1515 | as §4.2 but via `positionForIsoCoords`, with depth shading |
| `paintIsometricResidual` | 1516-1694 | per-band ridges in projection |
| `paintEnvelopeSurface` | 1816-1945 | real slices; `makeSliceTimes` 1801, `boxBlurInPlace` 1779, `boxRadiiForGauss` 1759 |
| `paintIsometricEnvelope` | 2030-2054 | wrapper |
| `paintEnvelopeSurfaceRDC` | 2179-2306 | RDC variant; basis fit in `namespace rdcDisplay` 2055-2158 |
| `paintSelectionOverlay` | 2307-2542 | lasso, residual rect, env range box, readouts |
| `paintSmearnovOverlay` | 2649-2763 | onset markers and handles |
| `paintSmearnovDetectionCurves` | 2764-2858 | detection + threshold curves |
| `paintBandLabelGutter` / `paintFreqLabelGutter` | — | gutter text |

The RDC path (`rdcDisplay`, cpp:2055-2158) is a least-squares basis fit —
`barkNorm`, `buildBasisRow`, `solveGaussian`, `fitRDC`. It is pure math with no JUCE
dependency; port it as-is into a testable header.

**A/B envelope overlay:** in the two envelope modes JUCE builds a *secondary* snapshot from
the complementary source (`SourceA↔SourceB`, `Current→SourceA`) and renders it with a
distinct warm ramp. `SpectrumRenderOptions::overlaySnapshot` exists but only carries
`envelopeNorm`; it must carry a full snapshot.

---

## 5. Interaction

Rewrite `SpectrumInteractionModel` around JUCE's state, and delete the fabricated helpers at
`VisageEditor.cpp:831-849`.

### 5.1 Selection state

```cpp
std::vector<int> selectedPartials;
std::vector<int> selectedResidualBands;
int selectedResidualFirstFrame = -1, selectedResidualLastFrame = -1;
int hoveredPartial = -1, hoveredResidualBand = -1, hoveredResidualFrame = -1;
float envRangeStartNorm = -1, envRangeEndNorm = -1;    // -1 = none
float envFreqStartNorm  = -1, envFreqEndNorm  = -1;    // -1 = full depth
```

### 5.2 Real hit-testing

- `findNearestPartial(pos, maxDistancePx)` — iterate visible traces, project each consecutive
  active point pair for the current mode, and take the minimum `distanceToSegment`
  (`SpectrumRender.h:85`). Return -1 past the radius.
- `residualBandForY` / `residualBandForPoint` / `residualBandsForRect` / `residualBandForGutterPoint`
  from `yRangeForBand`.
- `frameForX` / `timeNormFromScreenX` / `freqNormFromScreenY`, with isometric variants.
- `traceIntersectsLasso` for rubber-band selection.
- `onsetHandleAtPoint` (cpp:3149) returning `OnsetHandle{ None, Marker, LeftEdge, RightEdge }`.

### 5.3 Mouse state machine

Port cpp:3258-3915 (`mouseDown`, `mouseDrag`, `mouseUp`, `mouseMove`, `mouseDoubleClick`,
`mouseExit`, `mouseWheelMove`, `mouseMagnify`). It multiplexes: pan (`panningView`), lasso
(`draggingLasso`), scale-drag (`scalingSelection`), envelope range select
(`selectingEnvRange`), envelope gain (`scalingEnvGain`), residual envelope draw
(`drawingResidualEnvelope`), partial frequency draw (`drawingPartialFreq`) and onset drag
(`draggingOnsetIndex`). Today's editor unconditionally pans on drag
(`VisageEditor.cpp:718-728`) and emits an edit action on any non-moving mouse-up.

Wheel (cpp:3832): zoom at cursor, or pan by `kWheelPanPixelsPerDelta` with modifiers.
Magnify (cpp:3904) drives `applyZoomAtScreenPoint` too.

### 5.4 Keyboard

Port `keyPressed` (cpp:3916-4042) in full. The current `handleKey` is unrelated — note in
particular that its `'h'` branch selects every partial where `partial % 12 == hovered % 12`,
which has no JUCE counterpart and should be deleted.

---

## 6. Editing operations

Route through the existing engine services rather than reinventing. Coverage today:

| JUCE op | Backend status | Action |
|---|---|---|
| `mutePartials` (delete) | `PartialEditKind` has no delete/mute | **add** |
| `scalePartials` | `PartialEditKind::scaleSelection` | wire |
| `paintPartialFrequency` | `PartialEditKind::drawFrequency` | wire |
| `gainPartialEnvelope` | `PartialEditKind::gainBox` | wire |
| `smoothPartialEnvelope` | absent | **add** |
| `tiltPartialEnvelope` | absent | **add** |
| `playSelectedPartialsPreview` (Solo) | absent | **add** |
| `freezePartialAtFrame` (Freeze) | absent | **add** |
| Prune | client-side only — see below | UI only |
| `muteResidualBands` | `ResidualEditOperation::mute` | wire |
| `scaleResidualBands` | `::scale` | wire |
| `writeResidualBandEnergyDb`, `paintResidualBandEnvelope` | `::drawDb` | wire |
| `smoothResidualBands` | `::smooth` | wire |
| `tiltResidualBands` | `::tilt` | wire |
| `copyResidualBandToClipboard` / `paste` | `::copy` / `::paste` | wire |
| `swapResidualBands` | `::swap` | wire |
| `autoRedistributeResidualBands` | `::redistribute` | wire |
| `readResidualBandEnergyDb` | absent (read path) | **add** |

`SpectrumEditCommand` must grow past its six residual-only values to cover the partial ops.

**Prune is not a model mutation.** `runPartialPrune` (cpp:5360-5394) opens a modal asking for
a threshold percentage (default `"10"`) and merely *selects* every trace with
`peakAmpNorm < threshold/100`, then updates the label and repaints. Its own dialog text says
*"Press Delete to remove them after."* Implement it as a selection helper; do not add an
engine op.

### 6.1 Rebuild discipline after an edit

Which rebuild variant an op uses is observable, so it is part of parity:

- `mutePartials` and `muteResidualBands` use the **non-preserving** rebuild, so deleting
  visibly renormalises the remaining content.
- Every other edit uses `rebuildTraceSnapshot(true)` (preserving).
- After `scalePartials` / `scaleResidualBands` / `gainPartialEnvelope`, the panel manually
  latches `lastModelRevision = processor.getModelRevision()` (cpp:4210, cpp:4538). Omit that
  latch and the display flashes a renormalisation one timer tick after every drag.
- `copyResidualBandToClipboard` triggers **no** rebuild.

---

## 7. Chrome and layout

`resized()` (cpp:2909-2993). Header is 50 px when `residualUi || envelopeUi || partialUi`,
else 26 px; row 1 is 26 px.

Row 1, left to right: `viewMenuButton` 182 · gap 6 · `deleteButton` 72 · gap 8 ·
`onsetEditButton` 90 + gap 8 (only when visible) · in envelope modes `envSourceABtn` 76 ·
gap 4 · `envSourceBBtn` 76 · gap 4 · `selectionLabel` fills. `zoomBar` is removed from the
**right** at `kZoomBarWidth = 204`, then a 6 px gap.

Row 2 (the toolbar), one of three:

- **residual:** Draw 52 · Smooth 62 · Copy 52 · Paste 56 · Swap 56 · Redist 56 (4 px gaps) ·
  gap 8 · tilt slider `max(72, w/4)` · 4 · Tilt 44 · gap 8 · dB label 44 · dB editor `max(72, w)`
- **envelope:** smooth label 76 · 4 · smooth slider `max(120, w/3)` · 8 · `"Smooth Env"` 88
- **partials:** Draw 52 · Solo 48 · Freeze 56 · Prune 52 (4 px gaps) · gap 8 ·
  tilt slider `max(72, w/3)` · 4 · Tilt 44

Then `layoutTraceViewChrome()` (scrollbars + zoom bar) and the playhead overlay is set to
`getPlotBounds()`.

Two child components need Visage equivalents: `TraceZoomBar` (cpp:48-208) and
`PlayheadOverlay` (cpp:209-452, its own 60 Hz timer over the plot).

The view menu (`showViewMenu`, cpp:496-583) is built from `getModelFootprint` and
`getMorphSourceSlotInfo(0/1)`, so Source A/B entries carry live slot names and are disabled
when empty. The current `menu(hasSource, hasResidual)` takes two bools and is called with
`(true, false)` — that must become a real capability query (D4).

Timers: the panel runs at **10 Hz**, the playhead overlay at **60 Hz**.

---

## 8. Parity traps

Behaviours that a clean reimplementation will silently get wrong. Each needs a deliberate
decision recorded in the PR.

1. **Header-height mismatch.** `resized()` reserves 50 px whenever
   `residualUi || envelopeUi || partialUi`, but `getScreenBounds()` subtracts 50 only when
   `isResidualDisplayMode` and 30 otherwise. That is a genuine ~20 px overlap in Combined,
   Partials, IsoEnvelope and IsoEnvelopeRDC. **Recommendation: reproduce it in the port,
   then fix it as a separate, visible commit** — folding a fix into the port makes the
   screenshot diff unreviewable.
2. `kResidualToolbarHeight = 24` is declared in the header and never referenced. Dead — do
   not port.
3. **Zoom anchoring is planar-only.** `applyZoomAtScreenPoint` uses planar anchor math even
   in isometric modes, where screen X maps to frequency and screen Y to time. Isometric
   zoom-at-cursor is "wrong" but is the shipped behaviour.
4. A mode change alone triggers a non-preserving rebuild, so switching view renormalises.
5. Delete ops are non-preserving; all other edits preserve. See §6.1.
6. The manual `lastModelRevision` latch after scale/gain edits. See §6.1.
7. `mouseExit` does not repaint when only a residual band was hovered, leaving a stale
   highlight.
8. `playheadPaintBounds` uses looser cull thresholds than the painters (0.05 vs 0.01/0.025;
   wake depth 0.05 vs 0.03). Tightening them to match creates visible trails.
9. **Residual column time mapping differs by projection:** planar uses `(c + 0.5) / columns`,
   isometric uses `c / (columns - 1)`. A half-column offset.
10. `ampNorm` is unbounded above — clamp to 2.0 in planar paint, 1.25 on the isometric amp
    axis. See §2.
11. Partials are sorted twice (descending to truncate, ascending to draw). See §2.
12. Residual column energy is a max, not a mean. See §2.
13. `Cmd/Ctrl + '0'` in `keyPressed` is unreachable — the plain `'0'` clause matches first.
14. Passive Smearnov markers draw nothing unless `onsetEditButton` is toggled on.
15. The secondary (alt-source) envelope snapshot is built with an **empty** display
    reference, so the two surfaces in the envelope modes normalise independently.
16. Hover state is cleared on every rebuild, so at 10 Hz a changing model kills the hover
    readout until the next `mouseMove`.
17. `resized()` can be re-entered from `rebuildTraceSnapshot` via the onset-button
    visibility flip — the only data-driven relayout path.
18. The display cache is allocated at **physical** pixel scale
    (`getPhysicalPixelScaleFactor()`), not logical; at logical resolution the gutter text
    blurs on retina.
19. The dust field is a deterministic RNG walk — seed, count and call order all matter. See §4.3.

---

## 9. Implementation phases

Each phase ends green, with its own tests, and is separately reviewable. Stage by explicit
path — never `git add -A` in this repo.

**Phase 0 — foundation.** Fix the two theme constants (D7) and the test that asserts the
wrong one. Extend `SpectralTraceSnapshot` (§2), including unbounded-above `ampNorm`. Port
`SpectrumRender.h` → `src/ui/SpectrumPathBuilder.h` with unit tests for `smoothInPlace`,
`decimateInPlace`, `buildGradientTrace` and `ampColour`. No visible change yet.

**Phase 1 — geometry and viewport (fixes D1).** Port §3 verbatim. Thread the viewport into
`SpectrumRenderer::render()`. Add the path-capable draw command (§4.1). This is the first
phase with a visible payoff: zoom and pan start working.

**Phase 2 — planar rendering (fixes D8).** Rewrite `paintPartialTraces` per §4.2 and the
backdrop per §4.3, including dust, grid, time labels, title and empty state. Add the amp
legend (§4.4) and both gutters.

**Phase 3 — isometric and envelope (fixes D3).** Port the isometric grid/partials/residual
painters, real envelope slices with the Gaussian blur, the RDC basis fit, and the A/B
overlay snapshot.

**Phase 4 — interaction (fixes D2, D5).** Real hit-testing, the full mouse state machine,
`keyPressed`, the selection overlay, multi-select and lasso. Delete the fabricated helpers.

**Phase 5 — chrome (fixes D4, D6).** `TraceZoomBar`, `PlayheadOverlay`, scrollbars, the three
toolbars, the real view menu with live source names and residual capability.

**Phase 6 — editing (fixes D9).** Wire the existing engine services, then add the missing
ops: partial delete/mute, smooth envelope, tilt, Solo, Freeze, plus the residual dB read
path. Prune as a selection helper. Honour the rebuild discipline in §6.1.

**Phase 7 — reconciliation.** Rewrite the eight `UI-SPECTRUM-0*.md` baseline docs against
what actually shipped and correct their `Status:` lines. Update
`tests/architecture/ats_juce_replacement_traceability.json`. Decide trap #1 (header overlap)
in a standalone commit.

---

## 10. Verification

### 10.1 Build and test targets

Per `docs`-adjacent build conventions in this repo:

- `src/ui/` changes → build and ctest in **`build-review-ui`**
  (`ATS_BUILD_VISAGE_UI_PROBE=ON`).
- `src/host/` changes → **`build-plugin`**.
- To retest in Logic → rebuild **`build-macos-arm64`** and run
  `scripts/install_logic_au.sh --build-dir build-macos-arm64 --arch arm64`. The installer
  refuses to run while Logic is open.

The four existing targets — `spectrum_renderer_test`, `spectrum_interaction_model_test`,
`spectrum_trace_snapshot_test`, `spectrum_dispatch_test` — are declared in
`tests/CMakeLists.txt`, which applies a directory-wide `-UNDEBUG`, so their asserts are live.

**Any new test target declared outside `tests/` needs its own
`target_compile_options(<t> PRIVATE -UNDEBUG)`** or it compiles to an empty program and
passes unconditionally. The visage UI tests in the top-level `CMakeLists.txt` are exactly
this hazard.

The CMake target name for a test is often unprefixed (`host_action_routing_test`, not
`ats_host_action_routing_test`). A wrong name makes `make` fail while `ctest` happily reruns
the stale binary and reports a pass — always confirm the build succeeded before trusting a
green ctest.

### 10.2 Every fix must fail first

For each defect D1-D11 and each parity trap: write the assertion, confirm it **fails**
against current `HEAD`, then implement. For regressions found later, reinstate the defect,
rebuild, confirm the specific assertion fires, restore, confirm green.

Assert **upstream of any normalizing write path** (clamp, snap, saturate, coerce). A test
written against post-clamp state can never fail — this has already produced one worthless
test on this project. §2's unbounded-above `ampNorm` is precisely such a path: assert on
builder output, not on a value read back after a clamp.

On this exFAT drive, `touch` a source file after any out-of-band restore (`cp` back,
`sed -i` + `mv`) or the rebuild is a silent no-op and the test binary stays stale.

### 10.3 Test coverage to add

Presence assertions are not enough (D11). Required:

| Target | Assertions |
|---|---|
| `spectrum_geometry_test` (new) | `getScreenBounds`/`getTraceContentBounds`/`getPlotBounds` against a table of known sizes and modes; gutter predicates; the isometric axis-height budget `0.085+0.055+0.44+0.30*1.25 ≤ 1.0`; `positionForPoint` and `positionForIsoCoords` against hand-computed values; round-trip `timeNormFromIsoPoint(positionForIsoCoords(t,f,0)) ≈ t` |
| `spectrum_renderer_test` | **geometry**, not presence: at `timeZoom=4, timeCenter=0.25`, a point at `timeNorm=0.25` lands at `plot.centreX`; a point outside the visible range is culled. This is the D1 regression test — confirm it fails on `HEAD` first. Also: exact theme constants; the two-pass selected-last draw order; the `run.size()<=4 && peak<0.3` cull; the `maxAmp>0.6` glow threshold |
| `spectrum_path_builder_test` (new) | `decimateInPlace` keeps endpoints and honours all three keep-conditions; `smoothInPlace` pins endpoints; `buildGradientTrace` stop dedupe and the taper formula; `ampColour` at 0/0.25/0.5/0.75/1 against the four LUT stops |
| `spectrum_interaction_model_test` | `findNearestPartial` returns -1 past the radius and the true nearest within it (this is the D2 regression test); lasso membership; the full `keyPressed` table; zoom-at-anchor keeps the anchor stationary; `clampViewport` at both extremes |
| `spectrum_trace_snapshot_test` | `ampNorm > 1.0` survives a preserved display reference; the double sort leaves loudest last; residual column energy is a max not a mean; onset pre/peak norms round-trip |
| `spectrum_dispatch_test` | every op in the §6 table emits the right action with the right selection payload; the rebuild-variant discipline of §6.1 |
| `spectrum_rdc_test` (new) | `fitRDC` / `solveGaussian` against a known-solution linear system |

### 10.4 Visual verification

The JUCE reference app still exists at `build-juce-reference/ATS Resynth.app`. It is the
target — capture it and the Visage build side by side for all 7 modes.

Both clamp to roughly 1200×900, so they cannot tile on a 1728-wide display; capture
separately with `screencapture -o -l<windowid>`, getting ids from Quartz
`CGWindowListCopyWindowInfo` and filtering by **pid**, since both processes are named
"ATS Resynth".

**Kill the target app before capturing.** `open .../ATS Resynth.app` only re-focuses an
already-running instance, so a capture can show a UI built hours earlier. Run
`ps -eo pid,lstart | grep "ATS Resynth"`, kill the pid, `open`, and confirm the new start
time before capturing.

Capture matrix — 7 modes × {empty model, `crotales.ats`} × {fit, 8× time zoom} —
plus, per mode: hover, single selection, multi-selection via lasso, and an active playhead.

Golden-image diffing is the goal but the dust field (trap 19) and the 60 Hz playhead make
naive full-frame diffs noisy. Either seed both builds identically and mask the playhead
column, or diff with a small per-pixel tolerance and assert on the *fraction* of differing
pixels.

### 10.5 Performance

Budget: full repaint of a dense model ≤ 25 ms at 1600×900, measured on `crotales.ats`.
Decimation should *reduce* total vertices versus a naive port. The display cache means the
cost is paid on invalidation, not per frame — so also assert that a playhead-only frame does
**not** invalidate the cache.

---

## 11. Acceptance criteria

- [ ] Zoom and pan visibly change the rendering in all 7 modes; the anchor point stays put
      under wheel-zoom (D1).
- [ ] Clicking a trace selects *that* trace; clicking empty space selects nothing (D2).
- [ ] The envelope waterfall shows genuine per-frequency slices, verifiably different from
      one another (D3).
- [ ] All 7 view-menu items are reachable, with live Source A/B names and correct
      enable/disable (D4).
- [ ] Lasso multi-select, scale-drag, both draw modes and the envelope range box all work (D5).
- [ ] Band gutter, freq gutter, amp legend, isometric grid, zoom bar, both scrollbars, all
      three toolbars, title and empty state all render (D6).
- [ ] `SpectralCanvasTokens` matches `ats_spectrum::theme` exactly (D7).
- [ ] Traces render as single smooth gradient-stroked paths — no segment seams at any zoom (D8).
- [ ] Every op in the §6 table reaches the model and is undoable as one transaction (D9).
- [ ] Onset marker plus both edge handles are independently draggable (D10).
- [ ] Every new test was observed failing before its fix landed (D11, §10.2).
- [ ] Side-by-side JUCE-vs-Visage captures for the full §10.4 matrix attached to the PR.
- [ ] All 19 parity traps explicitly decided — reproduced or deliberately fixed — and
      recorded in the PR.
- [ ] `crotales.ats` full repaint ≤ 25 ms at 1600×900; playhead-only frames do not
      invalidate the display cache.
- [ ] The eight `UI-SPECTRUM-0*.md` docs rewritten to match what shipped, `Status:` lines
      corrected, traceability JSON updated.

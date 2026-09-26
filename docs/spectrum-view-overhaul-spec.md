# Spectrum View Visual Overhaul — Spec

**Target:** `SpectrogramPanel` (src/plugin/SpectrogramPanel.cpp/.h), all display modes.
**Goal:** Bring the spectrum viewer up to the visual quality of the Hydrosynth flow
visualization — smooth, continuous, glowing streamlines with depth, gradient color,
and a calm atmospheric background — replacing the current jittery, aliased,
noise-dominated rendering.

---

## 1. What's wrong today (per screenshot)

| View | Problem |
|---|---|
| **Iso partials** | Traces look like pencil scribbles: per-segment `g.drawLine()` calls (SpectrogramPanel.cpp:1122-1153) have no join continuity, so every frame-to-frame frequency jitter becomes a visible kink. Thin sub-pixel strokes shimmer/alias. Sparse dashes where amplitude gates segments out. |
| **Iso envelope waterfall** | Slices occlude via a thick near-black "clearance stroke" (cpp:1373-1376) which erases neighboring slices crudely, creating ragged dark halos. Too many overlapping slices → moiré. Stepped/boxy silhouettes at plateau edges. Flat single blue, no depth grading. |
| **2D partials + residual (Combined)** | Residual backdrop paints one `fillRect` per band-column (cpp:964-996) → hundreds of hard vertical stripes that bury the partial traces in visual noise. Grid, stripes, and traces all compete at similar brightness. |
| **Residual bands view** | Bar-column rendering again: a wall of vertical ticks at uniform brightness. Band ridgelines get lost. Band labels collide at the top of the gutter. |
| **Global** | Full-strength grid competes with data. Scanline overlay (cpp:897-899) adds noise, not CRT charm. No vertical color/brightness hierarchy: background, grid, residual, partials all live in the same green band. |

**What Hydrosynth does right (the target):** continuous smooth curves (no visible
vertices), stroke width and color that vary along the line with the data,
depth-graded alpha (far = dim, near = bright), a 2-3 layer soft glow around bright
lines, a dark blue-black vignette background, and a strict brightness hierarchy —
background ≪ grid ≪ context data ≪ primary data ≪ highlights.

---

## 2. Design language

### 2.1 Color system

Replace the flat green-on-black with a graded palette (new constants in
`UiConstants.h` or a `SpectrumTheme` struct):

```
background      #05080D  (near-black, blue-shifted — not pure green-black)
bgVignetteEdge  #030407
gridMinor       #0E2A33 @ 22% alpha
gridMajor       #14454F @ 35% alpha
axisFrame       #1E6E6E @ 55% alpha

// Amplitude gradient (Hydrosynth velocity-bar style): quiet → loud
ampGrad:  #1B4FA0 → #22A7C4 → #3FE8B0 → #B8FF7A
          (deep blue)  (cyan)   (mint)    (lime highlight)

selection       #FFE28A  (warm gold — the only warm hue on screen)
hover           #EAFFFB
playhead        #7AFFD4
residualFill    #123B4A → #1E7A6A gradient, always ≤ 40% partial brightness
```

Rules:
- **Amplitude maps to the gradient**, not just alpha. A loud partial is *lime-bright*;
  a quiet one is *deep blue and dim*. This is the single biggest "wow" change.
- Use `juce::ColourGradient`-sampled LUT (256-entry `std::array<juce::Colour, 256>`)
  built once — never interpolate colors per-segment in the paint loop.
- Selection gold and hover white stay as-is conceptually (they read well).

### 2.2 Line rendering — the core fix

**Never call `g.drawLine()` per segment for traces.** Build one `juce::Path` per
partial trace (per contiguous active run), then stroke it once:

1. **Smoothing:** convert the polyline to a Catmull-Rom → cubic Bézier path
   (`Path::cubicTo` with tangents from neighboring points, tension ≈ 0.5). Frame
   jitter becomes a gentle wave instead of kinks. Apply in *screen space* after
   projection so iso and 2D modes both benefit.
2. **Decimation first:** collapse runs of points closer than ~1.5 px
   (perpendicular-distance test, cheap RDP variant) before smoothing. Dense .ats
   files currently emit hundreds of sub-pixel segments per trace — this is both the
   shimmer and a perf win.
3. **Stroke:** `PathStrokeType(width, PathStrokeType::curved, PathStrokeType::rounded)`
   — curved joints + rounded caps. No more segment seams.
4. **Variable width via segmentation:** JUCE can't vary width along one path, so
   split each trace into path chunks wherever quantized width (0.25 px steps) or
   quantized gradient index (16 steps) changes. Chunks share endpoints so joins are
   invisible with rounded caps. Typical trace → 3-8 chunks, not 300 segments.

### 2.3 Glow

Three-pass stroke per path chunk, widest first:

```
pass 1: width × 4.0, gradient color @ 6-8% alpha   (halo)
pass 2: width × 2.0, gradient color @ 18% alpha    (bloom)
pass 3: width × 1.0, full color                    (core)
```

Only apply passes 1-2 when `ampVis > 0.35` — quiet lines get core-only so the view
doesn't fog up. (This is exactly the Hydrosynth streamline look: bright lines bloom,
dim ones stay hairline.) All of this renders into the existing `displayCache` image,
so the cost is paid only on invalidation, not per frame.

### 2.4 Background & grid

- Background: radial `ColourGradient` from `background` (center) to `bgVignetteEdge`
  (corners) instead of flat fill (cpp:835-836).
- Grid: two-level hierarchy — minor lines every division at `gridMinor`, major lines
  every 4th at `gridMajor`. In iso mode, fade grid lines toward the back of the
  floor plane (alpha × 0.5 at back edge) for depth.
- **Delete the scanline overlay** (cpp:897-899). Optionally replace with a single
  very subtle top-edge inner glow on the screen bezel.
- Keep the bezel/frame, but soften: 1 px `axisFrame` outline + 8% alpha inner
  highlight; drop the double rounded-rect outline.

---

## 3. Per-mode treatments

### 3.1 Isometric partials (`paintIsometricPartials`)

- Full §2.2/§2.3 pipeline: decimate → project via `positionForIsoCoords` → smooth →
  chunked gradient stroke with glow.
- **Depth cueing:** scale alpha and width by projected time-depth
  (`0.55 + 0.45 * timeNorm` — back traces dim/thin, front traces bright/thick).
  Additionally shift far traces slightly toward blue (LUT index bias −10%).
- **Draw order:** sort traces back-to-front (by mean timeNorm) so bright front
  lines naturally overlay dim back lines — free painter's-algorithm depth.
- Amp axis: raise `ampAxis` from `0.28 → 0.34` of plot height (cpp:3846-3847) so
  loud partials have more vertical drama; keep floor grid unchanged.
- Axis frame: draw the three vertical corner posts and floor edges with the
  `axisFrame` color; add small tick marks + dim labels on the front-left amp post
  (0 / −24 / −48 dB) and front-bottom freq edge.

### 3.2 Isometric envelope waterfall (`paintEnvelopeSurface`, `paintEnvelopeSurfaceRDC`)

The showcase view. Replace clearance-stroke occlusion with **filled ridgelines**
(joyplot / Unknown-Pleasures style):

- For each slice back-to-front, build a *closed* path: envelope top edge + drop to
  the slice's floor line + close. Fill it with a vertical gradient
  (`ampGrad`-at-depth @ 26% alpha at the crest → transparent at the floor), then
  stroke the top edge with the §2.3 glow stack. The fill *is* the occluder — no
  black halo, and overlapping translucent fills create genuine depth.
- Slice count: keep ≤ 40 in `paintEnvelopeSurface` but drop `kSlices` to **28-32**
  and raise `kSigma` slightly (6.5 → 7.5) — fewer, smoother, more separated ribs
  kills the moiré seen in the dense screenshot.
- Smooth each slice's top edge with the Catmull-Rom pass (§2.2.1) — this removes the
  stepped/boxy plateau edges.
- Depth grading: slice line color walks the LUT from deep blue (back) to mint
  (front); front slice gets a +1 px width bonus and full glow.
- A/B overlay mode: source A = cool ramp (blue→mint), source B = a magenta→amber
  ramp @ 70% alpha so the two surfaces are instantly distinguishable.

### 3.3 2D partials / Combined (`paintPartialTraces`, `paintResidualBackdrop`)

- Traces: §2.2/§2.3 pipeline in 2D (no depth cue; amp gradient only).
- **Residual backdrop redesign** — replace per-column `fillRect` stripes:
  - Background pass: per band, one closed path (energy ridge mirrored around band
    center, i.e. the existing ridge shape) filled with `residualFill` vertical
    gradient @ ≤ 12% alpha. Smooth the ridge with a 3-tap moving average before
    building the path.
  - Foreground (Residual mode): keep ridge stroke but move it to the glow stack at
    reduced intensity; delete the bar-column loop entirely (cpp:964-996).
  - Cap total residual luminance: residual must never exceed ~40% of the brightness
    of a mid-amplitude partial. It is context, not signal.

### 3.4 Residual bands view (`paintResidualBackdrop` foreground + gutter)

- Each band = filled ridgeline (as §3.3) + core stroke, alternating band background
  tint (2% alpha) instead of column ticks for time texture.
- Gutter labels: only draw a label when ≥ 14 px of vertical room; otherwise draw
  the label only for hovered/selected bands. Right-align Hz values, dim `Bxx` index.

### 3.5 Chrome shared by all modes

- Playhead: 1 px `playhead` core + 6 px gradient falloff wake behind it (leading
  edge crisp, trailing edge fades over ~24 px) — reads as motion, like Hydrosynth's
  particle flow direction.
- Selection lasso/slab: gold 1 px dashed stroke + 6% gold fill (largely as today,
  recolored).
- "ATS TRACE DISPLAY" header + readout box: keep, restyle with `textSecondary`
  equivalent from the spectrum theme; readout box gets the glow-stack border at low
  intensity.

---

## 4. Implementation plan

### Phase 1 — line quality core (biggest visible win)
1. Add `SpectrumTheme` + amplitude LUT to `UiConstants.h`.
2. Add a `TracePathBuilder` helper (new small header, e.g. `SpectrumRender.h`):
   decimation → Catmull-Rom smoothing → chunking by width/color. Pure functions
   over `std::vector<juce::Point<float>>`; unit-testable.
3. Rewire `paintPartialTraces` and `paintIsometricPartials` to use it, with glow
   stack and depth cueing. Delete per-segment drawLine loops.

### Phase 2 — envelope waterfall
4. Rewrite `paintEnvelopeSurface` / `paintEnvelopeSurfaceRDC` occlusion as filled
   ridgelines with gradient fills; retune slice count/sigma; wire A/B ramps.

### Phase 3 — residual & atmosphere
5. Replace residual bar-columns with filled ridges (2D + iso residual).
6. Background vignette, grid hierarchy, scanline removal, bezel/axis restyle,
   playhead wake, gutter label declutter.

### Phase 4 — polish pass
7. Screenshot review across all 7 modes at min/default/max window sizes and at
   deep zoom; tune alphas so the brightness hierarchy (§2.4) holds everywhere.

## 5. Performance constraints

- All heavy rendering stays inside the `displayCache` image (already in place,
  cpp:807-819); the glow stack multiplies stroke calls ×3 but only on cache
  invalidation. Budget: full repaint of a dense model ≤ 25 ms at 1600×900.
- Decimation (§2.2.2) should *reduce* total path vertices vs. today for dense
  models — measure with the crotales.ats session.
- No OpenGL dependency in this pass. Structure `TracePathBuilder` output so a
  future GL/juce::OpenGLContext backend could consume the same chunk lists.
- LUT lookups only in paint loops — no `Colour::interpolatedWith` per segment.

## 6. Acceptance criteria

- [ ] No visible polyline kinks or segment seams on any partial trace at default zoom.
- [ ] Loud vs. quiet partials distinguishable by *hue* (lime vs. blue), not only alpha.
- [ ] Envelope waterfall: no dark halos around slices, no moiré, no stepped plateau
      edges; front-to-back depth obvious at a glance.
- [ ] Combined view: partial traces immediately dominant; residual reads as a soft
      backdrop (≤ 40% relative luminance).
- [ ] Residual bands view: ridges legible, no bar-column stripe wall, no overlapping
      gutter labels.
- [ ] Selection (gold) and hover (white) still instantly visible in every mode.
- [ ] Repaint of crotales.ats dense model ≤ 25 ms; interaction (zoom/pan/hover)
      stays at current responsiveness.
- [ ] Side-by-side screenshots vs. current build for all 7 modes attached to the PR.

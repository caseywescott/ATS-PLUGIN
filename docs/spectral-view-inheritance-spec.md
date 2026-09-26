# Reusable Spectral Workbench View

## Visual inheritance and implementation specification

**Status:** Normative implementation handoff  
**Audience:** The engineer or coding agent building a spectral/data view in another
JUCE audio plugin  
**Reference implementation:** ATS Resynth `SpectrogramPanel`  
**Primary goal:** The new plugin must feel as polished, legible, dimensional, and
responsive as the ATS Resynth spectral view while displaying its own data.

This is not a loose mood board. It defines the visual system, rendering pipeline,
component architecture, interaction feedback, performance strategy, and acceptance
criteria that the new implementation must inherit.

The destination plugin may change:

- the product name;
- toolbar actions;
- view names;
- the meaning of the X, Y, and height axes;
- the data adapter and editing commands.

It must not casually change:

- the palettes;
- the brightness hierarchy;
- the trace colour ramp;
- smoothing, tapering, line widths, glows, or depth grading;
- the canvas frame, backdrop, grids, typography, legends, or state colours;
- the split between cached content and live overlays;
- the inherited control styling around the canvas.

If a destination feature has no equivalent data, omit that feature. Do not invent
decorative data merely to fill the view.

---

## 1. Normative references and precedence

The current reference behavior is implemented in:

1. `src/plugin/SpectrumRender.h` — canvas palette, amplitude ramp, trace smoothing,
   decimation, gradient paths, glow, and waterfall ridgelines.
2. `src/plugin/SpectrogramPanel.cpp/.h` — composition, modes, projection, backdrop,
   axes, legends, selection, navigation, editing feedback, caching, and playhead.
3. `src/plugin/UiConstants.h` — outer Ember/Porcelain palettes, typography, spacing,
   raised panels, inset wells, dividers, and JUCE colour IDs.
4. `src/plugin/PluginEditor.cpp` — inherited `LookAndFeel` recipes for buttons, toggles,
   segmented choices, dropdowns, sliders, popup menus, and application chrome.
5. `docs/mearcstapa-ui-style-reference.md` — the complete plugin-wide style system.

For the destination implementation, this document is the normative portable spec.
The reference code is supporting evidence and a source for extracting generic helpers.
ATS-specific labels, processor calls, and model structures are not part of the visual
contract.

### 1.1 Recommended reuse boundary

When both plugins are JUCE/C++ projects, extract or copy these as generic, product-neutral
modules rather than rewriting their math:

```text
SpectralViewTheme.h       fixed dark canvas tokens and amplitude LUT
SpectralTraceRender.h     smoothing, decimation, path, gradient, glow, ridgelines
SpectralViewModel.h       renderer-facing normalized snapshot types
SpectralWorkbenchView.h   canvas, chrome, viewport, selection, overlays
SpectralViewAdapter.*     destination-plugin data -> normalized snapshot
InheritedLookAndFeel.*    Ember/Porcelain outer controls
```

The renderer must not include or call the destination audio processor directly.
Processor-specific work belongs in the adapter/controller.

### 1.2 Whole-plugin inheritance

This document fully specifies the spectral workbench and the controls immediately
around it. If the destination request is to inherit **all styling from ATS Resynth**,
`docs/mearcstapa-ui-style-reference.md` is also mandatory for every other screen and
control in that plugin. Copy both documents into the destination repository and treat
them as one design contract:

- this document owns the spectral instrument canvas and its local chrome;
- the style reference owns the rest of the plugin's component library and layout.

The destination may add domain-specific composites, but they must be assembled from
those inherited tokens and drawing recipes. A screen outside the spectral view is not
exempt from the styling requirement.

---

## 2. Experience target

The view should read as a precision instrument embedded in a restrained professional
plugin:

- a deep blue-black optical well;
- calm, low-contrast spatial references;
- thin continuous data traces with no visible segment joins;
- loud or important data expressed through hue and luminance, not line thickness alone;
- clear front-to-back depth in isometric modes;
- a small amount of controlled bloom only around genuinely bright data;
- warm gold selection and near-white hover that remain visible over every data colour;
- motion expressed by one crisp playhead and a short fading wake;
- no shimmer, moiré, stripe walls, heavy fog, or competing decoration.

The required brightness order is:

```text
backdrop < dust < minor grid < major grid < context data
         < primary data < hover/selection < playhead
```

If a screenshot does not preserve that order, the implementation is not visually
equivalent even when every nominal colour is correct.

---

## 3. Two nested visual systems

The reference deliberately uses two related but distinct systems.

1. **Outer plugin shell:** Ember or Porcelain, selected by the user.
2. **Spectral instrument canvas:** always the fixed dark spectral palette below.

The canvas must remain dark in both outer themes. In Porcelain it should look like a
dark glass/instrument insert inside a light control surface, not like a recoloured
white chart. Toolbar controls outside the canvas follow the active outer theme.

### 3.1 Outer plugin palettes

Use a single `Palette` structure and swap every token atomically.

| Token | Ember | Porcelain |
|---|---:|---:|
| `window` | `#22242A` | `#F2F1EE` |
| `panel` | `#22242A` | `#FFFFFF` |
| `panelBorder` | `#2F343D` | `#D8D6D1` |
| `well` | `#1A1C21` | `#ECEAE5` |
| `wellBorder` | `#15161A` | `#D8D6D1` |
| `textPrimary` | `#E8E9EC` | `#23231F` |
| `textSecondary` | `#9AA0AC` | `#6E6C66` |
| `textMuted` | `#7A7F8A` | `#9C9A93` |
| `textFaint` | `#565C68` | `#B5B3AC` |
| `accent` | `#E8B96A` | `#1D6E63` |
| `accentDim` | `#8A6B3D` | `#6FA39B` |
| `shadowDark` | `#15161A` | transparent |
| `shadowLight` | `#2F343D` | transparent |

Ember uses soft opposing shadow/highlight pairs. Porcelain uses flat faces and a
one-pixel border; do not fake dark-theme shadows on the light theme.

### 3.2 Fixed spectral-canvas palette

Promote all values to named tokens. Do not scatter literal colours through paint code.

| Token | Value | Use |
|---|---:|---|
| `canvasBackground` | `#0A1220` | Canvas face and vignette centre |
| `canvasVignetteEdge` | `#050A12` | Radial falloff at the edges |
| `gridMinor` | `#0E2A33` at 22% | Fine planar/isometric grid |
| `gridMajor` | `#14454F` at 35% | Every fourth/fifth grid division |
| `axisFrame` | `#1E6E6E` at 55% | Canvas outline, floor edges, ticks |
| `axisText` | `#6FA3B0` | Titles, time labels, legend copy |
| `gutterText` | `#5F7F8A` | Frequency/band gutter labels |
| `selection` | `#FFE28A` | Selected traces and harmonic guides |
| `hover` | `#EAFFFB` | Hovered traces and hot labels |
| `playhead` | `#7AFFD4` | Playback marker and wake |
| `focusReadout` | `#BAFFEA` | Hover readout and focused guides |
| `footerReadout` | `#70FFD4` | Model counts and range footer |
| `emptyState` | `#5BFFBB` | Empty-data message |
| `editPreview` | `#AAFF99` | In-progress direct-draw preview |
| `rangeSelection` | `#5ED0FF` | Isometric editable range slab |
| `residualLow` | `#123B4A` | Optional low residual fill endpoint |
| `residualHigh` | `#1E7A6A` | Optional high residual fill endpoint |

Two dust colours are permitted only in the canvas backdrop:

- mint `#58C9A8`;
- blue `#4F7FD9`.

### 3.3 Amplitude/energy ramp

Normalized level `a` in `[0, 1]` maps through a 256-entry lookup table:

```text
0.00  #1B4FA0  deep blue
0.33  #22A7C4  cyan
0.67  #3FE8B0  mint
1.00  #B8FF7A  lime
```

Build the LUT once. Use a lookup during painting; do not repeatedly call colour
interpolation for every path segment.

The ramp is semantic:

- quiet/low energy = blue and dim;
- medium = cyan/mint;
- loud/high energy = lime and bright.

Do not substitute the outer plugin accent for this ramp. The ramp is what makes the
canvas analytically useful and visually rich.

For a second overlaid source only, use the warm comparison ramp:

```text
#8A2F8E -> #E8A04A
```

Draw that source first at 75% of normal alpha so the primary cool source remains
dominant.

### 3.4 Typography

Use logical JUCE points; let JUCE apply platform DPI scaling.

| Role | Face and size |
|---|---|
| Canvas title | UI sans, 12 |
| Empty state | UI sans, 15 |
| Footer and hover readout | UI sans, 11 |
| Legend heading | UI sans, 9, tracked with spaces if necessary |
| Legend endpoints and time ticks | UI sans, 8.5 |
| Frequency/band gutter | UI sans, 9 |
| Outer control labels | UI sans, 12–13 |
| Outer numeric readouts | monospaced, 10.5–11.5 |
| Outer section captions | UI sans, 10, uppercase, kerning factor 0.2 |
| Plugin title | UI sans bold, 17, kerning factor 0.18 |

All numerical control readouts outside the canvas must be monospaced. Canvas axis
labels may use the UI sans at the small sizes above to match the reference.

---

## 4. Inherited chrome recipes

The spectral view will look unfinished if the surrounding controls fall back to stock
JUCE styling. Apply the same `LookAndFeel` to the plugin and every child control.

### 4.1 Raised surface

Radius is 8 for buttons/generic controls and 14 for major panels.

**Ember**

For `i = 1..3`, with `offset = 2*i`:

1. dark shadow: `shadowDark` at `0.22/i`, translated `(+offset,+offset)`,
   expanded `i*0.5`, radius `radius+i`;
2. light lift: `shadowLight` at `0.16/i`, translated `(-offset,-offset)`,
   expanded `i*0.5`, radius `radius+i`;
3. face: diagonal gradient `panel.brighter(0.05)` top-left to
   `panel.darker(0.06)` bottom-right.

**Porcelain**

Fill `panel`; stroke `panelBorder` at one pixel.

### 4.2 Inset well

Fill `well`.

- Ember: one-pixel `shadowDark` outline at 85%; clip to the lower 40% and add a
  one-pixel `shadowLight` lower lip at 40%.
- Porcelain: one-pixel `wellBorder` outline.

Use this for dropdowns, slider tracks, toggle grooves, editor fields, and any
non-canvas recessed readout.

### 4.3 Buttons

- Radius: 8.
- Up/Ember: opposing 1.5 px shadows, face `window.brighter(0.05)`;
  hover uses `window.brighter(0.10)`.
- Up/Porcelain: `panel` face, `panelBorder` outline; hover changes the outline to
  `accent` at 60%.
- Down/active: draw an inset well and overlay `accent` at 15%.
- Label: UI sans 12.5, `textPrimary`.
- Disabled: 40–48% opacity.

### 4.4 Dropdowns

- Inset well, radius 8.
- Text: UI sans 13, left inset 8 px.
- Chevron: `accent`, 1.6 px, about 8 px wide, centred 13 px from the right.
- Popup face: `window.brighter(0.04)` in Ember, `panel` in Porcelain.
- Popup highlight: `accent` at 22% Ember / 14% Porcelain.

### 4.5 Compact sliders

- Row height: 42 px.
- Label row: 14 px.
- Track: 6 px high, radius 3, inset-well treatment.
- Fill: horizontal `accentDim -> accent` gradient.
- Ember fill halo: `accent` at 25%, expanded 2 px vertically.
- Thumb: 14 px ring; outer `accent`, inner `window`/`panel`.
- Value box: right-aligned monospaced 11.5, 52 px wide.
- Double-click restores the parameter default.

### 4.6 Toggle pills

- Maximum size: 42×21 px.
- Track: inset well, capsule radius.
- On wash: `accent` at 30% Ember / 18% Porcelain.
- Thumb radius: half-height minus 3 px.
- On thumb: `accent`; Ember adds a 4 px expanded halo at 28%.
- Off thumb: `textMuted`, raised to `textSecondary` on hover.

### 4.7 Segmented selectors

- Track: inset well; radius `min(height/2, 12)`.
- Active pill: inset 3 px, raised face, `accent` wash at 16% Ember / 10% Porcelain,
  `accent` outline at 80%, 1.2 px.
- Active text: `accent`.
- Inactive text: `textSecondary`.
- Hover wash: `accent` at 8%.

### 4.8 Spectral zoom control and scrollbars

The compact two-axis zoom control is a dark capsule even when embedded beside the
fixed-dark canvas:

| Element | Value |
|---|---:|
| Zoom capsule fill | `#1A1F1C` |
| Zoom capsule border | `#303833` |
| Centre divider | `#2A302C` |
| Axis icons/thumbs | `#B8C4BC` |
| Slider track | `#0F1310` |
| Radius | 8 |

Use bidirectional line-arrow icons, not text labels, for vertical and horizontal zoom.

Canvas scrollbars:

| Element | Value |
|---|---:|
| Background | `#0A0D0B` |
| Track | `#151A17` |
| Thumb | `#8A9A92` |
| Thickness | 13 px |

Disabled scrollbars remain visible at 35% opacity so the spatial affordance does not
jump when zoom changes.

---

## 5. Renderer-facing data contract

The destination plugin must adapt its data into normalized, immutable snapshots.
Painting must never lock the audio thread or inspect a mutable DSP graph.

```cpp
struct ViewPoint
{
    float timeNorm;     // 0..1 across the complete source
    float valueNorm;    // 0..1 across the complete vertical/frequency domain
    float levelNorm;    // normally 0..1; renderer may accept up to 2 for hot data
    bool active;
};

struct TraceSeries
{
    int id;
    std::vector<ViewPoint> points;
    double representativeValue;  // e.g. mean Hz; used only for readout/guides
};

struct ContextBand
{
    int id;
    double lowerValue;
    double upperValue;
    std::vector<float> energyNorm;
};

struct SpectralViewSnapshot
{
    uint64_t revision;
    int frameCount;
    double durationSeconds;
    double minimumValue;
    double maximumValue;
    std::vector<TraceSeries> traces;
    std::vector<ContextBand> bands;
    // Optional precomputed comparison/envelope/edit metadata.
};
```

### 5.1 Normalization rules

- Time is normalized over the complete source, not the visible viewport.
- For frequency-like values, normalize logarithmically:

  `valueNorm = log(value/min) / log(max/min)`.

- Level normalization must be stable while the user edits or changes display modes.
  Capture a display reference peak and preserve it until the source changes or the user
  explicitly resets the reference. Do not renormalize every repaint.
- Break inactive areas into separate contiguous runs. Never bridge an inactive gap with
  a line.
- Build or swap snapshots on the message thread. Pass snapshots by value, shared
  immutable ownership, or an equivalent lock-free handoff.

### 5.2 Reference density limits

The current quality/performance balance uses:

- at most 900 visible primary traces;
- at most 720 source points per trace before screen-space decimation;
- 220 columns for context/residual bands;
- 30 waterfall slices;
- 128 samples per waterfall ridge.

The destination may choose data-aware sampling, but it must retain screen-space
decimation and the visual result must pass the dense-data acceptance cases.

---

## 6. Component architecture

Use three responsibilities rather than one monolithic paint loop.

### 6.1 Controller/adapter

- Watches model/data revision.
- Builds normalized snapshots away from the audio callback.
- Owns destination-specific commands.
- Preserves the display reference across edits when appropriate.

### 6.2 Cached canvas component

Owns:

- background, dust, grid, axes;
- traces, context ridges, surfaces;
- legends, static readouts;
- selection, hover, and edit overlays that do not need audio-rate animation;
- viewport state.

Render to an ARGB image at the physical pixel scale:

```cpp
scale  = graphicsContext.getPhysicalPixelScaleFactor();
cacheW = round(logicalWidth  * scale);
cacheH = round(logicalHeight * scale);

Graphics cacheGraphics(cache);
cacheGraphics.addTransform(AffineTransform::scale(scale));
paintStaticCanvas(cacheGraphics);
```

Rebuild when:

- bounds or display scale change;
- data revision changes;
- source or mode changes;
- viewport changes;
- selection/hover/edit state changes;
- canvas tokens change.

### 6.3 Live playhead overlay

The playhead is a transparent child component above the cached canvas:

- timer: 60 Hz;
- mouse clicks: pass through;
- repaint only the union of old and new playhead bounds, expanded by 10 px;
- do not invalidate the full data cache for transport motion.

Smooth the displayed playhead toward the processor value with an exponential follower
of approximately 34 Hz. Snap immediately on loop/restart, backwards jumps greater than
0.02 normalized units, or absolute discontinuities greater than 0.25.

### 6.4 Data refresh cadence

Poll data/model revision at 10 Hz or use a message-thread change signal. Skip snapshot
rebuilds while a background worker owns incomplete data. Never repaint at 60 Hz merely
because a timer exists.

---

## 7. Canvas geometry and layer order

### 7.1 Geometry

- Component outer inset: 5 px.
- Toolbar row: 26 px.
- Optional second tool row: 24 px, for a total header height of 50 px.
- Canvas corner radius: 5 px.
- Canvas content inset: 14 px horizontal, 12 px vertical.
- Internal title reserve: 18 px at top.
- Footer reserve: 18 px plus the 13 px horizontal scrollbar.
- Optional axis/band gutter: 34 px at left.
- Plot right reserve: 10 px plus the 13 px vertical scrollbar.

When a secondary toolbar is visible, the canvas begins below the full 50 px header.
The destination must not allow toolbar controls to overlap the instrument canvas.

Recommended minimum canvas plot size is 560×280 logical pixels. If space falls below
that, hide nonessential legend copy before shrinking primary data or controls into
illegibility.

### 7.2 Cached paint order

Paint exactly in this conceptual order:

1. canvas face;
2. border and top inner highlight;
3. plot vignette;
4. deterministic dust;
5. grid and axes;
6. low-priority context data;
7. primary traces/ridges/surfaces;
8. amplitude/energy legend;
9. optional analysis overlays;
10. selection, hover, edit previews, and readouts;
11. gutter and footer labels.

The live playhead child paints after the cached layers.

---

## 8. Backdrop, frame, grid, and axes

### 8.1 Canvas frame

1. Fill the rounded canvas with `canvasBackground`, radius 5.
2. Stroke at one pixel with `axisFrame`.
3. Add a white 8% top-edge inner highlight, inset 1.5 px.

Do not draw a thick bezel or multiple bright outlines.

### 8.2 Vignette

Inside the clipped plot, draw a radial gradient:

- centre: `canvasBackground`;
- edge/corner: `canvasVignetteEdge`;
- centre point: plot centre;
- radial endpoint: plot top-left.

### 8.3 Deterministic dust

Dust provides depth but must never animate.

```text
seed             0x5eed
dot count        floor(plotWidth * plotHeight / 2600)
radius           random 0.4..1.3 px
alpha            random 0.02..0.09
colour mix       30% mint dust, 70% blue dust
```

The fixed seed is mandatory. Re-randomized dust reads as shimmer on cache rebuilds.

### 8.4 Planar grid

- 16 horizontal-domain divisions; draw 17 vertical lines including bounds.
- 12 vertical-domain divisions; draw 13 horizontal lines including bounds.
- Every fourth line uses `gridMajor`; the rest use `gridMinor`.
- Keep grid strokes at one device pixel or their JUCE hairline equivalent.

Time labels appear on major X divisions. Use 2 decimals when the visible duration is
under 2 seconds, 1 decimal under 20 seconds, otherwise no decimals.

### 8.5 Isometric projection

For visible normalized `time`, `value`, and `level`, use:

```text
origin   = (plot.x + 0.075*W, plot.bottom - 0.085*H)
valueAxis= (0.660*W, -0.055*H)
timeAxis = (0.255*W, -0.440*H)
levelAxis= (0.000*W, -0.300*H)

screen = origin
       + valueAxis * value
       + timeAxis  * time
       + levelAxis * clamp(level, 0, 1.25)
```

The proportions are load-bearing. They leave room for peaks, labels, and the front
floor edge while giving the time axis enough depth to read immediately.

Isometric floor grid:

- 8 value divisions;
- 10 time divisions;
- major value line at every fourth division;
- major time line at every fifth division;
- later/nearer time grid alpha multiplier: `0.55 + 0.45*time`.

Draw the four floor edges and three amplitude posts. Use a subtle two-pass frame stroke:
a wide copy at 45% of the line colour's existing alpha, then the core.

Put tick labels outside the floor plane. Do not put text or dense markers inside the
ground square.

---

## 9. Primary trace rendering

This pipeline is the main quality requirement. A polyline made from individual
`drawLine` calls is not acceptable.

### 9.1 Build contiguous runs

For each series:

1. project active points into screen space;
2. finish the current run at every inactive point;
3. discard nonselected/nonhovered runs with 4 or fewer points when peak level is below
   0.30;
4. process every surviving run independently.

### 9.2 Screen-space smoothing

Apply the binomial filter to position and level, pinning the endpoints:

```text
smoothed[i] = (previous + 2*current + next) / 4
```

- planar traces: 2 passes;
- isometric traces: 3 passes.

Smooth after projection. This is important: visual jitter is a screen-space problem,
and the same helper must work in both projections.

### 9.3 Decimation

Remove a middle vertex unless one of these is true:

- perpendicular deviation from the local segment is greater than 0.75 px;
- distance from the last kept point is greater than 24 px;
- level difference from the last kept point is greater than 0.08.

Always keep the first and last point.

### 9.4 Smooth path

Build one cubic path for the run using Catmull-Rom-style tangents:

```text
tangent[i] = (nextPoint - previousPoint) / 6

cubicTo(
    point[i]   + tangent[i],
    point[i+1] - tangent[i+1],
    point[i+1]
)
```

Use curved joins and rounded caps for every stroke.

### 9.5 Endpoint taper

Measure cumulative screen-space arc length. Fade over 7 px at both ends:

```text
fadeIn  = min(1, distanceFromStart / 7)
fadeOut = min(1, distanceFromEnd   / 7)
fade    = 0.18 + 0.82 * fadeIn * fadeOut
```

This makes births and deaths dissolve rather than terminate as bright fragments.

### 9.6 Gradient stops

Project vertices onto the start-to-end gradient axis. Keep a new stop only when it is
at least 0.01 farther along and one of these changes materially:

- level by 0.02;
- depth shade by 0.05;
- endpoint fade by 0.08.

For runs shorter than about 2 px, use one averaged flat colour.

### 9.7 Normal trace stroke

- Core width: 1.0 px, constant along the run.
- Planar alpha:

  `(0.28 + 0.62*level) * endpointFade`.

- Isometric alpha:

  `(0.28 + 0.58*level) * depthShade * endpointFade`.

- Colour: amplitude LUT at `clamp(level, 0, 1)`.
- Glow: only when the run peak exceeds 0.60.
- Glow width: `coreWidth * 2.4`.
- Glow alpha: 12% of the already-computed core alpha.

Keep width constant. Level and depth belong in colour/luminance; excessive
level-dependent thickness makes dense views muddy.

### 9.8 Hover and selection

Draw unselected traces first and selected traces last.

**Hover**

- colour: `hover`;
- halo width: 4.0 px planar / 4.5 px isometric;
- halo alpha: 18% planar / 22% isometric;
- core: 1.7 px;
- core alpha: approximately `0.72 + 0.20*peak` planar.

**Selection**

- colour: `selection`;
- halo width: 6.0 px planar / 6.4 px isometric;
- halo alpha: 18% planar / 22% isometric;
- core: 1.7 px;
- draw after every unselected trace.

Selection is always warm gold. Do not recolour it to match the destination brand.

---

## 10. Required display treatments

The destination may expose only modes supported by its data. Every exposed mode must
use the treatment below.

### 10.1 Planar traces

- X is normalized time/domain.
- Y is normalized value; invert so larger values are higher.
- Draw selected-trace harmonic/reference guides, if meaningful, as gold dashed lines
  at 14% and labels at 38%.
- Keep traces dominant over any context backdrop.
- Use a 34 px left gutter for logarithmic value/frequency labels.
- Generate log labels from the 1–2–5 series and suppress labels closer than 13 px.

### 10.2 Planar context/residual bands

Context data must be soft ridges, never a wall of bar columns.

For each band:

1. smooth energy with `[1,2,1]/4`;
2. map energy to a ridge reaching 92% of the band row height;
3. close the path to the band baseline;
4. fill from amplitude colour at the crest to transparent at the baseline;
5. stroke the smoothed ridge through the shared gradient-trace helper.

**Context behind primary traces**

- fill alpha: 0.06;
- stroke alpha: `0.10 + 0.16*energy`;
- no glow.

**Foreground context-only mode**

- fill alpha: 0.16;
- stroke width: 1.1 px;
- stroke alpha: `0.34 + 0.56*energy`;
- baseline is extremely faint.

When band rows are under 12 px high, omit labels unless hovered or selected. Use two
label lines only when the row is at least 22 px high.

### 10.3 Isometric traces

- Sort traces by mean active time and paint back-to-front.
- Depth shade:

  `0.55 + 0.45*relativeVisibleTime`.

- Use the same amplitude LUT as planar mode.
- Add a faint ground projection at level 0 to anchor each trace:

  alpha `(0.05 + 0.10*peakLevel) * depthShade`.

- Omit the shadow for short quiet fragments under the same 4-point/0.30 rule.

### 10.4 Isometric context/residual ridges

For each value/frequency band:

- use the band centre as its floor position;
- smooth energy with `[1,2,1]/4`;
- scale ridge height to 1.15 before the global level clamp;
- build a closed fill from crest to baseline;
- fade the fill to zero before it reaches the baseline so no straight floor stripe
  appears;
- normal fill alpha: 0.12;
- selected fill alpha: 0.26;
- hovered fill alpha: 0.20;
- normal crest width: 1.2 px;
- suppress silent bands below 0.02 unless hot;
- show the baseline only as a hover/selection affordance.

### 10.5 Isometric envelope/waterfall

The showcase treatment is a set of clean joyplot-style ridgelines:

- 30 time slices;
- 128 value/frequency samples per slice;
- paint back-to-front;
- depth alpha: `(0.40 + 0.60*depth) * sourceAlpha`;
- ridge core width: `0.9 + 0.4*depth`;
- halo width: 2.2 px;
- halo alpha: 20% of ridge alpha;
- body tint: 10% of depth alpha at the crest, fading to transparent by 65% of the
  distance to the floor.

For a Gaussian/kernel envelope:

- sigma: 7.5 bins;
- use three linear-time box blurs to approximate the Gaussian;
- use a small zero prior (`0.02`) in the denominator;
- apply a 14-bin cosine taper at both frequency/value edges.

The destination may replace the envelope estimator with its own analysis, but it must
retain the slice count, taper, fill, stroke, depth, and draw-order treatment.

Never use an opaque black clearance stroke between slices. It produces ragged halos
and destroys the sense of translucent depth.

---

## 11. Legend, readouts, and empty state

### 11.1 Level legend

Place a 132×38 px card 8 px from the plot top and 8 px from the right.

- Face: `canvasBackground` at 78%.
- Border: `axisFrame` at 45%.
- Radius: 4 px.
- Heading: `A M P`, `E N E R G Y`, or the destination's equivalent, 9 px.
- Ramp: 6 px high, radius 3, with intermediate stops at 0.25, 0.50, 0.75.
- Endpoint labels: 8.5 px.

For a 72 dB level mapping, label the low endpoint `-72 dB` and the high endpoint `0`.
For unitless energy, use `lo` and `hi`.

If the card would collide with other content or the plot is too narrow, hide the card
as a unit. Do not compress its typography.

### 11.2 Header/footer

- Canvas title: 14 px left inset, 6 px top inset, `axisText`, 12 px.
- Left footer: count or data summary, 14 px left inset, `footerReadout`, 11 px.
- Right footer: visible value range, 14 px right inset, same styling.
- Product-specific wording may change; placement and hierarchy may not.

### 11.3 Hover readout

Place at `plot.x + 8`, `plot.y + 7`, in `focusReadout`, 11 px.
Show the shortest useful identifier/value string. Avoid large tooltip cards floating
over the traces.

### 11.4 Empty state

When no snapshot is available:

- retain frame, vignette, dust, grid, title, and scrollbars;
- centre one short message in `emptyState`, 15 px;
- do not replace the view with a blank outer-theme panel.

---

## 12. Playhead and motion

### 12.1 Planar playhead

- Wake: 24 px behind the leading edge, `playhead` from 0% to 14% alpha.
- Outer line: 2.6 px at 45%.
- Core line: 1.1 px at 100%.
- Top head: 7 px wide, 8.5 px high, at 88%.

The leading edge is crisp. The wake trails behind it; do not centre a symmetric glow
around the line.

### 12.2 Isometric playhead

- Draw a line across the floor at the current time.
- Wake depth: 3% of the visible normalized time range.
- Wake alpha: 0% to 7%.
- Outer floor line: 3.4 px at 10%.
- Core floor line: 1.1 px at 42%.
- Front endpoint dot: 4 px at 70%.

Do not raise a large vertical wall through the isometric data; the floor marker is
deliberately subtle.

---

## 13. Selection and direct-edit feedback

Interaction feedback must use semantic colours, never the outer accent.

### 13.1 Lasso and range selections

- Generic lasso: cool mint outline, one pixel, and a very low-alpha cyan fill.
- Planar data-range selection: gold outline about 1.4 px and low-alpha cyan fill.
- Isometric range: `rangeSelection`; 15% floor fill, 50% floor outline, strong front
  walls, faint back walls.

Selection overlays must describe geometry without hiding the selected data.

### 13.2 Drawing/edit preview

- Direct-draw preview: `editPreview`, 2 px, 90%.
- Committed data returns to the standard amplitude ramp on the next snapshot.
- Edit handles become near-white or gold only while hot.

### 13.3 Modifier behavior

Use platform conventions:

- click: select one;
- Shift or Command/Ctrl click: add/toggle selection;
- drag empty area: lasso;
- Escape: clear selection;
- Delete/Backspace: invoke the destination's delete/mute command;
- Alt-drag or middle-drag: pan;
- `0` or Home: fit the full source;
- Command/Ctrl-click during playback: seek, if seeking is supported.

If the destination view is read-only, omit edit commands but retain hover, selection,
navigation, and playhead feedback.

---

## 14. Zooming and navigation

Viewport state:

```text
timeZoom   1..32
valueZoom  1..32
timeCenter 0..1
valueCenter 0..1
```

Use exponential zoom mapping in the compact zoom sliders:

```text
zoom = minZoom * pow(maxZoom/minZoom, sliderValue)
```

### 14.1 Input

- Pinch: zoom both axes around the cursor.
- Command/Ctrl-wheel: zoom around the cursor.
- Shift modifier while zooming: time/horizontal axis only.
- Alt modifier while zooming: value/vertical axis only.
- Unmodified two-axis wheel/trackpad: pan.
- Shift-wheel: horizontal-pan fallback.

In isometric mode, screen X primarily maps to the value/frequency axis and screen Y
primarily maps to time. Remap scrollbars and wheel panning accordingly.

### 14.2 Anchor preservation

When zooming, preserve the normalized data point beneath the pointer. Recompute the
viewport centre after applying the new zoom rather than zooming around the component
centre.

Clamp the centre so the visible range never extends beyond `[0,1]`.

---

## 15. Performance and rendering constraints

These are part of visual quality. Dropped frames and blurred HiDPI output fail parity.

- No OpenGL dependency is required.
- No per-frame blur.
- No per-segment `drawLine` loop for primary data.
- No colour interpolation inside an inner point loop when a LUT can be used.
- No full canvas rebuild for playhead movement.
- No allocation or model traversal on the audio thread.
- Clip to the plot before drawing data.
- Cull paths whose bounds do not intersect the plot.
- Render the static canvas at the physical pixel scale.
- Keep dust deterministic.
- Use cached snapshots and stable display normalization.

Targets on a typical release build at 1600×900:

- dense static cache rebuild: at most 25 ms;
- live playhead: visually stable at 60 Hz;
- hover/zoom/pan: no visible stall longer than one frame under normal data;
- no increase in audio callback work when the view is open.

---

## 16. Destination-plugin implementation plan

### Phase 1 — style foundation

1. Implement the Ember/Porcelain `Palette`.
2. Implement raised surface, inset well, button, dropdown, slider, toggle, segmented
   selector, tooltip, and popup recipes.
3. Install one plugin-wide `LookAndFeel`.
4. Implement the fixed spectral-canvas token structure and amplitude LUT.

### Phase 2 — generic rendering core

5. Implement normalized snapshot types.
6. Port screen-space smoothing, decimation, Catmull-Rom path generation, endpoint
   taper, gradient-stop pruning, constant-width gradient strokes, and ridgeline draw.
7. Add focused unit tests for those pure helpers.

### Phase 3 — canvas

8. Implement frame, vignette, deterministic dust, planar grid, axes, gutters, legend,
   and empty state.
9. Implement planar traces and any context bands.
10. Implement isometric projection, floor grid, trace depth, and waterfall if the
    product exposes an isometric mode.

### Phase 4 — interaction and animation

11. Add viewport model, scrollbars, cursor-anchored zoom, and pan.
12. Add hover, selection, lasso, and destination-specific editing.
13. Add the separate 60 Hz playhead overlay.

### Phase 5 — visual QA and tuning

14. Capture the required screenshot matrix.
15. Fix brightness hierarchy and collisions before tuning decorative details.
16. Profile cache rebuilds using the densest realistic model.

---

## 17. Required deliverables

The implementation PR for the destination plugin must include:

- generic spectral theme and rendering-helper files;
- the destination data adapter;
- the view component and any overlay component;
- pure helper tests;
- screenshot evidence at 1× and 2× backing scale;
- a short performance note with dense-cache rebuild timing;
- a list of intentionally omitted reference modes and why their data does not apply.

Do not deliver the work as one processor-coupled `paint()` function.

---

## 18. Visual QA matrix

Capture every applicable row at the plugin's minimum, default, and maximum supported
window size.

| Case | Required check |
|---|---|
| No data | Instrument frame and hierarchy remain intentional |
| Sparse quiet data | Blue traces remain visible without becoming thick |
| Dense mixed-level data | No shimmer, confetti, fog, or lost selection |
| One selected trace | Gold selection dominates without hiding its shape |
| Hover over dense data | Near-white hover is unambiguous |
| Full source | Labels, legend, footer, and scrollbars do not collide |
| Deep zoom | Paths remain smooth; endpoint fades and axes remain stable |
| Active playback | Crisp playhead, correct wake direction, no full-view flicker |
| Isometric view | Front/back depth is obvious and floor remains readable |
| Waterfall | No black clearance halos, moiré, or opaque wedge |
| Ember outer theme | Canvas feels integrated with dark shell |
| Porcelain outer theme | Canvas reads as a dark instrument insert |
| 2×/Retina | Text and one-pixel strokes are sharp |

Compare screenshots at 100% scale. Do not judge only from scaled-down images.

---

## 19. Acceptance criteria

The destination view is complete only when all applicable items pass.

### Visual

- [ ] Fixed spectral palette and exact amplitude ramp are used.
- [ ] Outer controls inherit Ember and Porcelain styling with no stock JUCE widgets.
- [ ] Canvas remains fixed-dark in both outer themes.
- [ ] Brightness hierarchy matches Section 2.
- [ ] Primary traces are smooth cubic paths with rounded caps and no segment seams.
- [ ] Loudness/energy is readable from hue and luminance, not thickness alone.
- [ ] Quiet context data never competes with primary data.
- [ ] Selection is gold; hover is near-white; playhead is mint.
- [ ] Isometric depth is obvious from projection, paint order, and alpha.
- [ ] Waterfall slices are translucent ridgelines with no black clearance stroke.
- [ ] Axes, gutters, legend, footer, and title remain legible without collisions.
- [ ] HiDPI output is sharp.

### Interaction

- [ ] Hover and selection remain responsive on dense data.
- [ ] Cursor-anchored zoom preserves the data point under the pointer.
- [ ] Pan and scrollbar mappings match the active projection.
- [ ] Full-view reset works.
- [ ] Playhead movement does not invalidate the static cache.
- [ ] Read-only destinations still provide hover, selection, zoom, and pan.

### Performance and safety

- [ ] Painting never reads mutable DSP state directly.
- [ ] Audio-thread work is unchanged by opening the view.
- [ ] Static content is cached at physical pixel scale.
- [ ] Dense cache rebuild meets the 25 ms target or includes measured justification.
- [ ] No animated random noise, per-frame blur, or repeated colour interpolation in
      hot inner point loops where a LUT can be used.

---

## 20. Explicit noncompliance list

Any of the following is a visual regression and must be rejected:

- using pure black as the plot background;
- recolouring the amplitude ramp to the destination plugin's brand accent;
- drawing every trace segment separately;
- square line caps or sharp joins on primary traces;
- making level primarily control line thickness;
- applying a large glow to every quiet trace;
- redrawing the full canvas at the playhead frame rate;
- rendering the cache only at logical resolution on a HiDPI display;
- re-randomizing dust on repaint;
- replacing context ridges with hundreds of filled bar columns;
- using an opaque black stroke to separate waterfall slices;
- using the same luminance for grid, context, and primary data;
- allowing toolbar chrome to overlap or visually merge with the plot;
- allowing stock JUCE controls to appear beside inherited controls;
- implementing only Ember and leaving Porcelain as a later styling pass.

---

## 21. Short handoff brief

Use this paragraph when assigning the work:

> Build a reusable spectral workbench view for the destination JUCE plugin using
> `docs/spectral-view-inheritance-spec.md` as the normative contract. Adapt the
> destination data into immutable normalized snapshots, but inherit the complete
> ATS Resynth visual language: Ember/Porcelain outer chrome, fixed dark spectral
> canvas, exact amplitude LUT, smooth screen-space cubic traces, restrained bloom,
> isometric depth, ridgeline surfaces, semantic hover/selection/playhead colours,
> HiDPI static caching, and a separate 60 Hz playhead overlay. Do not substitute
> stock JUCE styling or simplify the trace pipeline to per-segment lines. Complete
> the screenshot and performance acceptance matrix before handoff.

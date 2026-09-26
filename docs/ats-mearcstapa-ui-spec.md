# ATS Resynth — MEARCSTAPA UI Implementation Spec

> **Purpose.** A self-contained implementation spec that applies the MEARCSTAPA
> ("PolyFilter") neumorphic UI system — both themes, every drawing recipe, typography,
> and runtime discipline — to **ATS Resynth**, a spectral model editor built on the ATS
> (Analysis–Transformation–Synthesis) representation. The plugin analyzes WAV files into
> sinusoidal partial tracks + a noise residual, lets the user edit and resynthesize the
> model, morph between two analyzed sources (A/B), apply transient smearing ("Smearnov"),
> shape timbre, and script partial edits.
>
> **Relationship to the reference.** Sections marked **[carried]** are copied from
> `mearcstapa-ui-style-reference.md` unchanged and are load-bearing for visual parity —
> do not reinterpret them. Sections marked **[derived]** are new for ATS Resynth,
> designed with the reference's column-engine rules and reusable composites.
>
> **Source of truth for parameters:** `src/plugin/PluginProcessor.cpp`
> `createParameterLayout()` (`:1856–2195`) and `src/plugin/PluginProcessor.h`
> (`ats_param::` IDs, lane tables). Every parameter listed below exists in the APVTS
> today; nothing is hypothetical.
>
> **Colour convention.** JUCE `0xAARRGGBB`; hexes below are opaque `#RRGGBB`, alphas
> given separately.

---

## 1. Design philosophy **[carried]**

- **Neumorphic, low-chroma.** Depth from soft shadow/highlight pairs (dark theme) or a
  flat fill + 1px border (light theme). Surfaces read as **raised** (panels, knob bodies,
  buttons) or **inset/sunken** (tracks, wells, combos, toggle grooves).
- **One accent, disciplined.** Section identity comes from grouping, labels, and engraved
  dividers — not per-section colour. The single accent (amber dark / teal light) is
  reserved for active state, fills, live traces, and "lit" affordances. Partial-track
  traces, the morph timeline curve, and the spectrogram all draw in accent — never
  introduce a second hue.
- **Two themes, always both.** Every element is styled for both **Ember** (dark) and
  **Porcelain** (light). No element may be single-theme. Theme is a single int switch.
- **Tabular numerics.** All numeric readouts monospaced; number and unit colour-split
  (number primary, unit muted).
- **Cheap animation.** No per-frame blur. Glow = pre-rendered sprite or low-alpha wider
  copy. Idle beds cached to an `Image` and blitted; only changing elements redraw.

---

## 2. Theme palettes — exact values, both themes **[carried]**

Selected by a `uiTheme` int: **0 = Porcelain** (light), **1 = Ember** (dark). Both are a
single `Palette` struct. Ember is the primary look. **Porcelain is neumorphic-light with a
teal accent, not amber.** Every token is required for both themes.

| Token (struct field) | Role | **Ember** (dark, `dark=true`) | **Porcelain** (light, `dark=false`) |
|---|---|---|---|
| `window` | App background / behind-panel | `#22242A` | `#F2F1EE` |
| `panel` | Raised panel / knob face | `#22242A` | `#FFFFFF` |
| `panelBorder` | Panel outline (light theme) | `#2F343D` | `#D8D6D1` |
| `well` | Inset/sunken surface (tracks, combos, wells) | `#1A1C21` | `#ECEAE5` |
| `wellBorder` | Inset outline (light theme) | `#15161A` | `#D8D6D1` |
| `textPrimary` | Values, active labels, pointer | `#E8E9EC` | `#23231F` |
| `textSecondary` | Control labels | `#9AA0AC` | `#6E6C66` |
| `textMuted` | Section labels, units | `#7A7F8A` | `#9C9A93` |
| `textFaint` | Axis labels, unlit dots, hairline rules | `#565C68` | `#B5B3AC` |
| `accent` | **Amber / Teal** — active state, traces, fills | `#E8B96A` | `#1D6E63` |
| `accentDim` | Gradient low end, dim tint | `#8A6B3D` | `#6FA39B` |
| `shadowDark` | Neumorphic dark shadow | `#15161A` | `transparent` |
| `shadowLight` | Neumorphic light shadow | `#2F343D` | `transparent` |
| `keyFaceW` | Natural-key face | `#3A3D46` | `#FAF9F6` |
| `keyEdgeW` | Natural-key top-edge highlight | `#44474F` | `#FFFFFF` |
| `keyFaceB` | Sharp-key face | `#191A1F` | `#33332F` |
| `keyEdgeB` | Sharp-key top-edge highlight | `#2B2D34` | `#45453E` |

Notes:
- **Porcelain shadows are transparent** — the light theme does *not* use the feathered
  shadow stack; depth is a flat fill + a 1px `panelBorder`/`wellBorder` stroke. Only Ember
  draws the multi-layer soft shadows. Reproduce **both** paths.
- In Ember, `window == panel == #22242A`; raised panels read as "raised" purely via the
  shadow stack + a subtle diagonal gradient, not a different base fill.

### 2.1 Extra JUCE colour-IDs the LookAndFeel sets **[carried]**

Applied in the `ThemedLookAndFeel` constructor so stock JUCE components inherit the theme:

- `ResizableWindow::background` = `window`
- `Label::text`, `Slider::textBoxText`, `ComboBox::text`, `PopupMenu::text`,
  `TextButton::textColourOff`, `ToggleButton::text`, `AlertWindow::text`,
  `TextEditor::text` = `textPrimary`
- `Slider::textBoxOutline`, `Slider::textBoxBackground`, `TooltipWindow::background`
  = transparent
- `ComboBox::background` = `well` (dark) / `panel` (light); `ComboBox::outline` =
  `wellBorder`/`panelBorder`; `ComboBox::arrow` = `accent`
- `PopupMenu::background` = `window.brighter(0.04)` (dark) / `panel`;
  `PopupMenu::highlightedBackground` = `accent`@0.22 (dark) / @0.14 (light)
- `TextButton::button` = `window.brighter(0.05)` (dark) / `panel`;
  `TextButton::buttonOn` = `accent`; `TextButton::textColourOn` = `window`/`panel`
- `TextEditor::background` = `well` (dark) / `window`; `TextEditor::focusedOutline` =
  `accent`; `TextEditor::highlight` = `accent`@0.3; `CaretComponent::caret` = `accent`
- `BubbleComponent::background` = `window.brighter(0.05)` (dark) / `panel`

ATS-specific consumers of these IDs: the Math page script editor and the Partials page
amp/freq editors are stock `TextEditor`s and must inherit the `TextEditor` set above
(well background, accent focused outline, accent caret) with no local overrides.

---

## 3. Neumorphic drawing recipes **[carried]**

Reproduce each per-theme.

### 3.1 Raised panel — `drawRaisedPanel`
- **Ember (dark):** loop `i = 1..3`, `offset = 2*i`:
  - Dark drop: `shadowDark` @ `0.22/i`, rounded-rect at
    `r.translated(+offset,+offset).expanded(i*0.5)`, radius `radius+i`.
  - Light lift: `shadowLight` @ `0.16/i`, rounded-rect at
    `r.translated(-offset,-offset).expanded(i*0.5)`, radius `radius+i`.
  - Face fill: diagonal `ColourGradient` `panel.brighter(0.05)` (top-left) →
    `panel.darker(0.06)` (bottom-right), radius `radius`.
- **Porcelain (light):** fill `panel`; stroke `panelBorder` 1px at `r.reduced(0.5)`.

### 3.2 Inset well — `drawWell`
- Fill `well`.
- **Ember:** stroke `shadowDark`@0.85 1px at `r.reduced(0.5)`; then, clipped to the
  **lower 40%** of the rect, stroke `shadowLight`@0.4 1px at `r.expanded(0.5)` (a faint
  bottom light lip that sells the "sunken" read).
- **Porcelain:** stroke `wellBorder` 1px.

### 3.3 Accent glow — how "lit" reads
No single glow primitive. The pattern is **draw the accent shape, then stamp a low-alpha
wider copy behind/around it** — Ember only (Porcelain relies on the border stroke instead):
- Slider fill halo: `accent`@0.25 rounded-rect expanded `(0, +2)` behind the fill.
- Rotary value-arc halo: `accent`@0.26 stroked 6.5px under the 2.4px solid arc.
- Toggle-on thumb halo: `accent`@0.28 ellipse expanded 4 under the thumb.
- Dot halo (activity dots, list dots): `accent`@0.3 ellipse expanded 3–3.5.
- Keyboard press glow: a **pre-rendered 64px radial sprite** `accent`@0.85→0 centre-out,
  stamped into the lower 60% of a pressed key at run-alpha ≤0.45 — **never per-frame blur.**
- Morph-timeline / spectrogram trace halo: 4.5px `accent`@0.2–0.22 under the solid trace.

### 3.4 Button background — `drawButtonBackground`
- Radius **8**, drawn at `r.reduced(0.5)`.
- **Down/active:** `drawWell` + `accent`@0.15 fill (presses into a sunken accent-tinted well).
- **Ember, up:** shadow pair (`shadowDark`@0.4 at `+1.5,+1.5`; `shadowLight`@0.35 at
  `-1.5,-1.5`) + face `window.brighter(0.1)` if hovered else `window.brighter(0.05)`.
- **Porcelain, up:** fill `window` (hover) / `panel`; stroke `accent`@0.6 (hover) else
  `panelBorder`, 1px.

### 3.5 Tooltip — `drawTooltip`
Rounded rect radius 8 at `reduced(1)`: fill `window.brighter(0.06)` (dark) / `panel`,
stroke `panelBorder` 1px. Text via `AttributedString`, `uiFont(12.5)`, `textPrimary`,
left-justified, laid out at `width-16`, drawn at `reduced(8,4)`. Tooltip window background
colour-id transparent so only the rounded card shows. ATS Resynth already sets per-control
tooltips (`AtsUiHints.h`); keep them and render all through this recipe.

---

## 4. Corner-radius scale **[carried, with ATS additions]**

| Element | Radius (px) |
|---|---|
| Page column panels, keyboard bed backing | `14` / `12` |
| Spectrogram well, output-spectrum well | `10` |
| Combo box, text button, source card, generic panel, collapsed keyboard, script-editor well | `8` |
| Morph-timeline well | `8` |
| Selected variation slot, selected list panel | `9` |
| Toggle pill, segmented well | capsule = `height*0.5` (pill ≈10.5; segmented clamps ≤`12`); segmented active pill = `radius-3` |
| Cheek buttons (keyboard, snapshot strip, source-card actions) | `5` |
| Slider track (linear) | `3` |
| Meter track | `4` (fill drawn at `3`) |
| Keyboard keys | `3.0` (sharps) / `3.5` (naturals) |

---

## 5. Spacing rhythm **[carried, with ATS additions]**

- **Outer window margin:** `reduced(16)`.
- **Inter-column gap:** `16`. Each column inner padding `reduced(14)`, plus a `14`px
  column-top padding before the first item.
- **Section-label top pad:** `12`. Engraved divider = 2px (1 dark + 1 light). Heading
  block height = `topPad + [1 + 2 if top divider] + 15 label + 2 bottom`.
- **Compact control row height:** `42`px. Default `gapAbove` `2`; choice-group gaps `4`–`6`.
  Minimum compact control height `38` (`kMinCompactControlH`) — below this the layout
  engine hides the control.
- **Paired controls:** each `width/2 - 5`, split by a `10`px gutter.
- **Page strip:** height `30`, gap below `10` (same as a ContextPanel mode strip).
- **Spectrogram well:** `max(240, centerHeight*38/100)`, `14`px below it.
- **Morph timeline height:** `96`; `14`px below it before the engine panel.
- **Source card height:** `56`; gap between cards `8`.
- **Morph lane row height:** `30`; lane gap `4`.
- **Knob grid cell max height:** `132` (`kKnobCellMaxH`); knob column gap `14`, row gap `8`;
  mode-strip height `30` with a `10`px gap below it.

---

## 6. Typography **[carried]**

| Face | Source | Uses / sizes |
|---|---|---|
| **UI sans** | `uiFont()` = JUCE default sans | Control labels 12; list/combo/popup 13; buttons & ≤3-seg 12.5; >3-seg 10; cheek 11; chip 8.5; subtitle 9 (kerning 0.14); **title 17 bold (kerning 0.18)**. |
| **Mono** | `monoFont()` = JUCE default monospaced | All numeric readouts 10.5–11.5; spectrogram/timeline axis labels 8.5; octave readout 12; source-card filename 10.5; **Math script editor 12**. |
| **Serif** | `serifFont()` | **Unused in ATS Resynth** (no glyph cells) — omit. |
| **Section label** | `sectionFont()` = **10px, kerning factor 0.2** | All UPPERCASE section captions, colour `textMuted` (or `textFaint` in "rule" mode). |

---

## 7. Widget assignments — every ATS parameter, exact dims **[derived]**

All widgets are built to the reference widget specs (§7 of the reference, restated inline
where a dimension matters). `BoundControl` semantics carry over: `AudioParameterChoice` →
combo (15px label + 24px combo = 42px row), `AudioParameterBool` → toggle pill (42×21 max,
inline when row < 32px, stacked otherwise), float/int → compact linear slider (42px row:
14px label + 24px slider, TextBoxRight 52, mono 11.5) or rotary. Double-click resets to
default. `setDimmed` (alpha 0.4, no mouse) for mode-inapplicable controls.

### 7.1 Session page — morph performance (right column)

| Parameter (ID) | Widget | Dims / notes |
|---|---|---|
| `morphAmount` | Compact slider | 42px row, 2-dec display. The plugin's hero control — full column width, never paired. |
| Quick snapshots (0/25/50/75/100) | 5 cheek buttons | Row 24px; each `(w-4·6)/5` wide, `drawWell` radius 5, `uiFont(11)`, mono value text; sets `morphAmount`. Active (nearest snapshot) gets `accent`@0.24 wash + `accent`@0.9 stroke. |
| `morphPerformanceMacro` | Compact slider | Paired with smoothing, 42px row. |
| `morphSmoothingMs` | Compact slider | Paired with macro; unit "ms" in muted 24px gutter. |
| Lane amounts ×6 (`morphLane{Frequency,Amplitude,Residual,Time,Transient,Phase}Amount`) | Lane row (valueRow slider + link pill) | Row **30px**: label 58px left (sans 12 `textSecondary`: FREQ / AMP / NOISE / TIME / TRANS / PHASE), slider fills middle (6px track, r=7 thumb), mono 11 value 40px right-aligned, then link pill **34×18** far right. Lane gap 4. |
| Lane links ×6 (`morphLane…Linked`) | Toggle pill 34×18 | Inline in the lane row (above). Linked = accent pill; unlinked lane slider becomes independent (undimmed). When linked, the lane slider is `setDimmed(true)` and mirrors `morphAmount`. |
| "Link lanes" reset | Text button | 26px tall, full width under the lanes, font 12.5. |
| `timbreBrightnessDb` | Compact slider | Paired with tilt, 42px row, unit "dB". |
| `timbreTiltDbPerOct` | Compact slider | Paired with brightness, unit "dB/oct" (2-dec). |
| `timbreNoiseBalance` | Compact slider | Full width 42px row, 2-dec, label "TONAL/NOISE". |
| `partGain` | Compact slider | Paired with `resGain`, 42px row. |
| `resGain` | Compact slider | Paired with `partGain`. |
| `outputTrim` | Compact slider | Bottom-anchored master, full width 42px row, label "OUT". |

### 7.2 Session page — engine ContextPanel (center column)

Mode strip: `SegmentedRadio` **[MORPH | SMEARNOV]**, 30px, font 12.5 (2 segments),
10px gap below. Hard-swaps the grid beneath (see §11.3).

**MORPH grid** — 3 columns, col gap 14, row gap 8, every cell a 42px choice/toggle row:

| Row | Col 1 | Col 2 | Col 3 |
|---|---|---|---|
| 1 | `morphGridType` combo (Bark / Mel) | `morphQuality` combo (Max / Default / Economy) | `morphCorrespondenceStrategy` combo (Frequency rank / Greedy nearest / Assignment matrix / Optimal transport) |
| 2 | `morphUnmatchedSourcePolicy` combo (5 items) | `morphUnmatchedTargetPolicy` combo (5 items) | `morphDiscardOptIn` toggle pill, stacked label "ENABLE DISCARD" |
| 3 | `morphFrequencyInterpolationDomain` combo (Log frequency / Bark coordinate) | `morphAmplitudeInterpolationDomain` combo (Power / Decibels) | `morphPhaseStrategy` combo (5 items) |
| 4 | `morphTransientPolicy` combo (4 items) | — | — |

When `morphDiscardOptIn` is off, the "Discard" items in both policy combos render
disabled (`textFaint`); if a policy is currently Discard, dim both combos' chevrons to
`textFaint` per the combo-disabled rule.

**SMEARNOV layout** — knob row, then combos, then actions, then advanced:

| Element | Widget | Dims |
|---|---|---|
| `smearnovBlurMs`, `smearnovSharpenAmount`, `smearnovExaggerateDb`, `smearnovMix`, `smearnovThresholdAutoScale` (SENSITIVITY) | 5 tight rotary knobs | Knob grid, **cols = 3** (reference rule `count>4?3:2`) → 2 rows; cell ≤132px, col gap 14, row gap 8. 270° sweep, 2.4px accent value arc (+Ember 6.5px @0.26 halo), TextBoxBelow mono. |
| `smearnovTargets` (Partials/Residual/Both) | `SegmentedRadio` 3-seg | 28px row, font 12.5; paired beside scope with the 10px gutter. |
| `smearnovScope` (All/Selection/Sensitivity) | `SegmentedRadio` 3-seg | 28px, paired with targets. |
| Detect / Apply Smear | 2 text buttons | Paired row 30px (each `w/2-5`); Apply is the "primary" — it renders **down/active style** (well + accent@0.15) while a smear render is running. |
| ADVANCED disclosure | Text button 26px | Toggles the block below; chevron glyph rotates (`>` → `v`), text `textSecondary`→`accent` when open. |
| `smearnovMaxDb`, `smearnovWeightDetFlux`, `smearnovWeightResFlux`, `smearnovWeightHfcFlux`, `smearnovWeightBirth`, `smearnovWeightPhaseDev`, `smearnovThresholdLambda`, `smearnovPreMarginMs`, `smearnovPeakWindowFrames`, `smearnovMinGapFrames`, `smearnovMaxAttackFrames` | Compact sliders | 42px rows, paired (10px gutter) top-to-bottom in declaration order → 6 rows; ints get integer text boxes. Overflow guard hides rows < 38px. |
| `smearnovUsePhaseDeviation`, `smearnovUseRpcaFallback` | 2 toggle pills | One paired inline row 42px ("PHASE DEV" / "RPCA FALLBACK"). |

### 7.3 Session page — sources, mode, audition, variations (left column)

| Element | Widget | Dims |
|---|---|---|
| Source A / Source B | **Source card** ×2 | Raised panel radius 8, **56px** tall, inner `reduced(8,6)`. Row 1 (20px): letter chip "A"/"B" (`uiFont(8.5)` in a `textFaint` capsule 18×14; chip turns `accent` when the source has a model) + filename mono 10.5 `textPrimary`, ellipsized. Row 2 (22px): 3 cheek buttons (radius 5, `uiFont(11)`): ANALYZE / USE CUR / EXPORT, each `(w-2·4)/3`. Card gap 8. |
| Mode (Morph / Hybrid / Env) | `SegmentedRadio` 3-seg | 28px, font 12.5. Replaces the three exclusive `ToggleButton`s. |
| Transport source | Combo | 42px choice row (label "AUDITION" 15px + combo 24px)… paired with Play. |
| Play | Text button | 30px tall, 56px wide, right of the combo. Renders down/active while previewing. |
| Audition layer (Both / Partials / Residual / Smear) | `SegmentedRadio` 4-seg | 26px, **font 10** (>3 segments). Replaces the four exclusive toggles. |
| Smear enable (`smearEffectToggle`) | Toggle pill | Inline row 26px, label left "SMEAR FX", pill right. |
| Variation slots ×3 | Slot rows | Row **28px**, gap 5: recall button (flex, list-row style — `panel` fill + `panelBorder`, selected slot = `well` + `accent`@0.75 stroke 1.2px + 5px accent dot right, radius 9) + save button 44×28 labelled "SET". |
| Import WAV / Import ATS… / Export ATS… | 3 text buttons | 28px each, 6px gaps, FILE section. Duplicated in the preset-bar overflow menu (§8.6). |

### 7.4 Analysis page

| Parameter (ID) | Widget | Dims / grouping |
|---|---|---|
| Analysis profile (`analysisProfileBox`) | Combo | 42px choice row, top of left column. |
| `analysisType` (1 amp+freq / 2 +phase / 3 +noise / 4 full) | `SegmentedRadio` 4-seg | 26px, font 10. |
| `analysisStart` + `analysisDuration` | Compact sliders, **paired** | 42px, unit "s". RANGE section. |
| `analysisLowestFreq` + `analysisHighestFreq` | Compact sliders, **paired** | 42px, unit "Hz". |
| `analysisFreqDev` | Compact slider | 42px, 2-dec. TRACKING section. |
| `analysisWinCycles` + `analysisWinType` | Int slider **paired** with combo | 42px; combo items Blackman / Blackman-Harris / Hanning / Von Hann. |
| `analysisHopSize` | Compact slider | 42px, 2-dec. |
| `analysisLowestMag` | Compact slider | 42px, unit "dB". |
| `analysisTrackLen` + `analysisMinSegLen` | Int sliders, **paired** | 42px, unit "fr". |
| `analysisMinGapLen` | Int slider | 42px, unit "fr". |
| `analysisSmrThreshold` + `analysisMinSegSmr` | Compact sliders, **paired** | 42px, unit "dB". THRESHOLDS section. |
| `analysisLastPeakContribution` + `analysisSmrContribution` | Compact sliders, **paired** | 42px, 2-dec. |
| `analysisMultiRes` | Toggle pill | Inline row 26px, "MULTI-RES". |
| Import WAV / Analyze Current / Cancel / Stop Preview | Text buttons | ACTIONS section, right column: full-width 30px rows, 6px gaps. Cancel is `setDimmed` unless a worker is running. |
| Per-source: Re-analyze / Play / Export ATS | Cheek buttons in a source card | Same 56px source-card recipe as §7.3 (rows: chip+name / 3 buttons), plus a third row: audition `SegmentedRadio` [BOTH | PART | RES] 24px font 10 → card height **84px** on this page. |

### 7.5 Synthesis page

| Parameter (ID) | Widget | Dims |
|---|---|---|
| `freq` (frequency factor) | Compact slider | 42px, 2-dec, skewed range 0.25–4. PITCH section. |
| `freqShapeMode` (Linear / Stretch / Compress / Harmonic) | `SegmentedRadio` 4-seg | 26px, font 10. |
| `freqShapeAmount` + `freqShapePivotHz` | Compact sliders, **paired** | 42px; pivot unit "Hz". Both `setDimmed` when mode = Linear. |
| `timeStretch` | Compact slider | 42px, 2-dec. TIME section. |
| `synStart` + `synDur` | Compact sliders, **paired** | 42px, unit "s"; `synDur` shows "FULL" when 0 via `textFromValueFunction`. |
| `usePhase` + `useAllPartials` | Toggle pills, **paired** inline | 26px row. RENDER section. |
| `residualOnly` + `useIfftSynth` | Toggle pills, **paired** inline | 26px row. |
| `classicPreviewMode` (Classic combined / Layer mix) | Combo | 42px choice row, label "BOTH PLAYBACK". |

### 7.6 Partials page

| Element | Widget | Dims |
|---|---|---|
| Partial index | Int compact slider | 42px, CELL section, right column. |
| Frame index | Int compact slider | 42px. |
| Amplitude / Frequency | 2 `TextEditor`s | 42px rows each (15px label + 24px editor); themed per §2.1 (well bg, accent focus outline), mono 11.5, right-justified. |
| Apply to cell | Text button | 30px, full width. |

### 7.7 Math page

| Element | Widget | Dims |
|---|---|---|
| Script editor | Multi-line `TextEditor` in a well | Fills the page inside a `drawWell` radius 8 frame at `reduced(2)`; **mono 12**, `textPrimary`, accent caret/selection. |
| Apply script | Text button | 30px × 110px, bottom-right of the page, 10px above the panel edge. |
| Status line | Mono 10.5 label | Bottom-left, same 30px row; `textMuted`, turns `accent` on success flash (600ms), `#C96A5A`-free — errors use `textPrimary` on an `accent`@0.15 well chip (no new hue). |

### 7.8 Top bar controls

| Element | Widget | Dims |
|---|---|---|
| Undo / Redo | 2 icon buttons (§3.4 background) | 28×30 each, 4px gap; arrow glyph 1.8px stroke `textSecondary` → `accent` on hover, `textFaint` when history empty (poll at 24fps). |
| Preset bar | §8.6 composite | 28 + 28 + 216 + 56 + 36, 30px tall, centered. |
| Activity dots | §8.4 composite | 92px slot. |
| Output meter | §8.5 composite | 168px slot. |
| Theme combo | 118×30 | Items Porcelain (id 1) / Ember (id 2). |

---

## 8. Composite components **[derived from reference §8/§12]**

### 8.1 Spectrogram well (adapts `SpectralView` discipline)
The plugin's main display: partial tracks over time (the existing `SpectrogramPanel`,
restyled). Frame = `drawWell` radius **10**. Title `"SPECTRAL MODEL"` `sectionFont`
top-left; right-aligned chip (`uiFont(8.5)`, `textFaint` capsule) shows
`{nPartials} PARTIALS · {nFrames} FR`.
- **Axes:** X = time (mono 8.5 `textFaint` labels at 0/¼/½/¾/end, gridlines
  `textMuted`@0.09); Y = log frequency 40 Hz–14 kHz, ticks at 100/1k/10k
  (`textMuted`@0.18 lines + `textFaint` mono-8.5 labels) — same treatment as the
  reference spectrum well.
- **Partial traces:** each track a 1px polyline in `accent`, per-segment alpha =
  `0.15 + 0.77·ampNorm` (amplitude-weighted); **no halo per trace** (hundreds of
  traces — the glow budget is spent on selection only). Selected partial: 1.8px
  `accent`@0.92 + Ember halo 4.5px @0.22. Residual energy: filled band at the bottom,
  `accentDim`@0.10 (Porcelain 0.07).
- **Playhead:** 1px `textPrimary`@0.7 vertical line during preview.
- **Caching:** the full track bed renders **once** into an `Image` on model-revision
  change and is blitted; only playhead + selection redraw per frame (30 fps timer,
  early-out when neither changed). Same discipline as the keyboard bed.
- Both themes: Ember draws traces over `well` `#1A1C21`; Porcelain over `#ECEAE5` with
  the 1px `wellBorder` — no shadow stack.

### 8.2 Morph timeline (adapts `EnvelopeEditor`)
Breakpoint editor for morph-amount-over-time (existing `MorphTimelineEditor`, restyled).
Height **96**. Title `"MORPH TIMELINE"` `sectionFont` top-left; right-aligned text
buttons `BOUNCE` 64×22 and `RESET` 48×22. Graph = `drawWell` radius 8.
- Node handles: 9px circles, fill `window`/`panel`, ring `accent` 1.8px; drag to move,
  double-click to add/remove (existing behavior).
- Curve: 2px `accent` (+Ember halo 4.5px `accent`@0.2), fill under curve `accent`@0.12;
  when the timeline is inactive (mode ≠ Env): curve `textMuted`@0.55, fill @0.05.
- X axis **linear** over the model duration (not the reference's log-from-zero ADSR
  axis — a timeline is wall-clock), mono 8.5 time labels.
- Readout mono 10.5 bottom-right: `"{t}s  {amount}"` for the dragged node. 24 fps.

### 8.3 Output spectrum (Synthesis page; carries `SpectralView` verbatim)
Frame = `drawWell` radius 10; title `"OUTPUT SPECTRUM"`. Log-freq 40 Hz–14 kHz; ticks at
100/1k/10k; 3 horizontal gridlines at ¼/½/¾ height `textMuted`@0.09. Trace 1.8px
`accent`@0.92 (Ember glow 4.5px @0.22) + fill `accent`@0.10 (Porcelain 0.07). FFT order
12 (4096-pt), Hann, 256 log-mapped display bins, magnitude norm `(20·log10(m)+84)/84`,
smoothing `0.72·old + 0.28·new`, **30 fps**.

### 8.4 Activity dots (repurposes `VoiceDots`)
ATS Resynth has no polyphonic voices; the dot row becomes a **worker-activity meter**.
Caption `"ENGINE"` `sectionFont`; **2 dots, 6px dia**, labels beneath in mono 8.5
`textFaint`: `ANL` (analysis worker) and `RND` (resynth/smear/morph render worker).
Lit = `accent` (+Ember halo @0.3 expanded 3) while the worker thread runs; unlit =
`textFaint`@0.5 (dark) / `panelBorder`. 15 fps poll of the processor's
`analysisThreadRunning` / render-pending atomics.

### 8.5 Output meter (carries `OutputMeter` verbatim)
Caption `"OUT"`; right-aligned mono-11.5 `"{db} dB"` in a 58px slot, 8px gap, `drawWell`
track radius 4, gradient fill `accentDim`→`accent` radius 3, 2px peak-hold tick
`accent`@0.9. Norm `(dB+60)/60`; fast attack, ~300 ms release (`level *= 0.9` @30 fps);
1.5 s peak hold then `*0.85` decay.

### 8.6 Preset menu bar (carries the reference composite; ATS overflow items)
Widths prev `<` **28**, next `>` **28**, name combo **216**, Save **56**, `…` **36**,
all 30px, centered in the top bar (2/2/6/6/2 internal gaps). Binds to the existing
`PresetLibrary`. Overflow menu: Save · Save As… · (sep) · Rename… · Delete · Revert to
Saved · (sep) · **Import WAV… · Import ATS… · Export ATS…**. Dirty presets get `*`;
user presets append `  user`; Rename/Delete disabled for factory presets. Name entry via
themed `AlertWindow`.

### 8.7 Theme switch (carried)
**118×30** combo, items `Porcelain` (id 1) / `Ember` (id 2); on change sets `uiTheme`
and calls `applyTheme()` (§11.2).

### 8.8 Page ContextPanels
Each page (§10.3) is a persistent component shown/hidden by the page strip — a hard swap
with no animation. The **engine sub-panel** on the Session page is a true
`ContextPanel::rebuild()`: switching MORPH ↔ SMEARNOV destroys and recreates the grid
children, then calls `sendLookAndFeelChange()` so fresh sliders pick up themed text-box
colours.

---

## 9. On-screen keyboard **[carried, role adapted]**

Full reference spec applies unchanged: 92px cheek block (OCTAVE −/readout/+, PC and
VEL/FIX cheek toggles, 16×16 collapse), key geometry (sharps 0.60·whiteH × 0.62·whiteW,
radii 3.0/3.5), idle-key recipe, **cached bed image**, pressed-key recess + velocity-scaled
accent tint + 64px glow sprite, 60 fps with 0.11/frame release easing, position-based
velocity (`0.30 + 0.70·frac`, FIX = 0.80), mouse legato drag, PC input (A–L, Z/X octave),
range C2–C6 shifted ±3, collapsed height 22, expanded `clamp(15% of window, 108, 168)`.

**Role change (the only adaptation):** ATS Resynth is monophonic-transpose — a noteOn
sets `frequencyFactor` transpose = `2^((note−60)/12)` (`PluginProcessor.cpp:2938–2954`).
So the mirror layer lights **exactly one key**: the current transpose note (C4 = unity).
The unity key (C4 at octave 0) additionally carries a 3px `accent`@0.5 underline tick on
the bed so "no transpose" is findable. Velocity still injects (position-based) but only
affects the MIDI message, not the mirror intensity — mirror intensity is fixed 0.8.
**Default state: collapsed** (the keyboard is secondary to a model editor); expanded
state persists in the editor's saved UI state.

---

## 10. Layout system **[derived]**

### 10.1 Window & body
- **Base size 1280×940.** Resizable, limits **1100×860 → 1900×1400**.
- `paint`: fill `window`; body = `withTrimmedTop(62).reduced(16)`; subtract keyboard band
  (height + 12) from the bottom; subtract page strip (30 + 10) from the top; draw the
  current page's raised panels (radius 14) per §10.3.
- `resized` mirrors the arithmetic exactly.

### 10.2 Top bar (46px, trimmed 6 at bottom)
- **Left (320px):** title `"ATS RESYNTH"` 23px slot (`uiFont 17 bold`, kerning 0.18,
  `accent`); subtitle `"SPECTRAL MODEL EDITOR"` (`uiFont 9`, kerning 0.14, `textFaint`).
- **Right cluster (516px), right-to-left:** theme combo 118×30 · gap 14 · OUT meter 168 ·
  gap 14 · ENGINE dots 92 · gap 14 · redo 28 · 4 · undo 28. Meter and dots vertically
  centered on a dots-height row.
- **Center:** preset bar (≈380px) centered in the leftover middle, 30px tall.

### 10.3 Page strip
Directly under the top bar, inside the body margin: a `SegmentedRadio`
**[SESSION | ANALYSIS | SYNTHESIS | PARTIALS | MATH]**, height **30**, width
`min(640, bodyWidth)`, horizontally centered, **font 10** (>3 segments), 10px gap below.
Not parameter-bound (UI state only, persisted). Switching pages is a hard visibility
swap, no animation. Headings/dividers within pages follow §7.7–7.8 of the reference.

### 10.4 SESSION page — three columns
Raised panels radius 14: **left 260 · gap 16 · center flex · gap 16 · right 360**, each
inner `reduced(14)` + 14px top pad. Laid out by the column engine (§10.7).

- **Left (260):** SOURCES heading → Source A card 56 → gap 8 → Source B card 56 →
  MODE heading (top divider) → mode 3-seg 28 → AUDITION heading (top divider) →
  source combo + Play row 30 → gap 4 → layer 4-seg 26 → gap 4 → SMEAR FX pill row 26 →
  VARIATIONS heading (top divider) → 3 slot rows 28 (gap 5) → FILE heading (top divider)
  → 3 buttons 28 (gap 6).
- **Center:** spectrogram well `max(240, centerH*38/100)` → gap 14 → morph timeline 96 →
  gap 14 → engine ContextPanel (mode strip 30 + 10 + grid) filling the rest.
- **Right (360), bottom-anchored first:** OUT slider 42 → gap 10 → partGain+resGain
  paired 42 → engraved divider 27 → *(top-down from the 14px pad)* MORPH heading →
  morphAmount 42 → snapshot strip 24 (gap 4) → macro+smoothing paired 42 →
  LANES heading (top divider) → 6 lane rows 30 (gap 4) → LINK LANES button 26 →
  TIMBRE heading (top divider) → brightness+tilt paired 42 → noise balance 42.

### 10.5 ANALYSIS page — three columns
**Left 300 · gap 16 · center flex · gap 16 · right 320.**
- **Left (300):** PROFILE combo row 42 → analysisType 4-seg 26 → RANGE heading →
  start+duration paired 42 → low+high freq paired 42 → TRACKING heading (top divider) →
  freqDev 42 → winCycles+winType paired 42 → hop 42 → peak threshold 42 →
  trackLen+minSeg paired 42 → minGap 42 → THRESHOLDS heading (top divider) →
  SMR+minSegSMR paired 42 → lastPeak+SMRweight paired 42 → MULTI-RES pill 26.
  Overflow guard (§10.7) hides trailing rows below 38px at small sizes.
- **Center:** spectrogram well, full column height (same §8.1 component instance,
  re-parented or duplicated per implementation choice — visual spec identical).
- **Right (320):** ACTIONS heading → Import WAV 30 → Analyze Current 30 → Cancel 30 →
  Stop Preview 30 (gaps 6) → SOURCE A heading (top divider) → source card 84 (§7.4
  variant) → SOURCE B heading (top divider) → source card 84.

### 10.6 SYNTHESIS / PARTIALS / MATH pages — two columns
- **SYNTHESIS:** left panel 380 (PITCH: freq 42, shapeMode 26, amount+pivot paired 42;
  TIME heading: timeStretch 42, start+dur paired 42; RENDER heading: 2 pill pair rows 26,
  BOTH PLAYBACK combo 42) · gap 16 · right flex = OUTPUT SPECTRUM well (§8.3) full height.
- **PARTIALS:** center flex = spectrogram well (§8.1) with click-to-select partial/frame
  · gap 16 · right 320 = CELL heading, partial 42, frame 42, amp editor 42, freq editor
  42, APPLY 30.
- **MATH:** single full-width panel: script-editor well filling all but a bottom 40px row
  (status mono left, APPLY SCRIPT 30×110 right).

### 10.7 Column layout engine **[carried]**
- Walks `{component, height, gapAbove, gapBelow}` items top-to-bottom.
- **Pairing:** two consecutive non-heading controls share a row — each `width/2 - 5`,
  10px gutter.
- **Overflow guard:** any non-heading control whose available height drops below
  `kMinCompactControlH=38` is hidden (bounds set empty), never squashed.
- Headings never pair and are never hidden.
- The reference's guard replaces ATS Resynth's current `Viewport` scrolling: columns are
  sized so all controls fit at base size; the guard only engages near minimum size.

---

## 11. Runtime behavior & interaction **[carried, with ATS rows]**

### 11.1 Repaint timers
| Component | Rate | Why |
|---|---|---|
| On-screen keyboard | 60 fps | Press/release glow easing |
| Output spectrum, output meter | 30 fps | Live analysis / metering |
| Spectrogram well | 30 fps timer, blit-only unless playhead/selection/model-revision changed | Cached bed |
| Morph timeline | 24 fps | Node feedback |
| Editor (theme poll, undo/redo state, worker flags, preset dirty) | 24 fps | External state changes |
| Activity dots | 15 fps | Low-cost worker blink |

### 11.2 Theme switching
`applyTheme()` swaps the active LookAndFeel (prebuilt Ember + Porcelain instances),
recolours title/subtitle, tells the keyboard **and the spectrogram** to rebuild their
cached bed images, calls `sendLookAndFeelChange()`, repaints. The editor polls
`getUiTheme()` at 24 fps so a change from another editor instance re-syncs the combo.

### 11.3 Contextual rebuild
Engine mode strip (MORPH ↔ SMEARNOV) triggers `ContextPanel::rebuild()` — destroy and
recreate children, hard swap, no animation, then `sendLookAndFeelChange()`. Page-strip
switches are visibility-only (pages persist; no rebuild). The ADVANCED disclosure on the
Smearnov panel re-runs the panel's layout pass (no teardown).

### 11.4 Hover, dim, grey
- **Hover:** per-segment/per-row in `SegmentedRadio`, variation slots, source-card
  buttons — text lifts to `textPrimary` with `accent`@0.08 wash.
- **Dim** (`setDimmed`): alpha 0.4 + disabled + no mouse. Used for: freq-shape
  amount/pivot when mode=Linear; linked lane sliders; Cancel when no worker runs;
  Smearnov sensitivity knob when scope ≠ Sensitivity.
- **Grey** (`setGreyedOut`): `panel` veil + `textFaint` selected pill + no interaction.
  Used for: the layer audition 4-seg while no model is loaded; the engine mode strip
  while an analysis is in flight.

### 11.5 Animation & caching discipline
- No per-frame Gaussian blur anywhere. Glows are low-alpha wider copies or the
  pre-rendered 64px sprite.
- Keyboard idle bed and spectrogram track bed cached to `Image`s; only dirty regions
  redraw (pressed keys expanded 4px; playhead column ±2px; selected trace bounds).
- Busy states (Analyze/Apply/Play buttons) render as the **down/active** button state —
  no spinners, no pulsing.
- Reduced-motion (if wired) drops press-translate and eases instantly.

---

## 12. Mapping gaps — reference composites without a clean ATS home **[derived]**

| Reference composite | ATS verdict |
|---|---|
| **ADSR `EnvelopeEditor`** | No ADSR parameters exist. **Reused, not omitted:** its node/curve/well treatment becomes the Morph timeline (§8.2), with a linear time axis instead of log-from-zero and Bounce/Reset buttons instead of the enable pill + AMOUNT slider. |
| **`VowelGrid` XY pad** | No 2-D vowel-style parameter pair is central enough to justify it. Closest candidate: a TIMBRE pad (X=`timbreTiltDbPerOct`, Y=`timbreBrightnessDb`) with a single snap cell at (0,0). **Recommendation: omit in v1** and use the two paired sliders (§7.1); revisit if timbre becomes a performance surface. |
| **`VoiceDots`** | No polyphony. **Repurposed** as the 2-dot ENGINE worker-activity meter (§8.4). |
| **`SpectralView`** | Carried verbatim as the Synthesis-page output spectrum (§8.3); its axis/trace/caching discipline also restyles the existing `SpectrogramPanel` (§8.1). |
| **On-screen keyboard** | Kept, but its voice-mirror role collapses to a single lit transpose key (§9); collapsed by default. |
| **Serif font** | No glyph cells anywhere. Omit. |
| **Preset bar** | Maps directly onto `PresetLibrary` + the existing preset combo/save buttons; ATS file operations join the overflow menu (§8.6). |

---

## 13. How to reproduce this screen — build sequence **[derived]**

1. Build `ThemedLookAndFeel` with the §2 palette table (both themes, exact hexes) and the
   §2.1 colour-ID set; implement `drawRaisedPanel`, `drawWell`, `drawButtonBackground`,
   `drawTooltip`, `drawLinearSlider`, `drawRotarySlider`, `drawToggleButton`,
   `drawComboBox` per §3/§7 recipes — Ember shadow paths **and** Porcelain border paths.
2. Fill background `window`; body = `withTrimmedTop(62).reduced(16)` minus the keyboard
   band (height + 12) and the page strip (30 + 10).
3. Top bar (46px): title "ATS RESYNTH" (17 bold accent) + subtitle (9 faint) left;
   centered preset bar 28/28/216/56/36 bound to `PresetLibrary` (overflow: presets +
   Import WAV/Import ATS/Export ATS); right cluster undo/redo 28×30 · ENGINE dots 92 ·
   OUT meter 168 · theme combo 118×30.
4. Page strip: 5-segment `SegmentedRadio` (font 10), 30px, centered, hard visibility swap
   between five persistent page components.
5. SESSION page: three raised panels 260/flex/360 (radius 14, `reduced(14)`, 14 top pad).
   Left: source cards (56), mode 3-seg, audition combo+Play, layer 4-seg, SMEAR pill,
   3 variation slots, FILE buttons. Center: spectrogram well
   (`max(240, 38%)`) → morph timeline (96) → engine `ContextPanel` (mode strip 30 +
   MORPH combo grid 3-col / SMEARNOV knob grid + advanced disclosure). Right,
   bottom-anchored: OUT 42 · partGain+resGain paired 42 · divider 27; top-down:
   morphAmount 42, snapshot strip 24, macro+smoothing paired, 6 lane rows 30 with 34×18
   link pills, LINK LANES 26, timbre rows.
6. ANALYSIS page: 300/flex/320 — parameter column (paired 42px rows per §7.4, overflow
   guard at 38), spectrogram center, ACTIONS + two 84px source cards right.
7. SYNTHESIS page: 380/flex — pitch/time/render rows left, live OUTPUT SPECTRUM well
   right (FFT 4096, 256 log bins, 30 fps).
8. PARTIALS page: spectrogram (click-select) + right 320 CELL column (2 int sliders,
   2 themed TextEditors, APPLY). MATH page: script well (mono 12) + status/APPLY row 40.
9. Restyle `SpectrogramPanel` per §8.1: well radius 10, log-freq axis, amplitude-alpha
   accent traces, cached bed image, playhead/selection-only repaints at 30 fps.
10. Restyle `MorphTimelineEditor` per §8.2: 9px nodes, 2px accent curve + Ember halo,
    `accent`@0.12 under-fill, 24 fps.
11. Bottom: full-width cached-bed keyboard (cheek block + idle bed + single lit transpose
    key), collapsed (22px) by default, expanded `clamp(15%, 108, 168)`, 12px gap above.
12. Wire `uiTheme` → `applyTheme()` (swap LookAndFeel, rebuild keyboard + spectrogram
    beds, repaint); poll theme/undo/worker state on the 24 fps editor timer; drive
    ENGINE dots from the worker atomics at 15 fps.
13. Verify **every** element in both themes: Ember must show the shadow stacks and accent
    halos; Porcelain must show flat fills + 1px borders and **no** shadows — then check
    dim/grey states (linked lanes, Linear shape mode, no-model grey) in both.

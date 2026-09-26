# MEARCSTAPA UI Style & Behavior Reference (plugin-agnostic)

> **Purpose.** A self-contained, plugin-independent specification of the MEARCSTAPA
> ("PolyFilter") neumorphic UI: its two themes, every drawing recipe, every control
> widget and composite component, the layout system, and the runtime behavior
> (timers, animation, caching, interaction). Any other plugin can reproduce this
> look and feel from this document alone — no access to the MEARCSTAPA source is
> required, and nothing here assumes a synth (or any particular parameter set).
>
> **How to use it.** Treat every entry as a *specification to reproduce*. Class names
> (`ThemedLookAndFeel`, `BoundControl`, …) are given only for orientation; they name
> the reference implementation but are not importable across repos. Where a value is
> load-bearing for the look, it is stated exactly.
>
> **Extracted from** `Source/PluginEditor.cpp` (palettes `:28–58`; `ThemedLookAndFeel`
> `:472`; widgets/composites; `resized()`/`paint()` `:3153–3250`), verified against the
> current build. Where prose and code disagreed, **the code wins.**
>
> **Colour convention.** JUCE stores colours as `0xAARRGGBB`. All hexes below are the
> `#RRGGBB` (fully opaque) palette values; alphas are given separately where applied.

---

## 1. Design philosophy

- **Neumorphic, low-chroma.** Depth comes from soft shadow/highlight pairs (dark theme)
  or a flat fill + 1px border (light theme), not from bevels or gradients-as-decoration.
  Surfaces read as **raised** (panels, knob bodies, buttons) or **inset/sunken** (tracks,
  wells, combos, toggle grooves).
- **One accent, disciplined.** Section identity comes from grouping, labels, and engraved
  dividers — *not* from per-section colour. The single theme accent (amber in dark,
  teal in light) is reserved for active state, fills, live traces, and "lit" affordances.
- **Two themes, always both.** Every element is styled for both **Ember** (dark) and
  **Porcelain** (light). No element may be single-theme. Theme is a single int switch
  that swaps the whole palette and repaints.
- **Tabular numerics.** All numeric readouts are monospaced so digits form straight
  column edges; number and unit are colour-split (number primary, unit muted).
- **Cheap animation.** No per-frame blur. "Glow" is a pre-rendered sprite or a low-alpha
  wider copy of the accent shape. Idle key-beds are cached to an `Image` and blitted;
  only changing elements redraw.

---

## 2. Theme palettes — exact values, both themes

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

### 2.1 Extra JUCE colour-IDs the LookAndFeel sets

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

---

## 3. Neumorphic drawing recipes

Reproduce each per-theme. Line references are to `PluginEditor.cpp`.

### 3.1 Raised panel — `drawRaisedPanel` (`:103`)
- **Ember (dark):** loop `i = 1..3`, `offset = 2*i`:
  - Dark drop: `shadowDark` @ `0.22/i`, rounded-rect at
    `r.translated(+offset,+offset).expanded(i*0.5)`, radius `radius+i`.
  - Light lift: `shadowLight` @ `0.16/i`, rounded-rect at
    `r.translated(-offset,-offset).expanded(i*0.5)`, radius `radius+i`.
  - Face fill: diagonal `ColourGradient` `panel.brighter(0.05)` (top-left) →
    `panel.darker(0.06)` (bottom-right), radius `radius`.
- **Porcelain (light):** fill `panel`; stroke `panelBorder` 1px at `r.reduced(0.5)`.

### 3.2 Inset well — `drawWell` (`:130`)
- Fill `well`.
- **Ember:** stroke `shadowDark`@0.85 1px at `r.reduced(0.5)`; then, clipped to the
  **lower 40%** of the rect, stroke `shadowLight`@0.4 1px at `r.expanded(0.5)` (a faint
  bottom light lip that sells the "sunken" read).
- **Porcelain:** stroke `wellBorder` 1px.

### 3.3 Accent glow — how "lit" reads (`:548`, `:601`, `:644`, `:926`, `:2816`)
There is no single glow primitive. The pattern is **draw the accent shape, then stamp a
low-alpha wider copy behind/around it** — Ember only (it relies on dark contrast; Porcelain
relies on the border stroke instead):
- Slider fill halo: `accent`@0.25 rounded-rect expanded `(0, +2)` behind the fill.
- Rotary value-arc halo: `accent`@0.26 stroked 6.5px under the 2.4px solid arc.
- Toggle-on thumb halo: `accent`@0.28 ellipse expanded 4 under the thumb.
- Voice-dot / list-dot halo: `accent`@0.3 ellipse expanded 3–3.5.
- Keyboard press glow: a **pre-rendered 64px radial sprite** (`buildGlowSprite`,
  `:2816`) `accent`@0.85→0 centre-out, stamped into the lower 60% of a pressed key at
  run-alpha ≤0.45 — **never a per-frame blur.**

### 3.4 Button background — `drawButtonBackground` (`:678`)
- Radius **8**, drawn at `r.reduced(0.5)`.
- **Down/active:** `drawWell` + `accent`@0.15 fill (presses into a sunken amber-tinted well).
- **Ember, up:** shadow pair (`shadowDark`@0.4 at `+1.5,+1.5`; `shadowLight`@0.35 at
  `-1.5,-1.5`) + face `window.brighter(0.1)` if hovered else `window.brighter(0.05)`.
- **Porcelain, up:** fill `window` (hover) / `panel`; stroke `accent`@0.6 (hover) else
  `panelBorder`, 1px.

### 3.5 Tooltip — `drawTooltip` (`:725`)
Rounded rect radius 8 at `reduced(1)`: fill `window.brighter(0.06)` (dark) / `panel`,
stroke `panelBorder` 1px. Text via `AttributedString`, `uiFont(12.5)`, `textPrimary`,
left-justified, laid out at `width-16`, drawn at `reduced(8,4)`. Tooltip window background
colour-id is transparent so only this rounded card shows.

---

## 4. Corner-radius scale (as implemented)

| Element | Radius (px) |
|---|---|
| Body column panels, keyboard bed backing | `14` / `12` |
| Spectrum well | `10` |
| Combo box, text button, list row, generic panel, collapsed keyboard | `8` |
| ADSR graph well | `8` |
| XY-pad selected cell, selected list panel | `9` |
| Toggle pill, segmented well | capsule = `height*0.5` (pill ≈10.5; segmented clamps ≤`12`); segmented active pill = `radius-3` |
| Cheek buttons | `5` |
| Slider track (linear) | `3` |
| Meter track | `4` (fill drawn at `3`) |
| Keyboard keys | `3.0` (sharps) / `3.5` (naturals) |

---

## 5. Spacing rhythm (as implemented)

- **Outer window margin:** `reduced(16)`.
- **Inter-column gap:** `16`. Each column inner padding `reduced(14)`, plus a `14`px
  column-top padding before the first item.
- **Section-label top pad:** `12` (`kSectionLabelTopPad`). Engraved divider = 2px (1 dark +
  1 light). Heading block height = `topPad + [1 + 2 if top divider] + 15 label + 2 bottom`.
- **Compact control row height:** `42`px. Default `gapAbove` between stacked controls `2`;
  choice-group gaps `4`–`6`. Minimum compact control height to remain visible: `38`
  (`kMinCompactControlH`) — below this the layout engine hides the control.
- **Paired controls:** each takes `width/2 - 5`, split by a `10`px gutter.
- **Envelope editor height:** `148`; gap between the two stacked envelopes `14`.
- **Spectrum height:** `max(150, centerHeight*3/10)`, `14`px below it before the context panel.
- **Knob grid cell max height:** `132` (`kKnobCellMaxH`); knob column gap `14`, row gap `8`;
  mode-strip height `30` with a `10`px gap below it.

---

## 6. Typography

| Face | Source | Uses / sizes |
|---|---|---|
| **UI sans** | `uiFont()` = JUCE default sans | Control labels 12; list/combo/popup 13; buttons & ≤3-seg 12.5; >3-seg 10; cheek 11; chip 8.5; subtitle 9 (kerning 0.14); **title 17 bold (kerning 0.18)**. |
| **Mono** | `monoFont()` = JUCE default monospaced | All numeric value readouts 10.5–11.5; spectrum/pad axis labels 8.5; octave readout 12. |
| **Serif** | `serifFont()` = JUCE default serif | Glyph cells only (e.g. an XY-pad's letter glyphs). Most plugins won't need it. |
| **Section label** | `sectionFont()` = **10px, kerning factor 0.2** (~2px tracking) | All UPPERCASE section captions, colour `textMuted` (or `textFaint` in "rule" mode). |

---

## 7. Control widgets (build these to spec)

Widgets get their look from `ThemedLookAndFeel` (a `LookAndFeel_V4` subclass) or from a
bespoke `Component`. Each entry gives class → key params → reproduce note → behavior.

### 7.1 Labeled compact slider — the workhorse
`BoundControl(compact=true)` → `Slider::LinearHorizontal`, painted by
`drawLinearSlider` (`:579`).
- **Track:** `drawWell` rounded-rect **6px tall**, radius 3, vertically centered.
- **Fill:** from track left to thumb; gradient `accentDim`→`accent` L→R; Ember adds a
  halo `accent`@0.25 rounded-rect expanded `(0,+2)` behind the fill. Drawn only when
  fill width > 1px.
- **Thumb:** circle **r=7** (14px), `accent` fill with an inner `window`/`panel` circle
  `reduced(4.5)` (reads as a ring). Ember adds a `shadowDark`@0.6 drop at `+1.5,+1.5`.
- **Value box:** `TextBoxRight`, width 52, **mono 11.5**, right-justified (so numbers
  share a straight right edge down a column).
- **Label:** sans 12 `textSecondary`, drawn above-left in the widget's own `paint`.
- **Row layout:** total 42px — label row 14, then the 24px slider row.
- **Double-click** resets to the parameter default.
- Optional 2-decimal display for specific params via `textFromValueFunction`.

### 7.2 Rotary / arc knob
`drawRotarySlider` (`:508`); enabled via `BoundControl(compact=false)`, optionally
`tightRotary=true`.
- **Sweep:** `1.25π → 2.75π` (**270°**).
- **Body:** `reduced(3)`; `arcRadius = radius - 2.5`; `knobRadius = max(6, arcRadius - 7)`.
  Ember: shadow pair (`shadowDark`@0.55 at `+2,+2`; `shadowLight`@0.45 at `-2,-2`) +
  diagonal gradient `panel.brighter(0.09)`→`panel.darker(0.22)`. Porcelain: `panel` fill +
  `panelBorder` 1px ellipse.
- **Arc track:** 3px, `well` (Porcelain `well.darker(0.04)`).
- **Value arc:** **2.4px `accent`**; Ember halo 6.5px `accent`@0.26 beneath.
- **Pointer:** 2px `textPrimary` line from `knobRadius*0.35` out to `knobRadius-2.5`.
- **Value box:** `TextBoxBelow`, mono. `tightRotary` trims the layout: knob is a centered
  square, 13px text row, 2px gap, 12px label below.

### 7.3 Toggle / enable pill
`drawToggleButton` (`:622`).
- Pill max **42×21**, capsule radius = `height*0.5`; track = `drawWell`.
- **On:** `accent`@0.3 (Ember) / @0.18 (Porcelain) fill; thumb slid right.
- **Thumb:** r = `radius-3`; on → `accent` (+Ember glow `accent`@0.28 ellipse expanded 4);
  off → `textMuted` (hover → `textSecondary`).
- In `BoundControl`, bound to an `AudioParameterBool`. Inline layout (label left, pill
  right, 48px slot centered 44×22) when short; when height ≥ 32 the label stacks above the
  pill so it lines up with a combo/slider paired beside it.

### 7.4 Segmented / radio selector (horizontal)
`SegmentedRadio` (`:320`) — bespoke `Component` bound to an `AudioParameterChoice`.
- **Track:** sunken `well`, radius `min(height/2, 12)`.
- **Segment pill:** `seg.reduced(3)`, radius `max(2, radius-3)`.
- **Active:** `drawRaisedPanel` + `accent`@0.16 (Ember)/0.10 fill + `accent`@0.8 stroke
  1.2px; text `accent`.
- **Hover:** `accent`@0.08 wash; text `textPrimary`. Inactive text `textSecondary`.
- **Font:** **10 if >3 segments, else 12.5.**
- **Greyed-out state:** `setGreyedOut(true)` disables clicks, drops hover, tints the whole
  control with a `panel`@0.28/0.18 veil and draws the selected pill only as `textFaint`@0.18.

### 7.5 List selector (vertical, raised → inset-active)
`ChoiceButtons` (`:856`) — bespoke `Component` bound to an `AudioParameterChoice`.
- Rows **28px high, 5px gap** (`idealHeight = n*28 + (n-1)*5`).
- **Unselected:** `panel` fill + `panelBorder` 1px stroke, `reduced(2)`, radius 8.
  Hover adds `accent`@0.08 wash and lifts text to `textPrimary`.
- **Selected:** `well` fill + `accent`@0.1 (Ember)/0.06 wash + `accent`@0.75 stroke 1.2px,
  `reduced(1)`, plus a **5px `accent` dot** near the right edge (Ember halo @0.3 expanded 3.5).
- **Text:** sans 13, `accent` when active else `textSecondary`, left-inset 13px.

### 7.6 Dropdown / combo
`drawComboBox` (`:651`).
- Box = `drawWell` at `reduced(0.5)`, radius 8.
- **Chevron:** `accent` (disabled → `textFaint`), 1.6px stroke, ~4px arms, apex 2.5px,
  centered 13px from the right edge.
- **Text:** sans 13, inset 8px (`positionComboBoxText` sets label bounds `8,1,w-30,h-2`).
- **Popup:** background `window.brighter(0.04)` (dark) / `panel`; highlight
  `accent`@0.22 (dark) / @0.14; text `textPrimary`; font 13. Grouped popups keep named
  headers + separator rows.

### 7.7 Section label
`SectionHeading` (`:746`) + `sectionFont()`.
- **10px sans, kerning 0.2** (~2px tracking), UPPERCASE, colour `textMuted` (or `textFaint`
  in "rule" mode).
- **Optional trailing hairline rule:** `textFaint`@0.4, 1px, starting 8px past the text and
  filling the remaining row width.
- **Optional top engraved divider** (see 7.8) drawn at `y+1`.
- Constants: `kSectionLabelTopPad=12`, `labelH=15`, `labelBottomPad=2`, `kDividerLineH=2`.
  `preferredHeight(withTopDivider)` and `labelAreaTop(withTopDivider)` compute block size.

### 7.8 Engraved divider
`InsetDividerBar` (`:825`) and `SectionHeading`'s top-divider path.
- Two stacked 1px lines: **dark** = `shadowDark`@0.85 (Ember) / `wellBorder` (Porcelain);
  **light** = `shadowLight`@0.6 (Ember) / `white`@0.6 (Porcelain), directly below the dark line.
- `InsetDividerBar::preferredHeight()` = `12 + 1 + 2 + 12` = **27** (pad, dark, light, pad).
- Reads as a thin dark line with a faint light lower edge — the neumorphic "carved"
  separator. Replaces boxed borders between stacked sub-sections.

### 7.9 Numeric / mono readout
`monoFont()` + a number/unit split (`ValueReadout`, `:259`; `createSliderTextBox`, `:710`).
- Default monospaced face, **10.5–11.5px**. Inline (label-left) values are right-justified
  so digits share a straight right edge; below-knob values are centered.
- **Number** in `textPrimary`, **unit** in `textMuted`; the unit is given a fixed **24px
  gutter** on the right so numbers align regardless of whether/what unit follows.

### 7.10 Text button (Save / `…` / `<` / `>` / any action)
`drawButtonBackground` (see 3.4), font 12.5. Small raised neumorphic button that presses
into an amber-tinted well when down/active. Icon buttons use the same background with a
single glyph in `textSecondary` → `accent` on hover.

### 7.11 `BoundControl` — the unified parameter binder
One `Component` (`:986`) that inspects the bound parameter and becomes a **slider**,
**combo**, or **toggle** automatically:
- `AudioParameterChoice` → combo (label 15px on top, 24px combo below).
- `AudioParameterBool` → toggle pill (inline or stacked per height, see 7.3).
- otherwise → slider: `compact` → linear (7.1); else rotary (7.2), optionally `tightRotary`.
- `valueRow` mode: the slider carries **no** text box; the value is painted manually
  (label left `textSecondary` 12, number right-aligned mono 11 in `textPrimary`, unit in
  the 24px muted gutter) — used where several rows must share a value column.
- `setDimmed(true)`: fades to alpha **0.4**, disables the inner control, and drops mouse
  interaction — used to grey out controls that don't apply to the current mode.

---

## 8. Composite components

| Component | Class (`:line`) | Defining parameters & behavior |
|---|---|---|
| **ADSR curve editor** | `EnvelopeEditor` (`:1744`) | Title `sectionFont` top-left; graph = `drawWell` radius 8. **3 draggable node handles** at A / D‑S / R breakpoints: 9px circles (fill `window`/`panel`, ring = curve colour 1.8px). Curve: 2px `accent` when enabled (Ember halo 4.5px `accent`@0.2), fill under curve `accent`@0.12; disabled → `textMuted`@0.55, fill @0.05. Time axis is **log-from-zero** (`normTime`/`timeFromNorm`, curve constant 20000, max 10 s), each A/D/R segment ≤ **0.30** of width. Stage readout mono 10.5: `"A {t}  D {t}  S {v}  R {t}"` (`formatMs`; sustain 2‑dp). **Enable pill top-right 42×18.** AMOUNT: `sectionFont` label + `LinearHorizontal` slider (TextBoxRight 48). Component height **148**. 24 fps repaint. |
| **Spectrum well** | `SpectralView` (`:1363`) | Frame = `drawWell` radius 10. Log-freq axis **40 Hz–14 kHz**; vertical ticks at 100/1k/10k (`textMuted`@0.18 lines + `textFaint` mono‑8.5 labels); 3 horizontal gridlines at ¼/½/¾ height `textMuted`@0.09. Title `"OUTPUT SPECTRUM"` `sectionFont` top-left. Trace: **1.8px `accent`@0.92** (Ember glow 4.5px @0.22) + filled area `accent`@0.10 (Porcelain 0.07). **FFT order 12** (4096-pt), Hann window, **256 display bins** log-mapped with linear bin interpolation, magnitude norm `(20·log10(m)+84)/84`, smoothing `0.72·old + 0.28·new`, **30 fps**. Optional dashed target overlay in `accentDim`. |
| **XY morph pad** | `VowelGrid` (`:1568`) | A generic draggable **XY parameter pad**. `drawWell`-free plot with a status row on top (mono 11 primary readout + `uiFont(9)` `textFaint` hint right). Snap targets are cells laid out at normalized positions: selected cell = `drawWell` + `accent`@0.14 wash + `accent`@0.85 stroke 1.5px radius 9; others = `drawRaisedPanel` radius 9; glyph `serifFont`. A **fading trail** (≤22 points, alpha 0.28·age) follows the cursor; the live position is a layered accent dot (48/28/12px halos + `accent` core + `window`/`panel` 4px pip). Drag sets the two normalized params. 30 fps. Reusable for any 2-D control. |
| **Voice / activity dots** | `VoiceDots` (`:1209`) | `sectionFont` caption over **8 dots, 6px dia**, gap ≤11. Lit = `accent` (Ember halo @0.3 expanded 3); unlit = `textFaint`@0.5 (dark) / `panelBorder`. When the count exceeds 8, lit dots are proportional. 15 fps. Generic "N-of-M activity" indicator. |
| **Level meter + readout** | `OutputMeter` (`:1278`) | `sectionFont` caption; row = right-aligned **mono‑11.5 `"{db} dB"`** in a 58px slot, 8px gap, then a `drawWell` track (radius 4) with gradient fill `accentDim`→`accent` (radius 3) + a 2px peak-hold tick `accent`@0.9. Norm = `(dB+60)/60`. **Fast attack, ~300 ms release** (`level *= 0.9` at 30 fps), **1.5 s peak hold** then decay `*0.85`. |
| **Contextual / swappable panel** | `ContextPanel` (`:1971`) | A container that **tears down and rebuilds** its child controls when a mode/type int changes (`rebuild()`), a **hard swap, no animation**. Header `sectionFont` top-left; an optional right-aligned pill "chip" (`uiFont(8.5)` in a `textFaint` capsule). Layout picks one of several arrangements: **mode-strip (30px) + knob grid** (2×2, or 3-across, or generic `cols = count>4?3:2`), or a bespoke sub-cluster. Cells cap at `kKnobCellMaxH=132`, gaps col 14 / row 8. This is the pattern for a panel whose contents depend on a selector elsewhere. |
| **Preset menu bar** | `TextButton`/`ComboBox` in `resized()` (`:3207`) + `showPresetMenu` (`:3303`) | Widths: prev `<` **28**, next `>` **28**, name combo **216**, Save **56**, `…` **36**, all 30px tall, centered in the top bar (2/2/6/6/2 internal gaps). Overflow menu: Save · Save As… · (sep) · Rename… · Delete · Revert to Saved · (sep) · Import… · Export…. Dirty presets get a `*`; user presets append `  user`. Rename/Delete disabled for factory presets. Name-entry via a themed `AlertWindow`. |
| **Theme switch** | `themeBox` `ComboBox` (`:3074`) | **118×30** combo, items `Porcelain` (id 1) / `Ember` (id 2); on change sets `uiTheme` and calls `applyTheme()`. |
| **On-screen keyboard** | `OnScreenKeyboard` (`:2356`) | See §9. |

---

## 9. On-screen keyboard (full spec)

`OnScreenKeyboard` (`:2356`). Runs full-width along the bottom; dual-role (injects MIDI +
mirrors sounding voices).

- **Cheek block:** `cheekWidth = 92`px on the left, `reduced(8,8)` inside. `drawWell`
  backing (radius 8). Layout: an `"OCTAVE"` caption strip (11px), then two rows —
  row 1 = `−` / octave readout / `+` (thirds); row 2 = a `PC` toggle (computer-key input)
  and a `VEL`/`FIX` velocity-mode toggle (half each). A 16×16 collapse `–` sits top-right of
  the cheek. Each is a **cheek button**: `drawWell` radius 5; active → `accent`@0.24/0.20
  wash + `accent`@0.9 stroke; text `uiFont(11)`, `accent` when active. Octave readout is
  `accent` mono 12.
- **Key geometry:** `keyArea = bounds.reduced(4,6)`. Naturals fill the width evenly;
  sharps are `0.60·whiteH` tall, `0.62·whiteW` wide, centered on the gap between adjacent
  naturals and drawn **on top**. Radius 3.0 (sharp) / 3.5 (natural).
- **Idle key (`drawIdleKey`):** light-from-above. A baked soft drop shadow beneath the
  lower edge (2 layers, drop 2.5px natural / 3.0px sharp, alpha 0.20 / 0.34), an even face
  with a gentle top-lighter gradient (`edge → face → face.darker(0.06)`), a crisp top-edge
  highlight line, a porcelain hairline border on naturals, and a faint front lip on sharps.
- **Cached bed (`renderBed`):** the whole idle bed — raised backing panel (radius 12) + all
  idle keys + the cheek + a 64px glow sprite — is rendered **once** into an `Image` and
  blitted each frame. Rebuilt only when size/theme/state changes (`bedDirty`).
- **Pressed key (`drawPressedKey`):** only currently-lit keys are redrawn live over the bed.
  The key **darkens** (recess `0.22`·amount dark / `0.10` light), gains an inset dark
  top-left + light bottom lip, an `accent` face tint scaled by velocity (base 0.11, or 0.07
  when held by sustain, +0.34·intensity, clamped ≤0.55), the glow sprite in its lower 60%
  (alpha ≤0.45), and a thin `accent` edge stroke. A lit natural clips out overlapping sharps
  so a sharp never vanishes.
- **Animation:** 60 fps. Press is instant; release **eases** at 0.11/frame and snaps to 0
  below 0.004 so the glow fully clears. Repaints are per-key (`noteBounds` expanded 4px).
- **Velocity:** position-based by default (`0.30 + 0.70·frac`, lower on key = harder) or
  fixed `0.80` in FIX mode.
- **Input:** mouse (with legato drag/gliss across keys), MIDI, and computer keys (A–L row,
  `pcMap` a=0…l=14 semitones; Z/X shift the PC octave). Octave shift ±3; PC octave ±4.
- **Range:** default C2–C6 (notes 36–84, 4 octaves), shifted by octave.
- **Collapsed:** height 22; a `drawRaisedPanel` bar with a `KEYBOARD` cheek button and a
  hint label; click to expand. Expanded height = `clamp(15% of window, 108, 168)`.

---

## 10. Layout system

### 10.1 Window & body
- **Base size 1280×940.** Resizable, limits **1100×900 → 1900×1400**.
- `paint`: fill `window`; body = `withTrimmedTop(62).reduced(16)`; subtract the keyboard
  band (its height + 12) from the bottom; draw three raised panels — left 250, gap 16,
  right 360, gap 16, center = remainder — each radius 14.
- `resized` mirrors the same arithmetic (`reduced(16)`; keyboard removed from bottom + 12px
  gap; 46px top bar trimmed 6 at bottom; columns 250 / gap 16 / center / gap 16 / 360, each
  `reduced(14)`).

### 10.2 Top bar (46px, trimmed 6 at bottom)
- **Left:** title area 320px — title 23px slot (`uiFont 17 bold`, kerning 0.18, coloured
  `accent`), subtitle beneath (`uiFont 9`, kerning 0.14, `textFaint`).
- **Right cluster:** 452px — theme combo 118×30 (right), gap 14, OUT meter 168, gap 14,
  VOICES dots (remainder). Meter and dots are vertically centered on a `VoiceDots`-height row.
- **Center:** preset bar (28+28+216+56+36+16 ≈ 380px) centered in the leftover middle, 30px tall.

### 10.3 Three-column content (reference arrangement)
- **Left column (250):** section headings + compact controls + a vertical `ChoiceButtons`
  list, laid out by the column engine (see 10.4). Headings use engraved top dividers to
  separate sub-sections.
- **Center column:** 14px top pad, spectrum well (`max(150, height*3/10)`), 14px gap, then
  the swappable `ContextPanel` filling the rest.
- **Right column (360), bottom-anchored first:** OUT slider (42) · gap 10 · a small
  control (26) · engraved divider · [14px top pad] · Amp envelope (148) · gap 14 · Filter
  envelope (148) · gap 10 · a paired-control list.

> The three-column arrangement is the *reference* layout, not a requirement. The palette,
> recipes, radii, spacing, typography, widgets, and composites all carry over to any layout
> (editor/workspace archetypes included) — only this section is synth-panel-specific.

### 10.4 Column layout engine — `layoutColumn` (`:3450`)
- Walks a list of `{component, height, gapAbove, gapBelow}` items top-to-bottom, removing
  `gapAbove` then the item height.
- **Pairing** (`pairControls=true`): two consecutive non-heading controls share a row —
  each `width/2 - 5`, split by a 10px gutter.
- **Overflow guard:** any non-heading control whose available height drops below
  `kMinCompactControlH=38` is hidden (bounds set empty) rather than squashed.
- Headings never pair and are never hidden.

---

## 11. Runtime behavior & interaction

### 11.1 Repaint timers (frame rates)
| Component | Rate | Why |
|---|---|---|
| On-screen keyboard | 60 fps | Smooth press/release glow easing |
| Spectrum, level meter, XY pad | 30 fps | Live analysis / metering / trail |
| ADSR editor | 24 fps | Node feedback |
| Editor (theme/preset/filter-type poll) | 24 fps | Detects external state changes |
| Voice dots | 15 fps | Low-cost activity blink |

### 11.2 Theme switching
`applyTheme()` swaps the active `LookAndFeel` (a prebuilt Ember + Porcelain instance),
recolours the title/subtitle, tells the keyboard to rebuild its cached bed, calls
`sendLookAndFeelChange()`, and repaints. The editor also polls `getUiTheme()` on its 24 fps
timer so a theme change from elsewhere (e.g. another editor instance or automation) is
picked up and the combo re-synced.

### 11.3 Contextual rebuild
When the mode/type int changes, `ContextPanel::rebuild()` destroys and recreates its
children — a **hard swap with no animation**. Freshly created sliders are forced to pick up
the themed LookAndFeel via `sendLookAndFeelChange()` (otherwise they'd bake in default
text-box colours).

### 11.4 Hover, dim, grey
- **Hover** is tracked per-segment / per-row in the bespoke selectors (`SegmentedRadio`,
  `ChoiceButtons`) and lifts text to `textPrimary` with a faint `accent`@0.08 wash.
- **Dim** (`BoundControl::setDimmed`): alpha 0.4 + disabled + no mouse — for
  mode-inapplicable controls.
- **Grey** (`SegmentedRadio::setGreyedOut`): a `panel` veil + `textFaint` selected pill +
  no interaction — for a selector that is inert in the current mode.

### 11.5 Animation & caching discipline
- No per-frame Gaussian blur anywhere. Glows are low-alpha wider copies or a **pre-rendered
  64px radial sprite**.
- The keyboard's idle bed is cached to an `Image` and only dirty keys redraw; on release the
  key eases and its `noteBounds` (expanded 4px) is the only region repainted.
- Reduced-motion (if wired) drops the press-translate and eases instantly.

---

## 12. Reusable patterns beyond the reference's own use

The reference is a synth, but several of its components are generic and worth lifting for any
plugin:

- **`ContextPanel`** — a selector-driven panel that hard-swaps its contents. Use for any
  "the controls below depend on a mode above" situation.
- **Knob grid** — the `ContextPanel` already lays knobs into 2×2, 3-across, and generic
  `cols = count>4?3:2` grids with `kKnobCellMaxH=132`, col gap 14, row gap 8. There is *no*
  dedicated `KnobGrid` class, but the layout math is present and reproducible; factor it into
  a shared component if you need knob grids in several places.
- **`VowelGrid` → generic XY pad** — a draggable 2-D parameter pad with snap cells, a fading
  cursor trail, and a layered live dot. Rename/retheme for any XY control.
- **`VoiceDots` → N-of-M activity meter**, **`OutputMeter` → any level+readout**,
  **`SpectralView` → any log-frequency trace** with the same well/trace/glow discipline.
- **`EnvelopeEditor`** — a draggable-node curve editor over a well; the node/curve/readout
  treatment transfers to any breakpoint editor.

---

## 13. Quick "how to reproduce a screen" checklist
1. Fill background `window`. Draw raised column/section panels (radius 14) inside a
   `reduced(16)` body with a 62px top strip; subtract the keyboard band from the bottom.
2. Top strip (46px): title (17 bold, accent) + subtitle (9, faint) left; centered preset
   bar (28/28/216/56/36); right cluster level meter · activity dots · theme combo (118×30).
3. Populate content with section labels (10px tracked, muted), engraved dividers (27px)
   between sub-sections, and the §7 widgets at 42px rows (pair with a 10px gutter where wanted).
4. For live displays use the §8 composites (spectrum well 30% height, ADSR 148, XY pad,
   meter, dots) at their frame rates.
5. Anchor a master control at the bottom of its column; put contextual content in a
   hard-swapping `ContextPanel`.
6. Bottom: full-width cached-bed keyboard (cheek block + idle bed image + live lit overlay),
   15% window height clamped [108,168], 12px gap above.
7. Wire the `uiTheme` switch to swap the whole palette (§2) and repaint; poll for external
   theme/state changes on a low-rate timer.

## 14. On sharing real code vs. reproducing
The cleanest path to *guaranteed* parity is to lift `ThemedLookAndFeel` + the widget and
composite classes into a **shared JUCE module** consumed by multiple plugins, so a palette or
radius change lands everywhere at once. If that's not feasible, every value in §2–§10 is
complete enough to hit pixel-parity by reproduction alone.

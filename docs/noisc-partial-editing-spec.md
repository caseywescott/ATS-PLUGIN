# Noisc-Style Partial Editing & Audition — Spec

**Reference:** [noisc / formant demo](https://dy.github.io/noisc/) (Dmitry Iv., single-file
editor: `docs/index.html` in [dy/noisc](https://github.com/dy/noisc)) — "SVG for sound":
a recording traced into editable curves, every partial a vector path with control points,
played back live while you edit.

**Target:** `SpectrogramPanel` (src/plugin/SpectrogramPanel.cpp/.h),
`PartialEditPanel`, `PluginProcessor` edit/preview APIs.

**Prime directive:** the ATS frame model stays the source of truth. Everything below is
an *editing and audition layer* over the existing partial/residual frames — control
points are fitted from frames and rendered back to frames, exactly as noisc does
(`fitCP` → edit nodes → `renderTrack` → per-frame arrays). No change to the analysis
pipeline, morph engine, session format core, or the offline transient layer.

---

## 1. Feature gap analysis

What noisc has that we don't (function first, then UI):

| # | noisc feature | Our current state | Gap |
|---|---|---|---|
| 1 | **Control-point curve editing** — partials stored as sparse nodes (RDP-simplified, ~4× fewer than frames), monotone-cubic (PCHIP) interpolation between; drag a node moves it in **frequency AND time**; click the line = insert node; ⌥-click = delete node (middle deletion splits the curve); drag one curve's endpoint onto another's = **join**; rectangle-select nodes, drag the set, ⌫ deletes the set | `PartialEditPanel`: one partial × one frame × typed amp/freq cell. Freehand freq "Draw" repaints frames under the cursor | **The** headline gap. No node model, no time editing, no split/join, no multi-node ops |
| 2 | **Direct drag on the spectrogram** — drag a curve to transpose it (whole selection moves together; a group moves as one) | Selection exists (click/lasso/shift-add) but all edits go through toolbar buttons; no drag-to-move | Drag = the primary edit verb in noisc; we have none |
| 3 | **Hover label with draggable numerals** — hovering a curve shows a floating `f · q · level` label; drag a number to tune it, click to type | Hover highlights the partial; dB readout for residual only | No direct numeric manipulation at the cursor |
| 4 | **Scrub & frozen-instant audition** — drag the caret to any time and *hold*: the snapshot keeps sounding (non-destructive listening tool); ←/→ nudge | Playhead is display-only. `freezePartialAtFrame` exists but is a **destructive edit** | Can't park the playhead somewhere and listen to the spectrum there |
| 5 | **Playback speed keeping pitch** — ±speed slider (curves are evaluated at any clock rate, pitch untouched) | Fixed-rate preview | ATS synthesis is frame-interpolated, so varispeed audition is nearly free for us |
| 6 | **Non-destructive per-partial mute/solo** — `m` key, S\|M pills, live toggles during playback | `mutePartials` zeroes amplitudes in the model (undoable, but destructive); Solo = one-shot preview render | No live mute mask feeding resynthesis |
| 7 | **Grouping / channel rack** — ⌘G groups curves into a channel; harmonic constructs (f0 curve + locked harmonics edited as one object); rack rows drag between groups; lock, visibility eye, per-group level, × deletes group with curves | No grouping of any kind | Harmonic-construct editing (drag f0, all harmonics follow) is uniquely valuable for ATS material |
| 8 | **Hover-prelisten** — hovering a rack row auditions that layer at the caret | Nothing comparable | Cheap once #4's snapshot voice exists |
| 9 | **Log/mel frequency scale** — mel default, log toggle | Linear frequency only | Low frequencies (where the music is) are crushed in our view |
| 10 | **Aligned waveform strip** — under the spectrogram, seek/scrub there, spectrogram no longer seeks | None | Nice-to-have; pairs with #4 |
| 11 | **Create partials by drawing** — ＋ / shift-double-click adds a curve; drag its endpoints to extend | Partials come only from analysis | Needed for "repair" workflows (redraw a lost harmonic) |
| 12 | **Per-partial width/noisiness (q) editing** — drag band edge or wheel over a curve | ATS partials carry per-band noise energy but we never expose it per partial | Maps naturally onto ATS band-noise redistribution |
| 13 | **Gesture help overlay** — `?` button, one panel listing every gesture | Tooltips only | Trivial, high value once gestures exist |
| 14 | **Per-gesture undo granularity** — `beginEdit` opens a history entry at drag start; every point/curve/channel edit lands in ⌘Z | `UndoManager` with coarse transactions | Extend, don't replace |

Already at parity or better (do not touch): zoom/pan/scrollbars/pinch, lasso + shift
additive selection, delete/scale selection, residual band editing suite (copy/paste/
swap/redist/tilt/smooth — noisc has nothing like it), isometric views, morph timeline,
Smearnov onset editing, envelope box tools.

---

## 2. The noisc look (reference notes)

Palette: near-black blue `#0b0e14` background, panels `#11151d`/`#141926`, hairline
borders `#1e2430`/`#2a3346`, body text `#c8d0dc`, muted `#5c6675`, accent blue
`#2456a6`→`#3987e5`, **orange caret** (the one warm element), green = fitted curves,
per-channel hues for bound curves, white = selection.

Interaction texture — this is what to steal, not the colors (we keep the
mearcstapa/spectrum-overhaul theme, see `docs/mearcstapa-ui-style-reference.md` and
`docs/spectrum-view-overhaul-spec.md`):

- **Inline draggable numerals**: 10px monospace values with dotted underlines,
  `ew-resize` cursor, drag to scrub the value, click to type. No sliders where a
  number will do.
- **On-canvas floating labels**: translucent dark chips (`rgba(11,14,20,.72)`,
  4px radius) that follow the hovered/selected curve; contain the numerals plus tiny
  mute/delete icon buttons. Controls live *at the data*, not in a toolbar.
- **Quiet pill toggles**: segmented S|M pill, states color-coded (solo amber
  `#ffd479`, mute red `#ff9d9d`), invisible until relevant.
- **Left rack**: 236px, nested rows with fold arrows, 9px channel-color dots,
  hover reveals row actions, drag rows to re-parent.
- Minimal chrome overall: gestures carry the workload; a single `?` overlay
  documents them.

---

## 3. Design

### 3.1 Node overlay for partials (gap #1, #2, #11) — M1/M2

**Data model** (new, view/edit-side only):

```cpp
struct PartialNode  { int frame; double freqHz; double amp; };   // + noiseRatio later
struct PartialCurve { int partialIndex; std::vector<PartialNode> nodes; bool dirty; };
```

- `fitNodes(const AtsPartialTrace&) -> nodes`: Ramer–Douglas–Peucker on
  (log-f tol ≈ 10 cents, level tol ≈ 3.5 dB) — noisc's proven tolerances; vibrato
  survives, drift compresses (~4× reduction measured on real material).
- `renderNodes(curve) -> frames`: monotone cubic (Fritsch–Carlson/PCHIP) evaluated at
  every frame index between first/last node; write through the existing
  `PluginProcessor` partial-edit path so **undo, resynthesis, and session save all work
  unchanged**. New processor API: `setPartialTrajectory(source, partialIndex,
  firstFrame, lastFrame, const std::vector<AtsPartialFramePoint>&, label)` — one
  undoable transaction per gesture.
- Nodes are fitted lazily when a partial enters **edit mode** and discarded on commit;
  the model never stores nodes (frames stay canonical — a session opened elsewhere is
  identical).

**Interaction — spectrogram level (Combined/Partials modes):**

- Drag a selected curve vertically → transpose in Hz (whole selection moves; snap to
  cents with ⌘). Horizontal drag with ⌥ → time-shift (bounded by trace length).
  Commit on mouse-up as one undo transaction.
- Double-click a partial → **point editor mode** for that curve (replaces nothing;
  Esc exits). All other curves dim to 25% alpha.

**Interaction — point editor mode:**

- Nodes drawn as 5px squares on the curve; selected = white, others = theme accent.
- Drag node = move in f **and** time (clamp between neighbor frames; sweeping over a
  neighbor consumes it — noisc's "eats them" rule).
- Click on the curve between nodes = insert node there.
- ⌥-click node = delete; deleting a middle node splits the partial in two (new partial
  index via existing add-partial path).
- Drag a terminal node onto another curve's terminal node (≤ 8px) = **join** the two
  partials (gap filled by PCHIP; the joined partial keeps the earlier index).
- Rectangle-drag on empty background = select node set; drag any selected node moves
  the set rigidly; ⌫ deletes the set.
- Selected node grows a vertical **amp bar**; drag its tip to set level (dB-mapped).
- Every completed gesture = one undo entry (`beginEdit` at pointer-down, commit at up).

### 3.2 Direct numerals & on-canvas labels (gap #3) — M2

- Hover chip near the cursor when over a partial: `p17 · 1174.2 Hz · −23.4 dB`
  (+ `· q 41` once 3.5 lands). Values use the mearcstapa mono font.
- In point-editor mode and for selected partials the chip is interactive: drag a
  numeral horizontally to scrub (freq: 1 cent/px, ⇧ = 10; amp: 0.1 dB/px), click to
  type. Applies through the same trajectory API (constant offset over the partial —
  i.e. dragging the freq numeral transposes the partial).
- Chip carries two icon buttons: mute-toggle (3.4) and delete.

### 3.3 Scrub, freeze-listen, varispeed (gaps #4, #5, #8, #10) — M1

The audition core; independent of node editing and shippable first.

- **Caret drag = seek.** Playhead becomes interactive in all planar modes: click/drag
  on the top 12px strip (or the playhead line ±4px) scrubs preview playback time.
  ←/→ step one frame (⇧ = 10 frames).
- **Freeze-listen.** Holding the caret still (or toggling a `Hold` pill next to the
  transport) sustains the current frame's spectrum through the existing synthesis
  engine — loop a 1-frame window with interpolation jitter disabled. This is a
  *preview state*, never a model edit. The existing destructive Freeze button stays
  (renamed **"Freeze → model"** to disambiguate).
- **Varispeed slider** `0.25×–4×` (log, center 1×) next to the transport: scales the
  frame-clock of preview synthesis only. ATS synthesis already interpolates between
  frames, so pitch is inherently preserved.
- **Hover-prelisten (later, M3):** hovering a group row in the rack (3.5) plays that
  group at the caret while hovered — reuses the freeze-listen voice with a partial mask.
- **Waveform strip (optional, M3):** 60px strip under the plot showing the source
  audio (when the .ats has a companion audio file) or the resynth envelope; click/drag
  there = seek. Spectrogram clicks then stay pure selection, matching noisc.

### 3.4 Non-destructive mute/solo (gap #6) — M1

- `std::vector<bool> partialAuditionMask` (+ residual band mask) held by the
  processor, **outside** the undoable model; applied at synthesis voice-gain stage.
  Not saved in sessions; cleared on model reload.
- `m` mutes selection, `s` solos selection (solo = mask everything else; press again
  to clear). Muted partials render at 20% alpha with a slashed chip icon.
- Existing destructive `mutePartials` stays for "commit this mute into the model"
  (button relabelled **"Mute → model"**).

### 3.5 Groups & harmonic constructs (gap #7) — M3

- `PartialGroup { juce::String name; juce::Colour colour; std::vector<int> members;
  bool locked, hidden; float gainDb; int f0Member = -1; }` — persisted in the session
  blob (unknown-field JSON, same pattern as `timbre_motion_plan`).
- ⌘G groups current selection; ⌘⇧G ungroups. Click a member selects the group first;
  click again drills into the single partial (noisc's rule).
- Rack: reuse the left column of the session tab pattern (Viewport per
  `project_scrolling_ui`) listing groups → member rows; per-row S|M pill, lock,
  eye (hidden = excluded from display, *not* from audio), color dot, gain numeral.
- **Harmonic construct**: "Detect harmonics" on a selected partial finds members at
  ~k·f0 (reuse existing peak/partial data, tolerance ±35 cents). In a construct,
  dragging the f0 curve (or its nodes) scales every member's frequency by the same
  ratio — the ATS answer to noisc's f0-locked groups. Amp edits stay per-member.
- Group transpose/gain apply through existing scale/trajectory APIs as single
  transactions.

### 3.6 Per-partial noisiness editing (gap #12) — M4 (exploratory)

ATS types 3/4 carry per-partial band noise. Expose it as noisc exposes q:

- Selected partial + wheel (with `N` held) or dragging the rendered band-width edge
  scales that partial's share of band-noise energy, redistributing within its critical
  band (keeps total band energy — consistent with `feedback_dsp_correctness`, no
  free energy).
- Display: band-width halo around the curve at width ∝ noise ratio (already have
  the residual backdrop machinery to draw it cheaply).
- Numerals chip gains a `q`-like readout: `tone 87%`.

### 3.7 Frequency scale toggle (gap #9) — M1

- `lin | log | mel` segmented pill in the view chrome (mearcstapa segmented radio,
  `docs/MearcstapaSegmentedRadioDropIn.h`). Default stays linear for continuity;
  persist choice in plugin state.
- Pure view transform: `freqNormFromScreenY`/`positionForPoint` route through a
  `FreqScale` mapper (u ↔ Hz). All hit-testing already funnels through these two
  helpers, so selection/draw/labels follow automatically. Isometric modes keep linear
  in M1 (revisit later).

### 3.8 Help overlay (gap #13) — M2

- `?` button in view chrome → dismissible overlay listing every gesture/key, two-column
  `gesture — effect` rows exactly like noisc's `#helpPanel`. Content lives in one
  static table in code; update it in the same commit as any gesture change.

---

## 4. Milestones

| Phase | Contents | Depends on |
|---|---|---|
| **M1 — Audition core** | Caret scrub + ←/→, freeze-listen, varispeed, non-destructive mute/solo masks, lin/log/mel scale | — |
| **M2 — Node editing** | Node fit/render + trajectory API, point-editor mode (insert/delete/split/join/amp bar/node lasso), drag-to-transpose, numerals chip, help overlay | M1 (audition while editing) |
| **M3 — Groups** | Group model + rack UI, harmonic constructs, hover-prelisten, waveform strip | M1, M2 |
| **M4 — Width editing** | Per-partial noise-ratio editing + halo display | M2 |

Each phase must pass: resynthesis unchanged when no edit performed (bit-compare),
every gesture = exactly one undo step, session save/load round-trips (masks excluded
by design), and no regression of the SMR-restore path (`project_smr_state_save_bug`).

## 5. Non-goals

- No noisc/Kubo stochastic oscillator engine — our synthesis stays ATS
  (`do_synthesis`); q-editing maps to ATS band noise, not OU line shapes.
- No change to analysis, morphing, the transient layer
  (`project_transient_layer_offline_only`), or the .ats/session formats beyond the
  additive group blob.
- No adoption of the noisc color palette — interaction patterns only; visuals follow
  `docs/spectrum-view-overhaul-spec.md`.
- No raster spectral painting (out of scope in noisc too).

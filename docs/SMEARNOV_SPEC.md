# SMEARNOV — Onset Detection & Attack-Smearing Spec

Status: draft · 2026-06-02 · target module `src/core/AtsTransientSmear.{h,cpp}`

Smearnov detects note/transient onsets directly from the ATS model (partials +
residual) and reshapes the **attack** of each onset — blurring, sharpening, or
exaggerating it — with an editable visualization in `SpectrogramPanel`.

---

## 0. Naming / prior art

There is no published algorithm named "smearnov"; it is the project codename. The
DSP is grounded in the canonical onset-detection literature:

- Bello et al., *A Tutorial on Onset Detection in Music Signals* (2005) — detection
  functions, half-wave rectification, adaptive-threshold peak picking.
- Masri — High-Frequency Content (HFC) onset measure.
- Duxbury / Bello — complex-domain (phase-deviation) detection.
- Fitzgerald (2010) HPSS, Huang et al. (2012) RPCA — transient/steady separation,
  referenced only to justify **not** needing SVD here.

### On SVD / RPCA

Skip it. SVD/RPCA's purpose (decompose into a low-rank steady component + a sparse
transient component) is **already performed by ATS analysis**: the deterministic
partials are the steady/tonal part, `band_energy` is the stochastic part. Running
flux/HFC/birth detectors on the already-separated layers is lighter and more
current than re-deriving the split with SVD. RPCA is kept only as an optional
offline "advanced segmentation" fallback (§6), flagged real-time-unsafe.

---

## 1. Inputs (ATS model arrays)

From `ATS_SOUND` (see `atsa/src/atsa/atsa.h`), all `[partial][frame]` /
`[band][frame]`:

| Array | Meaning | Used by |
|-------|---------|---------|
| `amp[p][f]` | partial linear amplitude | D1, smear target |
| `frq[p][f]` | partial frequency (Hz) | D5 phase prediction |
| `pha[p][f]` | partial phase (type 2/4 only) | D5 |
| `smr[p][f]` | signal-to-mask ratio | D1 perceptual weight |
| `band_energy[b][f]` | Bark-band residual energy (25 bands) | D2, D3, smear target |
| `time[p][f]`, `frame_size`, `srate` | frame timing → hop seconds | all |

Frame hop seconds `H = frame_size / srate`. Detection runs at frame rate (one
value per analysis frame), **not** per audio sample.

---

## 2. Detection model

### 2.1 Per-frame detection functions

Five half-wave-rectified streams (rising energy only — the standard onset choice):

| ID | Name | Definition (frame `f`) | Catches |
|----|------|------------------------|---------|
| D1 | Deterministic energy flux | `Σ_p w_p · max(amp[p][f] − amp[p][f−1], 0)`, `w_p` from `smr` | tonal attacks (pluck, mallet) |
| D2 | Residual energy flux | `Σ_b max(be[b][f] − be[b][f−1], 0)` | noisy attacks (pick, breath, stick) |
| D3 | Residual HFC flux | flux of `Σ_b c_b · be[b][f]`, `c_b` = band centre Hz | bright transients |
| D4 | Partial-birth rate | count of tracks going inactive→active at `f` | clean note onsets |
| D5 | Phase deviation *(type 2/4 only)* | `Σ_p \|wrap(pha[p][f] − pha[p][f−1] − 2π·frq[p][f]·H)\|` | soft onsets energy flux misses |

D5 is auto-disabled when the model has no phase (type 1/3).

### 2.2 Normalization & fusion

Each `Dᵢ` is normalized by its running median + MAD over a sliding window so the
streams are commensurable, then fused with the advanced-panel weights:

```
D[f] = α·D1̂ + β·D2̂ + γ·D3̂ + δ·D4̂ + ε·D5̂
```

Defaults: `α=1.0, β=0.8, γ=0.6, δ=0.7, ε=0.4`.

### 2.3 Peak picking (onset localization)

Bello adaptive threshold:

```
thr[f] = median(D[f−w .. f+w]) + λ
onset(f)  ⇔  D[f] is a local max  ∧  D[f] ≥ thr[f]  ∧  (f − lastOnset) ≥ minGapFrames
```

Defaults: `w` ≈ 6 frames, `λ` auto from global MAD, `minGapFrames` ≈ 30 ms / H.

### 2.4 Per-onset attack window

For each onset frame `f0`: scan forward on the smoothed combined energy to the
local post-onset peak `f_peak` (the **attack end**), with a small pre-onset margin
`f_pre = f0 − preMarginFrames`. The window `[f_pre, f_peak]` is the region every
smear operation acts on. Window edges are user-draggable (§4).

---

## 3. The smear transforms

All three reshape `amp[p][f]` and/or `be[b][f]` **inside the attack window only**.
Per the project's morph-fix and DSP-correctness guidance, every operation is
continuous in time and bounded by the signal's own dynamic range (no cliffs →
no clicks/chirps). Phase is never edited — synthesis re-derives it from frequency,
so amplitude/energy reshaping stays artifact-free.

### 3.1 Blur (knob 1)

Convolve the in-window envelopes along `f` with an **energy-normalized Gaussian**
kernel of width = `blurMs`. Spreads/softens the attack; can only *reduce* slope.
Energy-preserving (kernel sums to 1).

### 3.2 Sharpen (knob 2)

Unsharp mask: `amp'[f] = amp[f] + k · (amp[f] − blur(amp)[f])`, `k` from
`sharpenAmount`. Equivalent option: time-warp the rise onto a shorter span,
holding the pre-onset level for the freed time. Steeper attack. Result clamped so
it never exceeds the original window peak unless Exaggerate is also engaged.

### 3.3 Exaggerate (knob 3)

Transient-designer gain: a window-shaped boost peaking at `f_peak`, magnitude from
`exaggerateDb`, **clamped to ±maxDb** (default ±12 dB). Optionally lifts residual
bands in the window too for added punch (governed by the Targets setting).

### 3.4 Interaction

Knobs are independent and applied in order Blur → Sharpen → Exaggerate. Setting a
knob to 0 is a no-op for that stage. Targets selector (partials / residual / both)
and Scope selector (all onsets / display-selected frame range / above-sensitivity)
gate which data and which onsets are affected.

---

## 4. UI — Smearnov takes the morph control area in `SessionTabPanel`

### 4.1 What gets hidden

Today `SessionTabPanel::resized()` (cpp:1535-1592) lays out the morph control
cluster vertically:

- **lane grid** — `morphSmoothingLabel/Slider` (Smooth), `resetMorphLanesButton`
  (Link lanes), and the 2×3 lane grid (`morphLaneLabels/Sliders/LinkedButtons`:
  Freq, Amp, Noise, Time, Transient, Phase, each with an `L` link toggle);
- **policy area** — `morphPolicyLabel`, `morphDiscardOptInButton` (Discard), and
  the 2×4 combo grid (`morphGridTypeCombo`, `morphCorrespondenceCombo`,
  `morphSourcePolicyCombo`, `morphTargetPolicyCombo`, `morphFrequencyDomainCombo`,
  `morphAmplitudeDomainCombo`, `morphPhaseStrategyCombo`, `morphTransientPolicyCombo`);
- **band strip** — `morphBandLabel`, `morphBandStrip`, `morphBandInspectorLabel`.

This entire cluster moves behind a **segmented tab control** at the top of the
region: `[ Morph | Smearnov ]`. The two are mutually exclusive — selecting a tab
shows that workflow's controls in the full region and hides the other's. No morph
behaviour changes; the widgets and `apvts` attachments are identical, only their
visibility toggles. `resized()` lays out whichever tab is active and skips the
other (`setVisible(false)` + zero bounds for the inactive set).

**Why a tab, not a collapsible fold/dropdown** (UX best practice):

- Each workflow gets the full vertical space — the morph cluster (lanes + 8 combos
  + band strip) and the Smearnov panel both need room; stacking them fights for it.
- A segmented control has no layout-jump: the region height is constant regardless
  of selection, so nothing below shifts (a fold/disclosure reflows the panel every
  toggle — an anti-pattern here).
- Matches the actual task model: a user is morphing *or* reshaping attacks, not
  reading both at once. One clear mode beats a busy combined view.
- Selection is sticky (persisted in `apvts`/editor state); **default = Morph** so
  existing users land where they expect. Tab is keyboard-navigable.

### 4.2 What takes its place — the Smearnov panel

The vacated area hosts the Smearnov controls as the primary content:

- **`Smear` macro** header. Expanded body shows:
  - three independent knobs: **Blur** (ms), **Sharpen** (amount), **Exaggerate**
    (dB) — laid out where the lane grid was;
  - **Targets** toggle (Partials / Residual / Both),
  - **Scope** toggle (All / Selection / Sensitivity),
  - **Apply** button (runs detection if stale, then smears scoped onsets),
  - **Advanced ▾**: detection internals — weights `α–ε`, threshold `λ`, peak
    window `w`, `minGapFrames`, pre-margin; plus optional **D5 phase** toggle and
    **RPCA fallback** toggle (§6).
- New members live in `SessionTabPanel` mirroring the morph members'
  declaration/attachment style (`SessionTabPanel.h:185-241`), with `apvts`
  `SliderAttachment`s for the three knobs.

The `Advanced ▾` detection section is itself a small in-tab disclosure (collapsed
by default) so the common case stays to three knobs + Apply.

### 4.3 On-canvas visualization (overlay in `SpectrogramPanel`)

Detection/attack feedback is drawn on the existing partial/residual trace view —
no separate stacked lane:

- onset markers — vertical lines at onset frames,
- attack-window shading — translucent span `[f_pre, f_peak]` per onset,
- before/after ghost — original attack envelope (dashed) vs smeared (solid), reusing
  `paintEnvelopeSurface` machinery,
- optional faint `D[f]` + threshold curve overlaid at the foot of the plot
  (toggle in Advanced), not its own lane.

### 4.4 Editing handles

- drag an onset marker → move `f0`,
- drag a window edge → adjust `f_pre` / `f_peak`,
- double-click marker → delete/disable that onset,
- selection (`selectedResidualFirstFrame/Last`) scopes Smear when Scope = Selection.

---

## 5. Code shape & threading

- **Core**: `AtsTransientSmear.{h,cpp}` — pure functions over the model arrays:
  `detectOnsets(model, params) → std::vector<Onset>` and
  `smearAttacks(model, onsets, params)`. No JUCE, unit-testable.
- **Processor** (mirrors residual ops like `smoothResidualBands`):
  - `bool detectOnsets(AtsTraceSource, const SmearnovDetectParams&, std::vector<AtsOnset>& out, juce::String& error)`
  - `bool smearAttacks(AtsTraceSource, const std::vector<AtsOnset>&, const SmearnovParams&, juce::String& error)`
  - both run on the **worker thread**, bump `modelRevision`, invalidate the preview
    layer cache. Nothing runs in `processBlock`.
- **Snapshot**: extend `AtsSpectralTraceSnapshot` with an `onsets` vector
  (`f0, f_pre, f_peak, strength`) so the panel draws without touching the model.
- **State**: Smearnov params persist via `apvts` / full-state preset.

---

## 6. Optional RPCA fallback (advanced, offline)

If flux detection is too weak on a pathological source, an offline pass can run
robust PCA (low-rank + sparse, inexact ALM) on the `band_energy` matrix to extract
a sparse transient mask feeding D2/D3. Off by default, worker-thread only, never
real-time. Documented as a last resort — normally unnecessary because ATS already
separates the layers.

---

## 7. Validation (numerical, per DSP-correctness bar)

- **Detection accuracy**: F-measure vs hand-labeled onsets at ±50 ms (MIREX) on
  existing fixtures (`plucked_guitar.wav`, crotale/drum hits).
- **Blur energy conservation**: total in-window energy preserved within tolerance;
  assert no introduced per-frame slope exceeds the original max slope.
- **Sharpen/Exaggerate boundedness**: output window peak ≤ clamp bound (±maxDb).
- **Continuity**: no discontinuity introduced at window edges beyond pre-existing
  frame-to-frame deltas (guards against clicks).
- **Phase safety**: confirm `pha` arrays are untouched by all transforms.

---

## 7a. Implementation plan (commit sequence)

Each commit builds and passes tests on its own, in dependency order. Phases A–B
land with zero UI/behaviour change visible to users; the feature becomes reachable
at C9.

### Phase A — Core (pure, no JUCE)

- **C1 · feat: Smearnov onset-detection core**
  `src/core/AtsTransientSmear.{h,cpp}` + `tests/ats_transient_smear_test.cpp`.
  Structs (`AtsOnset`, `SmearnovDetectParams`), detection functions D1–D4, MAD
  normalization + weighted fusion, Bello peak picking, attack-window scan. D5
  stubbed behind a flag. Done when: unit tests cover flux/birth/peak-pick on
  synthetic ramps; detect returns plausible onsets on a fixture buffer.
- **C2 · feat: attack-smear transforms**
  Adds `smearAttacks(...)` + Blur/Sharpen/Exaggerate to the core module; extends
  the test. Done when: blur conserves in-window energy (±tol) and never increases
  slope; sharpen/exaggerate respect the ±maxDb clamp; `pha` proven untouched.
- **C3 · test: detection accuracy + smear invariants on fixtures**
  `tests/ats_transient_smear_accuracy_test.*` using existing audio fixtures
  (`plucked_guitar.wav`, crotale/drum hits) with hand-labeled onsets. Done when:
  F-measure ≥ target at ±50 ms; continuity/boundedness assertions pass.

### Phase B — Processor (worker thread)

- **C4 · feat: wire Smearnov into processor + apvts params**
  `detectOnsets(...)` / `smearAttacks(...)` on `AtsAudioProcessor`, run on the
  worker thread, bump `modelRevision`, invalidate preview cache. Register apvts
  params (3 knob amounts, Targets, Scope, advanced weights/threshold, D5 & RPCA
  flags). Done when: invoking from a test/headless path mutates the model and
  refreshes preview; nothing touches `processBlock`.
- **C5 · feat: carry onset markers in spectral trace snapshot**
  Extend `AtsSpectralTraceSnapshot` with `onsets` (`f0, f_pre, f_peak, strength`);
  populate in `buildSpectralTraceSnapshot`. Done when: snapshot exposes onsets for
  the current/source models without model access from the UI thread.

### Phase C — Display overlay (`SpectrogramPanel`)

- **C6 · feat: draw onset overlay (read-only)**
  Onset markers, attack-window shading, before/after envelope ghost (reusing
  `paintEnvelopeSurface`), optional foot-of-plot `D[f]`+threshold curve (Advanced
  toggle). Done when: overlay renders from snapshot in partial/residual modes.
- **C7 · feat: onset/attack-window editing handles**
  Drag onset, drag window edges, double-click to disable, scope via existing
  `selectedResidualFirstFrame/Last`. Done when: edits update the snapshot/params
  and re-trigger smear on the affected onsets.

### Phase D — Controls (`SessionTabPanel`)

- **C8 · refactor: gate morph cluster behind `Morph | Smearnov` tab**
  Add the segmented tab control; move the existing lane/policy/band cluster under
  the Morph tab; make `resized()` lay out only the active tab; default Morph,
  selection persisted. Pure refactor — **no morph behaviour change**, Smearnov tab
  empty placeholder. Done when: morph UI/automation behaves identically, tab
  switching shows/hides cleanly with no layout jump.
- **C9 · feat: Smearnov control panel**
  Fill the Smearnov tab: `Smear` macro → Blur/Sharpen/Exaggerate knobs,
  Targets/Scope toggles, Apply, `Advanced ▾` disclosure; wire to apvts (C4) and
  `detectOnsets`/`smearAttacks`. Done when: a user can detect + smear end-to-end
  and hear/see the result. **Feature reachable here.**

### Phase E — Optional & polish

- **C10 · feat: persist active tab + Smearnov params in plugin state**
  (Fold into C4/C8 if trivial.) Full-state preset + `getStateInformation`
  round-trip. Done when: reload restores tab and knob values.
- **C11 · feat: offline RPCA onset-segmentation fallback** *(optional)*
  Advanced-panel toggle, worker-thread only, feeds D2/D3. Done when: off by
  default; produces a transient mask on a hard fixture.
- **C12 · docs: document Smearnov**
  Update `COMMANDS.md`, `ATS-Plugin-Vault/Feature_Matrix.md`, `docs/RELEASE.md`.

## 8. Defaults summary

| Param | Default |
|-------|---------|
| Weights α,β,γ,δ,ε | 1.0, 0.8, 0.6, 0.7, 0.4 |
| Peak window `w` | ~6 frames |
| `minGap` | ~30 ms |
| Pre-margin | ~5 ms |
| Blur | 0 ms (off) |
| Sharpen | 0 (off) |
| Exaggerate | 0 dB, clamp ±12 dB |
| Targets / Scope | Both / All |
| D5 phase, RPCA | off |

# Task List: Morph-to-ATS Analysis in Coherent Commits

**Source:** [MORPH_TO_ATS_ANALYSIS_PRD.md](./MORPH_TO_ATS_ANALYSIS_PRD.md)
**How to use:** Each numbered section is intended to become one reviewable commit. Keep commits buildable and testable. If a section grows too large, split it along the same ownership boundary.

---

## Phase 0 - Product and Test Baseline

### Commit 1 - `docs: add morph-to-ATS analysis requirements`

**PRD:** section 1-section 10

- [x] Add the morph-to-ATS PRD and this task list.
- [x] Record the baseline policy: Bark/Mel grid, within-band partial assignment, and frequency/amplitude interpolation.
- [x] Call out the baseline risks: ambiguous correspondence, energy bookkeeping, and phase/time behavior.
- [x] Link the broader modern ATSH PRD and brainstorm note.

Implementation note: Added the morph-to-ATS PRD and coherent commit task list. The PRD records the Bark-first baseline policy, optional Mel path, within-band assignment/interpolation defaults, explicit ambiguity/energy/phase-time risks, and links to the broader modern ATSH PRD plus the spectral morphing brainstorm note.

### Commit 2 - `test: define morph fixture manifest`

**PRD:** section 8.1

- [ ] Define fixture categories: sustained harmonic, vibrato, voice/vowel, inharmonic metallic, plucked/percussive, noisy/breathy, dense/polyphonic.
- [ ] Document fixture provenance, license, sample rate, and expected analysis mode.
- [ ] Add placeholders for source A/source B fixture pairings.

### Commit 3 - `test: add morph metric harness`

**PRD:** section 8.2-section 8.3

- [ ] Add endpoint fidelity checks for morph amount 0 and 1.
- [ ] Add per-band power delta checks across a morph sweep.
- [ ] Add correspondence churn and trajectory smoothness metrics.
- [ ] Record tolerances in one place so later commits can tighten them.

---

## Phase 1 - Recipe Schema and Source Validation

### Commit 4 - `feat(morph): add morph recipe schema`

**PRD:** section 6.1, section 6.11, section 7.2

- [ ] Add recipe version, engine version, source ids, source hashes, and source metadata.
- [ ] Store morph amount defaults and independent lane settings.
- [ ] Keep recipe data separate from source ATS models.
- [ ] Add serialization round-trip tests.

### Commit 5 - `feat(morph): validate source model compatibility`

**PRD:** section 6.1, section 7.1

- [ ] Validate sample rates, frame times, frequency bounds, channel assumptions, and required partial data.
- [ ] Emit warnings for missing phase, missing residual/noise data, or incompatible frame grids.
- [ ] Add deterministic error codes for UI and tests.

### Commit 6 - `feat(morph): persist morph recipe in plugin state`

**PRD:** section 6.11

- [ ] Store recipe metadata and user-selected policies in plugin state.
- [ ] Reference source models by id/hash or embed blobs according to existing state patterns.
- [ ] Preserve unknown future fields where practical.
- [ ] Add reload tests for deterministic recipe reconstruction.

---

## Phase 2 - Perceptual Band Grid

### Commit 7 - `feat(morph): implement Bark band grid`

**PRD:** section 6.2, section 7.2

- [ ] Add deterministic Bark band edge generation.
- [ ] Store grid type, band count, bounds, and edge frequencies in the recipe.
- [ ] Add tests for stable band definitions across platforms.

### Commit 8 - `feat(morph): assign partials to perceptual bands`

**PRD:** section 6.2

- [ ] Assign partials to bands by instantaneous frequency per frame.
- [ ] Add track centroid assignment for diagnostics.
- [ ] Handle out-of-range, silent, and inactive partial frames explicitly.
- [ ] Add tests for boundary behavior near band edges.

### Commit 9 - `feat(morph): add Mel grid option`

**PRD:** section 2, section 6.2, section 9

- [ ] Add Mel band generation behind a non-default option.
- [ ] Store scale choice in recipe metadata.
- [ ] Compare Bark and Mel on fixture metrics before considering Mel for presets.

---

## Phase 3 - Within-Band Correspondence

### Commit 10 - `feat(morph): add frequency-rank correspondence`

**PRD:** section 6.3

- [ ] Pair source A/source B partials by sorted frequency within each band.
- [ ] Store explicit correspondence records in the recipe.
- [ ] Add unmatched records when counts differ.
- [ ] Add tests for stable output on simple fixtures.

### Commit 11 - `feat(morph): add unmatched partial policies`

**PRD:** section 6.3, section 6.5

- [ ] Implement fade out, fade in, hold, merge into residual, and discard policies.
- [ ] Make discard opt-in and visible in recipe metadata.
- [ ] Add tests for each policy with count-mismatch fixtures.

### Commit 12 - `feat(morph): add greedy nearest matching`

**PRD:** section 6.3

- [ ] Match by nearest log-frequency with optional amplitude-rank bias.
- [ ] Add per-match cost diagnostics.
- [ ] Compare against frequency-rank matching on vibrato and inharmonic fixtures.

### Commit 13 - `feat(morph): smooth correspondence over time`

**PRD:** section 6.3, section 8.2

- [ ] Add hysteresis, continuity penalties, or short-window smoothing to reduce frame-to-frame flapping.
- [ ] Emit correspondence churn metrics.
- [ ] Add regression tests for sustained and vibrato fixtures.

### Commit 14 - `feat(morph): prototype assignment matching`

**PRD:** section 6.3, section 9

- [ ] Add an assignment-matrix matcher behind an advanced strategy flag.
- [ ] Use bounded candidate sets to keep CPU predictable.
- [ ] Include log-frequency, amplitude, SMR/confidence, and continuity costs where available.
- [ ] Compare quality and CPU against greedy matching.

---

## Phase 4 - Interpolation and Energy Bookkeeping

### Commit 15 - `feat(morph): interpolate frequency in perceptual domain`

**PRD:** section 6.4

- [ ] Add log-frequency or Bark-coordinate interpolation.
- [ ] Preserve morph endpoints within documented tolerance.
- [ ] Add band-edge migration or clamping behavior.
- [ ] Add tests for monotonic morph sweeps.

### Commit 16 - `feat(morph): interpolate amplitude by power or db`

**PRD:** section 6.4

- [ ] Add power interpolation as the default amplitude policy.
- [ ] Add dB interpolation with a configurable floor.
- [ ] Verify quiet partials do not create level jumps.
- [ ] Add endpoint and sweep tests.

### Commit 17 - `feat(morph): conserve per-band energy`

**PRD:** section 6.5, section 8.3

- [ ] Track source and output sinusoidal power per band.
- [ ] Redistribute merge/split power according to selected unmatched policy.
- [ ] Emit warnings when default-mode band power delta exceeds tolerance.
- [ ] Add tests for 3-to-1 and 1-to-3 partial count mismatches.

### Commit 18 - `feat(morph): add independent morph envelopes`

**PRD:** section 6.4, section 6.7, section 6.9-section 6.10

- [ ] Add independent curves for frequency, amplitude, residual/noise energy, time, and optional phase.
- [ ] Store curve settings in the recipe.
- [ ] Keep one-knob morph behavior as the simple default.
- [ ] Add automation/state tests for curve recall.

---

## Phase 5 - Residual, Phase, and Transients

### Commit 19 - `feat(morph): morph residual and noise lanes`

**PRD:** section 6.7

- [ ] Morph residual/noise band energy separately from sinusoidal partials.
- [ ] Preserve orphan residual energy when available.
- [ ] Add independent gain paths for partial, partial-noise, and orphan residual lanes.
- [ ] Add noisy/breathy fixture tests.

### Commit 20 - `feat(morph): add phase strategy metadata`

**PRD:** section 6.6

- [ ] Add phase strategy choices: oscillator continuation, source-A donor, source-B donor, relative cluster preservation, and direct blend.
- [ ] Default to renderer phase continuity unless tests justify a different default.
- [ ] Add tests for deterministic births, deaths, and endpoint behavior.

### Commit 21 - `feat(morph): fade partial births and deaths`

**PRD:** section 6.6, section 8.3

- [ ] Add configurable birth/death fade windows.
- [ ] Avoid phase resets except at explicit lifecycle boundaries.
- [ ] Add click and zipper checks around unmatched partial transitions.

### Commit 22 - `feat(morph): protect transient windows`

**PRD:** section 6.8

- [ ] Carry transient markers from source models into the recipe.
- [ ] Add transient policies: source A, source B, crossfade buffers, or sustained-only morph after attack.
- [ ] Warn when source onsets are poorly aligned.
- [ ] Add plucked/percussive fixture tests.

---

## Phase 6 - Rendering and Real-Time Integration

### Commit 23 - `feat(morph): render immutable morph snapshots`

**PRD:** section 6.1, section 7.1

- [ ] Evaluate a recipe at a morph amount and produce an immutable model snapshot.
- [ ] Keep rendering off the audio thread unless it is proven bounded and allocation-free.
- [ ] Add deterministic offline render tests.

### Commit 24 - `feat(synth): play prepared morph snapshots`

**PRD:** section 7.1, section 8.3

- [ ] Route prepared snapshots through the existing synthesis path.
- [ ] Confirm audio-thread safety: no allocation, locks, file I/O, logging, or analysis.
- [ ] Add stress tests for automation changes while snapshots are swapped.

### Commit 25 - `perf(morph): profile morph analysis and snapshot costs`

**PRD:** section 8.2-section 8.3

- [ ] Measure CPU/memory for representative partial and frame counts.
- [ ] Document soft caps and UI warning thresholds.
- [ ] Compare frequency-rank, greedy, and assignment strategies.

---

## Phase 7 - UI and Presets

### Commit 26 - `feat(ui): add morph source slots and amount control`

**PRD:** section 6.9-section 6.10.2

- [ ] Add source A/source B slots.
- [ ] Show model name, duration, sample rate, partial count, residual/noise availability, source gain, and time offset.
- [ ] Add compact source spectrogram or partial-track overview with transient markers when available.
- [ ] Add source actions for analyze/replace, normalize, listen source A, listen source B, and future time anchors.
- [ ] Expose morph amount as a host-automatable parameter.
- [ ] Show source hashes or names in saved state diagnostics.

### Commit 27 - `feat(ui): add central morph macro and lanes`

**PRD:** section 6.9-section 6.10.2

- [ ] Add a large morph knob or horizontal macro where 0 is source A, 0.5 is midpoint, and 1 is source B.
- [ ] Add optional XY pad support with X mapped to A/B morph and Y mapped to a performance macro.
- [ ] Add lane controls for frequency, amplitude, residual/noise, time, transient, and optional phase.
- [ ] Add linked/unlinked lane states so lanes can follow the main morph amount or move independently.
- [ ] Add smoothing and reset-to-linked behavior for live-safe controls.

### Commit 28 - `feat(ui): expose morph policy controls`

**PRD:** section 6.9-section 6.10.4

- [ ] Expose grid type, correspondence strategy, unmatched policy, interpolation domains, and phase strategy.
- [ ] Expose transient policy: source A attack, source B attack, crossfade attacks, and sustained-only morph after attack.
- [ ] Keep discard policy behind an explicit expert choice.
- [ ] Keep defaults simple and visible.
- [ ] Place advanced options behind an expert panel if needed.

### Commit 29 - `feat(ui): add perceptual band strip and inspector`

**PRD:** section 6.9-section 6.10.4, section 8.2

- [ ] Add a horizontal Bark/Mel band strip.
- [ ] Show source A energy, source B energy, current morphed energy, matched count, unmatched count, and warning state per band.
- [ ] Open a selected-band inspector when a band is clicked.
- [ ] Show correspondence pairs, unmatched partials, selected policy, match costs when available, and lifecycle state around births/deaths.
- [ ] Display matched/unmatched partial counts per band.
- [ ] Display energy delta and warning bands.
- [ ] Display transient alignment warnings.
- [ ] Keep diagnostics generation off the audio thread.

### Commit 30 - `feat(ui): add morph snapshots and live rebuild states`

**PRD:** section 6.9-section 6.10.5, section 6.11

- [ ] Add quick snapshot buttons for A, 25%, 50%, 75%, and B.
- [ ] Add user variation slots when preset/state support is available.
- [ ] Store lane link states, performance macro settings, and snapshot slots in plugin state.
- [ ] Keep the last valid snapshot playing while expensive policy changes rebuild in the background.
- [ ] Show stale, rebuilding, ready, and warning states.

### Commit 31 - `feat(presets): add morph analysis presets`

**PRD:** section 5, section 6.9

- [ ] Add stable default preset for broad source material.
- [ ] Add tonal, vocal/formant, percussive, and noisy texture presets.
- [ ] Store preset metadata explaining selected policies.

---

## Dependency Index

| Concern | First commit |
| --- | --- |
| Product requirements | 1 |
| Fixture and metrics baseline | 2-3 |
| Serializable recipe | 4 |
| Source compatibility | 5 |
| Bark/Mel grid | 7-9 |
| Within-band correspondence | 10-14 |
| Interpolation and energy conservation | 15-18 |
| Residual/noise morphing | 19 |
| Phase/time behavior | 20-21 |
| Transient protection | 22 |
| Render snapshots and synthesis | 23-24 |
| UI exposure | 26-31 |

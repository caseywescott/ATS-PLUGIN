# Task List: Modern ATSH Engine in Coherent Commits

**Source:** [MODERN_ATSH_PRD.md](./MODERN_ATSH_PRD.md)
**How to use:** Each section is intended to become one reviewable commit. Keep commits buildable and testable. If a section proves too large, split it only along the same ownership boundary.

---

## Phase 0 - Research Lockdown and Compatibility Baseline

### Commit 1 - `docs: add modern ATSH engine requirements`

**PRD:** section 1-section 9

- [ ] Add/maintain this PRD and task list.
- [ ] Record the source-review decisions: classic mode retained, modern tracker optional, transient preservation first, orphan residual energy retained.
- [ ] Link references used to justify the modernization plan.

### Commit 2 - `test: add ATS compatibility fixture manifest`

**PRD:** section 7.1-section 7.2

- [ ] Define fixture categories: sustained, vibrato, close/crossing partials, plucked/percussive, noisy voice/flute, dense polyphony, percussive hit.
- [ ] Document fixture provenance and license.
- [ ] Add expected metrics: reconstruction error, spectral distance, track continuity, transient flux, residual energy preservation.

### Commit 3 - `test: preserve classic analyzer and synth parity`

**PRD:** section 5.1, section 6.3, section 7.1

- [ ] Add reference renders or tolerance thresholds against current ATSA/Qatsh-compatible behavior.
- [ ] Verify classic mode serializes enough metadata to reproduce a run.
- [ ] Make classic ATS the default analysis mode until modern mode passes acceptance criteria.

---

## Phase 1 - Data Model and Serialization

### Commit 4 - `feat(model): version modern ATS model schema`

**PRD:** section 6.2-section 6.3

- [ ] Add model header fields: schema version, analysis mode, tracker mode, sample rate, hop/window, parameter hash, source hash.
- [ ] Preserve existing partial frame data: amplitude, frequency, phase, SMR.
- [ ] Add optional fields without breaking old serialized state readers.

### Commit 5 - `feat(model): store analysis diagnostics and track lifecycle`

**PRD:** section 5.3, section 5.8, section 6.2

- [ ] Store confidence and reason codes: matched, born, slept, interpolated, pruned, rejected.
- [ ] Store track lifecycle events for visualization and debugging.
- [ ] Add tests for deterministic serialization of diagnostics.

### Commit 6 - `feat(model): represent orphan residual and transient markers`

**PRD:** section 5.4-section 5.5, section 6.2

- [ ] Add per-frame orphan residual band energy.
- [ ] Add transient markers with onset time, window bounds, and gain.
- [ ] Confirm plugin state size impact for representative fixtures.

---

## Phase 2 - Analysis Pipeline

### Commit 7 - `feat(analysis): introduce analysis mode registry`

**PRD:** section 5.1, section 6.1

- [ ] Add Classic ATS, Modern ATS, and Experimental mode identifiers.
- [ ] Route existing analysis through Classic ATS mode.
- [ ] Persist analysis mode and parameters in model metadata.

### Commit 8 - `feat(analysis): isolate peak estimation behind an interface`

**PRD:** section 5.2

- [ ] Extract current STFT peak picking/interpolation behind a deterministic interface.
- [ ] Add diagnostics for raw peaks, accepted peaks, rejected peaks, and thresholds.
- [ ] Keep current behavior unchanged in Classic ATS mode.

### Commit 9 - `feat(tracking): isolate classic ATS tracker`

**PRD:** section 5.3

- [ ] Wrap the existing frequency/SMR recursive tracker as the Classic tracker.
- [ ] Normalize track lifecycle output for shared downstream processing.
- [ ] Add tests for short-gap fill and short-segment pruning.

### Commit 10 - `feat(tracking): add assignment tracker prototype`

**PRD:** section 5.3, section 7.3

- [ ] Implement a Modern tracker using a cost matrix with frequency, amplitude, SMR, birth/death, and spurious-peak terms.
- [ ] Use a bounded candidate set per frame to control CPU.
- [ ] Compare against Classic tracker on crossing/close-partial fixtures.

### Commit 11 - `feat(analysis): add modern peak continuity features`

**PRD:** section 5.2-section 5.3

- [ ] Add optional phase/frequency-slope or short-term model continuity inputs to Modern tracker costs.
- [ ] Record cost breakdowns for debugging.
- [ ] Guard feature extraction behind Modern mode.

### Commit 12 - `feat(residual): preserve orphan critical-band noise`

**PRD:** section 5.4, section 7.3

- [ ] During residual distribution, keep band energy that has no active partial target.
- [ ] Add synthesis path for orphan band noise.
- [ ] Test residual energy preservation on noisy/percussive fixtures.

### Commit 13 - `feat(transient): detect and mark onset windows`

**PRD:** section 5.5

- [ ] Add onset detection using spectral flux/energy and model discontinuity cues.
- [ ] Store transient markers in the model.
- [ ] Expose diagnostics for threshold tuning.

### Commit 14 - `feat(transient): protect attacks during analysis`

**PRD:** section 5.5, section 7.3

- [ ] Break/reset track interpolation around strong transient windows.
- [ ] Avoid filling gaps across marked attacks unless explicitly allowed.
- [ ] Verify plucked/percussive fixtures improve attack preservation.

---

## Phase 3 - Synthesis

### Commit 15 - `feat(synth): render modern model snapshots`

**PRD:** section 5.7, section 6.1

- [ ] Extend synthesis to read versioned modern snapshots.
- [ ] Maintain classic render path parity.
- [ ] Keep audio-thread safety checks: no allocation, locks, file I/O, or analysis.

### Commit 16 - `feat(synth): add orphan residual-band renderer`

**PRD:** section 5.4, section 5.7

- [ ] Synthesize orphan residual bands independently from partial-modulated noise.
- [ ] Expose gain path for orphan residual/noise.
- [ ] Add deterministic noise seed handling for offline tests.

### Commit 17 - `feat(synth): add transient buffer playback and crossfades`

**PRD:** section 5.5, section 5.7

- [ ] Store or reference short transient buffers/grains from analysis.
- [ ] Mix transients with sinusoidal/noise synthesis using click-free crossfades.
- [ ] Add host-automation-safe transient gain.

### Commit 18 - `perf(synth): profile modern synthesis budgets`

**PRD:** section 7.2-section 7.3

- [ ] Measure CPU/memory for representative partial/frame counts at 48 kHz / 256 samples.
- [ ] Document soft caps and guardrail thresholds.
- [ ] Add regression checks for denormals and silence-edge behavior.

---

## Phase 4 - Morphing Engine

### Commit 19 - `feat(morph): add model-domain morph recipe`

**PRD:** section 5.6, section 6.2

- [ ] Add serializable morph recipe with source model ids and envelopes.
- [ ] Keep recipe separate from source models.
- [ ] Add snapshot tests for state recall.

### Commit 20 - `feat(morph): implement partial correspondence maps`

**PRD:** section 5.6

- [ ] Store explicit labels and source-to-target correspondence maps.
- [ ] Support unmatched source/target partial policies: fade out, fade in, nearest, hold, discard.
- [ ] Add tests for stable correspondence serialization.

### Commit 21 - `feat(morph): add automatic correspondence strategies`

**PRD:** section 5.6

- [ ] Implement harmonic-number matching when F0/harmonicity is reliable.
- [ ] Implement nearest-frequency matching with slope/SMR constraints.
- [ ] Add fallback handling for non-harmonic or polyphonic sources.

### Commit 22 - `feat(morph): interpolate frequency amplitude and noise envelopes`

**PRD:** section 5.6-section 5.7

- [ ] Add independent morph envelopes for frequency, amplitude, partial noise, orphan residual, and time.
- [ ] Preserve phase sanity at births/deaths.
- [ ] Verify renders are deterministic and click-free under automation.

### Commit 23 - `feat(morph): add transient-aware morph controls`

**PRD:** section 5.5-section 5.6

- [ ] Add transient timing/source/gain morph controls.
- [ ] Avoid smearing attacks when morphing percussive and sustained sources.
- [ ] Add listening-test notes or fixture metrics for transient morphs.

---

## Phase 5 - UI and Plugin Integration

### Commit 24 - `feat(ui): expose analysis and tracker mode selection`

**PRD:** section 5.8

- [ ] Add Classic ATS / Modern ATS selection.
- [ ] Add tracker choice when Modern mode is active.
- [ ] Display warnings when mode choice changes compatibility expectations.

### Commit 25 - `feat(ui): show modern model diagnostics`

**PRD:** section 5.8

- [ ] Visualize partial count, frame count, transient markers, orphan residual energy, and analysis warnings.
- [ ] Show track lifecycle states in the partial/spectrogram view.
- [ ] Keep diagnostics off the audio thread.

### Commit 26 - `feat(ui): add A/B morph workspace`

**PRD:** section 5.6, section 5.8

- [ ] Add two source slots and morph recipe selection.
- [ ] Show correspondence status and unmatched partial counts.
- [ ] Expose independent morph envelopes as host-automatable parameters where appropriate.

### Commit 27 - `feat(presets): add modern analysis presets`

**PRD:** section 5.8

- [ ] Add presets for voice, sustained instrument, plucked/percussive, noisy texture, and polyphonic material.
- [ ] Store preset metadata explaining mode/tracker choices.
- [ ] Add migration behavior for older sessions.

---

## Phase 6 - Experimental and Future Work

### Commit 28 - `feat(experimental): add reassigned or DDM peak estimator`

**PRD:** section 5.1-section 5.2, section 8

- [ ] Add estimator behind Experimental mode only.
- [ ] Compare analysis quality and CPU against Modern mode.
- [ ] Document when users should prefer it.

### Commit 29 - `feat(experimental): prototype full S+T+N transient lane`

**PRD:** section 5.5, section 8

- [ ] Prototype compact transient representation separate from preservation buffers.
- [ ] Compare DCT/TMS-style transient synthesis with stored-buffer preservation.
- [ ] Decide whether it should graduate to Modern mode.

### Commit 30 - `research: evaluate DDSP-assisted analysis helper`

**PRD:** section 2, section 3.2, section 8

- [ ] Keep the classical model as the editable source of truth.
- [ ] Evaluate learned helpers only for parameter estimation, denoising, or preset suggestion.
- [ ] Do not add a runtime dependency until licensing, model size, and determinism are resolved.

---

## Dependency Index

| Concern | First commit |
| --- | --- |
| Classic ATS parity | 3 |
| Versioned modern model | 4 |
| Assignment-capable tracking | 10 |
| Orphan residual energy | 6, then 12 and 16 |
| Transient preservation | 6, then 13-14 and 17 |
| Morph recipes and correspondence | 19-21 |
| Plugin UI exposure | 24-27 |
| Experimental reassignment/STN/DDSP | 28-30 |

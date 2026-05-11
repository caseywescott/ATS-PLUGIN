# Task List: Modern ATSH Engine in Coherent Commits

**Source:** [MODERN_ATSH_PRD.md](./MODERN_ATSH_PRD.md)
**How to use:** Each section is intended to become one reviewable commit. Keep commits buildable and testable. If a section proves too large, split it only along the same ownership boundary.

---

## Phase 0 - Research Lockdown and Compatibility Baseline

### Commit 1 - `docs: add modern ATSH engine requirements`

**PRD:** section 1-section 9

- [x] Add/maintain this PRD and task list.
- [x] Record the source-review decisions: classic mode retained, modern tracker optional, transient preservation first, orphan residual energy retained.
- [x] Link references used to justify the modernization plan.

Phase 0 baseline notes live in [ATSH_COMPATIBILITY_BASELINE.md](./ATSH_COMPATIBILITY_BASELINE.md).

### Commit 2 - `test: add ATS compatibility fixture manifest`

**PRD:** section 7.1-section 7.2

- [x] Define fixture categories: sustained, vibrato, close/crossing partials, plucked/percussive, noisy voice/flute, dense polyphony, percussive hit.
- [x] Document fixture provenance and license.
- [x] Add expected metrics: reconstruction error, spectral distance, track continuity, transient flux, residual energy preservation.

The manifest is [tests/fixtures/ats_compat_manifest.json](../tests/fixtures/ats_compat_manifest.json) and is validated by the `ats_fixture_manifest` CTest.

### Commit 3 - `test: preserve classic analyzer and synth parity`

**PRD:** section 5.1, section 6.3, section 7.1

- [x] Add reference renders or tolerance thresholds against current ATSA/Qatsh-compatible behavior.
- [x] Verify classic mode serializes enough metadata to reproduce a run.
- [x] Make classic ATS the default analysis mode until modern mode passes acceptance criteria.

The baseline is enforced by `golden_offline`, `ats_roundtrip`, and `classic_compat_baseline`. `ats_offline --metadata <analysis.json>` records `classic_ats` / `classic_recursive_frequency_smr`, source hash, input dimensions, `ANARGS`, computed analysis fields, and model summary fields.

---

## Phase 1 - Data Model and Serialization

### Commit 4 - `feat(model): version modern ATS model schema`

**PRD:** section 6.2-section 6.3

- [x] Add model header fields: schema version, analysis mode, tracker mode, sample rate, hop/window, parameter hash, source hash.
- [x] Preserve existing partial frame data: amplitude, frequency, phase, SMR.
- [x] Add optional fields without breaking old serialized state readers.

Implemented in `src/core/AtsModernModel.h` / `.cpp`; schema notes are in [MODERN_ATSH_MODEL_SCHEMA.md](./MODERN_ATSH_MODEL_SCHEMA.md). The classic plugin state reader remains unchanged while the modern snapshot stays optional.

### Commit 5 - `feat(model): store analysis diagnostics and track lifecycle`

**PRD:** section 5.3, section 5.8, section 6.2

- [x] Store confidence and reason codes: matched, born, slept, interpolated, pruned, rejected.
- [x] Store track lifecycle events for visualization and debugging.
- [x] Add tests for deterministic serialization of diagnostics.

Covered by the `ats_modern_model` CTest.

### Commit 6 - `feat(model): represent orphan residual and transient markers`

**PRD:** section 5.4-section 5.5, section 6.2

- [x] Add per-frame orphan residual band energy.
- [x] Add transient markers with onset time, window bounds, and gain.
- [x] Confirm plugin state size impact for representative fixtures.

The `ats_modern_model` CTest validates optional empty fields, 25-band orphan residual payloads, transient markers, and representative serialized state-size budgets.

---

## Phase 2 - Analysis Pipeline

### Commit 7 - `feat(analysis): introduce analysis mode registry`

**PRD:** section 5.1, section 6.1

- [x] Add Classic ATS, Modern ATS, and Experimental mode identifiers.
- [x] Route existing analysis through Classic ATS mode.
- [x] Persist analysis mode and parameters in model metadata.

Implemented in `src/core/AtsAnalysisPipeline.h` / `.cpp`; details are in [MODERN_ATSH_ANALYSIS_PIPELINE.md](./MODERN_ATSH_ANALYSIS_PIPELINE.md). The plugin analysis worker now calls `ats::runClassicAtsAnalysis`; `ats_offline --analysis-mode classic_ats --metadata <analysis.json>` persists mode, tracker, source hash, parameter hash, and `ANARGS`.

### Commit 8 - `feat(analysis): isolate peak estimation behind an interface`

**PRD:** section 5.2

- [x] Extract current STFT peak picking/interpolation behind a deterministic interface.
- [x] Add diagnostics for raw peaks, accepted peaks, rejected peaks, and thresholds.
- [x] Keep current behavior unchanged in Classic ATS mode.

Covered by `ats::IPeakEstimator`, `ats::ClassicStftPeakEstimator`, and the `ats_analysis_pipeline` CTest. Classic ATS still uses the original ATSA tracker path.

### Commit 9 - `feat(tracking): isolate classic ATS tracker`

**PRD:** section 5.3

- [x] Wrap the existing frequency/SMR recursive tracker as the Classic tracker.
- [x] Normalize track lifecycle output for shared downstream processing.
- [x] Add tests for short-gap fill and short-segment pruning.

### Commit 10 - `feat(tracking): add assignment tracker prototype`

**PRD:** section 5.3, section 7.3

- [x] Implement a Modern tracker using a cost matrix with frequency, amplitude, SMR, birth/death, and spurious-peak terms.
- [x] Use a bounded candidate set per frame to control CPU.
- [x] Compare against Classic tracker on crossing/close-partial fixtures.

### Commit 11 - `feat(analysis): add modern peak continuity features`

**PRD:** section 5.2-section 5.3

- [x] Add optional phase/frequency-slope or short-term model continuity inputs to Modern tracker costs.
- [x] Record cost breakdowns for debugging.
- [x] Guard feature extraction behind Modern mode.

### Commit 12 - `feat(residual): preserve orphan critical-band noise`

**PRD:** section 5.4, section 7.3

- [x] During residual distribution, keep band energy that has no active partial target.
- [x] Add synthesis path for orphan band noise.
- [x] Test residual energy preservation on noisy/percussive fixtures.

### Commit 13 - `feat(transient): detect and mark onset windows`

**PRD:** section 5.5

- [x] Add onset detection using spectral flux/energy and model discontinuity cues.
- [x] Store transient markers in the model.
- [x] Expose diagnostics for threshold tuning.

### Commit 14 - `feat(transient): protect attacks during analysis`

**PRD:** section 5.5, section 7.3

- [x] Break/reset track interpolation around strong transient windows.
- [x] Avoid filling gaps across marked attacks unless explicitly allowed.
- [x] Verify plucked/percussive fixtures improve attack preservation.

`ats_analysis_pipeline` covers protected attack frames: short-gap interpolation is blocked across transient frames, and the assignment prototype resets/births attack-side tracks on plucked/percussive-style transient breaks.

---

## Phase 3 - Synthesis

### Commit 15 - `feat(synth): render modern model snapshots`

**PRD:** section 5.7, section 6.1

- [x] Extend synthesis to read versioned modern snapshots.
- [x] Maintain classic render path parity.
- [x] Keep audio-thread safety checks: no allocation, locks, file I/O, or analysis.

Implemented in `src/core/AtsModernSynth.h` / `.cpp`; details and budgets are in [MODERN_ATSH_SYNTHESIS.md](./MODERN_ATSH_SYNTHESIS.md). The classic ATSA/Qatsh render path remains unchanged and covered by existing parity tests.

### Commit 16 - `feat(synth): add orphan residual-band renderer`

**PRD:** section 5.4, section 5.7

- [x] Synthesize orphan residual bands independently from partial-modulated noise.
- [x] Expose gain path for orphan residual/noise.
- [x] Add deterministic noise seed handling for offline tests.

### Commit 17 - `feat(synth): add transient buffer playback and crossfades`

**PRD:** section 5.5, section 5.7

- [x] Store or reference short transient buffers/grains from analysis.
- [x] Mix transients with sinusoidal/noise synthesis using click-free crossfades.
- [x] Add host-automation-safe transient gain.

### Commit 18 - `perf(synth): profile modern synthesis budgets`

**PRD:** section 7.2-section 7.3

- [x] Measure CPU/memory for representative partial/frame counts at 48 kHz / 256 samples.
- [x] Document soft caps and guardrail thresholds.
- [x] Add regression checks for denormals and silence-edge behavior.

Covered by the `ats_modern_synth` CTest.

---

## Phase 4 - Morphing Engine

### Commit 19 - `feat(morph): add model-domain morph recipe`

**PRD:** section 5.6, section 6.2

- [x] Add serializable morph recipe with source model ids and envelopes.
- [x] Keep recipe separate from source models.
- [x] Add snapshot tests for state recall.

Implemented in `src/core/AtsModernMorph.h` / `.cpp`; details are in [MODERN_ATSH_MORPHING.md](./MODERN_ATSH_MORPHING.md).

### Commit 20 - `feat(morph): implement partial correspondence maps`

**PRD:** section 5.6

- [x] Store explicit labels and source-to-target correspondence maps.
- [x] Support unmatched source/target partial policies: fade out, fade in, nearest, hold, discard.
- [x] Add tests for stable correspondence serialization.

### Commit 21 - `feat(morph): add automatic correspondence strategies`

**PRD:** section 5.6

- [x] Implement harmonic-number matching when F0/harmonicity is reliable.
- [x] Implement nearest-frequency matching with slope/SMR constraints.
- [x] Add fallback handling for non-harmonic or polyphonic sources.

### Commit 22 - `feat(morph): interpolate frequency amplitude and noise envelopes`

**PRD:** section 5.6-section 5.7

- [x] Add independent morph envelopes for frequency, amplitude, partial noise, orphan residual, and time.
- [x] Preserve phase sanity at births/deaths.
- [x] Verify renders are deterministic and click-free under automation.

### Commit 23 - `feat(morph): add transient-aware morph controls`

**PRD:** section 5.5-section 5.6

- [x] Add transient timing/source/gain morph controls.
- [x] Avoid smearing attacks when morphing percussive and sustained sources.
- [x] Add listening-test notes or fixture metrics for transient morphs.

The `ats_modern_morph` CTest records morph fixture metrics for output partials, frames, transient markers, protected attack-window duration, and orphan residual energy.

---

## Phase 5 - UI and Plugin Integration

### Commit 24 - `feat(ui): expose analysis and tracker mode selection`

**PRD:** section 5.8

- [x] Add Classic ATS / Modern ATS selection.
- [x] Add tracker choice when Modern mode is active.
- [x] Display warnings when mode choice changes compatibility expectations.

### Commit 25 - `feat(ui): show modern model diagnostics`

**PRD:** section 5.8

- [x] Visualize partial count, frame count, transient markers, orphan residual energy, and analysis warnings.
- [x] Show track lifecycle states in the partial/spectrogram view (lifecycle event overlay lines on spectrogram; birth=green, death=red, gap-fill=amber).
- [x] Keep diagnostics off the audio thread.

### Commit 26 - `feat(ui): add A/B morph workspace`

**PRD:** section 5.6, section 5.8

- [x] Add two source slots and morph recipe selection.
- [x] Show correspondence status and unmatched partial counts (shown in diagnostics label via getModernDiagnosticsSummary).
- [x] Expose independent morph envelopes as host-automatable parameters where appropriate.

### Commit 27 - `feat(presets): add modern analysis presets`

**PRD:** section 5.8

- [x] Add presets for voice, sustained instrument, plucked/percussive, noisy texture, and polyphonic material.
- [x] Store preset metadata explaining mode/tracker choices.
- [x] Add migration behavior for older sessions.

---

## Phase 6 - Experimental and Future Work

### Commit 28 - `feat(experimental): add reassigned or DDM peak estimator`

**PRD:** section 5.1-section 5.2, section 8

- [x] Add estimator behind Experimental mode only.
- [x] Compare analysis quality and CPU against Modern mode.
- [x] Document when users should prefer it.

### Commit 29 - `feat(experimental): prototype full S+T+N transient lane`

**PRD:** section 5.5, section 8

- [x] Prototype compact transient representation separate from preservation buffers.
- [x] Compare DCT/TMS-style transient synthesis with stored-buffer preservation.
- [x] Decide whether it should graduate to Modern mode.

### Commit 30 - `research: evaluate DDSP-assisted analysis helper`

**PRD:** section 2, section 3.2, section 8

- [x] Keep the classical model as the editable source of truth.
- [x] Evaluate learned helpers only for parameter estimation, denoising, or preset suggestion.
- [x] Do not add a runtime dependency until licensing, model size, and determinism are resolved.

---

## Phase 7 - Quality Gates and Release Acceptance

### Commit 31 - `test(acceptance): add modern ATSH quality gate harness`

**PRD:** section 7.1-section 7.3

- [x] Add a reusable acceptance report with stable check ids, observed values, thresholds, and failure messages.
- [x] Measure classic parity, tracker continuity/identity, transient preservation, residual energy, morph determinism, and synth RT safety.
- [x] Add deterministic CI fixtures that exercise the PRD acceptance criteria without external audio assets.

Implemented in `src/core/AtsModernAcceptance.h` / `.cpp` and covered by the `ats_modern_acceptance` CTest. Notes are in [MODERN_ATSH_ACCEPTANCE.md](./MODERN_ATSH_ACCEPTANCE.md).

### Commit 32 - `test(acceptance): extend acceptance gate for real-audio ATSA comparison`

**PRD:** section 7.1-section 7.3

- [x] Add external fixture manifest (`tests/fixtures/ats_external_fixture_manifest.json`) with schema version 1, per-fixture provenance/license, enabled gates, and ATSA reference artifact paths.
- [x] Add reference generation script (`tests/generate_atsa_references.py`) that runs `ats_offline --metadata` on fixture audio, writes stable sorted JSON artifacts, and fails loudly on missing assets or unresolved license fields.
- [x] Extend `AtsModernAcceptance.h/.cpp` with `ExternalFixtureReferenceMetrics`, `ExternalFixtureThresholds`, `ExternalFixtureGate`, `parseExternalFixtureGate`, `loadExternalFixtureReferenceMetrics`, and `evaluateExternalFixtureGates` for per-fixture, per-gate comparison with fixture id in every check id.
- [x] Add `ats_modern_acceptance_external_test` C++ binary (always builds, exercises gate logic with synthetic data, registered as `ats_modern_acceptance_external_gates` CTest).
- [x] Add `run_external_acceptance.py` Python CTest driver that loads the manifest, checks for audio and reference assets, runs `ats_offline`, and compares against reference metrics per enabled gate.
- [x] Gate the real-audio CTest (`ats_modern_acceptance_external`) behind `ATS_ENABLE_EXTERNAL_FIXTURES=OFF` (default) so default `ctest` remains fully deterministic.
- [x] Five placeholder fixtures defined: sustained_flute, plucked_guitar, sung_vowel, drum_hit, crossing_strings — all with PENDING license status pending audio procurement.

Notes and gate documentation are in [MODERN_ATSH_ACCEPTANCE.md](./MODERN_ATSH_ACCEPTANCE.md).

---

## Phase 8 - Risk Closure and Release Policy

### Commit 33 - `feat(model): record modern ATS channel policy`

**PRD:** section 5.1, section 6.2, section 8

- [x] Decide the MVP stereo/multichannel policy: mono input is analyzed directly; stereo or wider input is averaged into a mono ATS model.
- [x] Persist source input channel count and channel policy in modern model metadata.
- [x] Validate channel-policy consistency and document reserved future per-channel/mid-side modes.

Implemented in `src/core/AtsModernModel.h` / `.cpp`; the plugin records imported WAV channel count before launching analysis. Notes are in [MODERN_ATSH_RISK_CLOSURE.md](./MODERN_ATSH_RISK_CLOSURE.md).

### Commit 34 - `fix(analysis): make Modern ATS analyze source peaks directly`

**PRD:** section 5.1-section 5.3, section 6.1, section 7.3

- [x] Close the unfinished architecture gap: Modern ATS plugin analysis no longer derives partial tracks from already-tracked classic `ATS_SOUND` cells.
- [x] Add `ats::runModernAtsAnalysis` as a core audio-to-modern-model path with deterministic STFT peak estimation, peak diagnostics, source-audio hashing, transient markers, and assignment tracking.
- [x] Keep Classic ATS available for compatibility, ATS export, and residual-layer rendering while Modern ATS partials improve independently.
- [x] Add a deterministic CTest proving direct modern analysis tracks a source sine peak, preserves original channel-count metadata, records assignment costs, and validates the modern model.

### Commit 35 - `test(analysis): prevent classic-derived modern regression`

**PRD:** section 5.1-section 5.3, section 6.1

- [x] Remove the stale plugin fallback that converted classic `ATS_SOUND` partial cells into a Modern ATS model.
- [x] Keep the classic ATSA output scoped to compatibility/export and residual-band overlay only.
- [x] Add a deterministic source-path CTest that fails if the plugin reintroduces classic-derived Modern ATS helper paths or warnings.

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
| Quality and release acceptance | 31 |
| External real-audio ATSA comparison | 32 |
| Stereo/multichannel policy | 33 |
| Direct modern source analysis | 34 |
| Modern source-path regression guard | 35 |

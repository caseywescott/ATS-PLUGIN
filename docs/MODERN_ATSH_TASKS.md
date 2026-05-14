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
- [x] First-party fixture support defined for external real-audio gates; initial placeholders were later replaced with committed Casey Wescott samples.

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
- [x] Keep Classic ATS available for compatibility, ATS export, and classic preview/render fallback while Modern ATS partials improve independently.
- [x] Add a deterministic CTest proving direct modern analysis tracks a source sine peak, preserves original channel-count metadata, records assignment costs, and validates the modern model.

### Commit 35 - `test(analysis): prevent classic-derived modern regression`

**PRD:** section 5.1-section 5.3, section 6.1

- [x] Remove the stale plugin fallback that converted classic `ATS_SOUND` partial cells into a Modern ATS model.
- [x] Keep the classic ATSA output scoped to compatibility/export and residual-band overlay only.
- [x] Add a deterministic source-path CTest that fails if the plugin reintroduces classic-derived Modern ATS helper paths or warnings.

### Commit 36 - `feat(transient): capture direct-analysis transient buffers`

**PRD:** section 5.5, section 5.7, section 7.3

- [x] Populate `ModernAtsTransientBuffer` payloads from bounded source-audio windows when direct Modern ATS analysis marks transients.
- [x] Wire transient marker `bufferIndex` values to matching source buffers so modern synthesis can preserve attacks.
- [x] Add a deterministic CTest proving an impulse-style source transient produces a valid marker/buffer pair containing the source attack.

### Commit 37 - `fix(synth): render captured transients in Modern preview`

**PRD:** section 5.5, section 5.7, section 6.1

- [x] Enable the modern transient-buffer lane in plugin Modern/Experimental preview rendering.
- [x] Keep classic ATSA residual rendering as the residual layer while routing captured attacks through the modern partial layer.
- [x] Extend the source-path CTest guard so plugin preview cannot silently mute captured transient buffers again.

### Commit 38 - `fix(analysis): close modern source-analysis quality gaps`

**PRD:** section 5.1-section 5.5, section 6.1, section 7.3

- [x] Scale modern transient-buffer start times and playback durations with the synth `timeFactor` so preserved attacks stay aligned with stretched or compressed partial renders.
- [x] Replace the direct-analysis per-bin DFT with an in-tree FFT-backed spectrum pass, keeping Modern ATS source analysis bounded by window-size FFT cost rather than bins multiplied by window samples.
- [x] Add deterministic direct-source acceptance coverage that runs `ats::runModernAtsAnalysis` on a two-tone audio fixture, validates sustained 512 Hz and 768 Hz tracks, renders the resulting model, and checks finite audible output.
- [x] Guard plugin editor auto-fit sizing so small displays cannot invert JUCE `jlimit` or resize-limit bounds.

### Commit 39 - `fix(ui): stabilize diagnostics layout refresh`

**PRD:** section 5.8, section 7.3

- [x] Keep raw status/diagnostic/warning strings separate from wrapped label text so resize-driven wrapping does not fight timer refreshes.
- [x] Size Modern/Classic diagnostics, compatibility warnings, preset metadata, and CPU warnings from wrapped line counts instead of clipping them to one fixed short row.
- [x] Stop forcing a full `SessionTabPanel::resized()` pass on every editor timer tick; relayout only when dynamic text actually changes.

### Commit 40 - `feat(residual): estimate modern residual bands from source`

**PRD:** section 5.4, section 5.7, section 7.3

- [x] Replace direct Modern ATS uniform residual placeholders with source-derived 25-band Bark residual estimates from the same FFT-backed STFT frames used for peak estimation.
- [x] Subtract capped source peaks from per-band spectral energy so untracked/noisy components remain in orphan residual bands for diagnostics and morphing.
- [x] Add deterministic analysis and acceptance tests that fail if direct Modern ATS stops producing structured, non-zero source residual energy.

### Commit 41 - `fix(plugin): preserve modern source residual bands`

**PRD:** section 5.1, section 5.4, section 6.1

- [x] Stop overwriting direct Modern ATS source-derived residual bands with classic ATSA residual bands after plugin analysis.
- [x] Keep the classic `ATS_SOUND` model available for ATS export and classic preview/render compatibility without mutating the Modern model diagnostics/morph data.
- [x] Update the source-path regression guard so classic-derived residual overlay helpers cannot be reintroduced silently.

### Commit 42 - `fix(synth): render modern source residual preview lane`

**PRD:** section 5.4, section 5.7, section 6.1

- [x] Render the Modern/Experimental preview residual channel from the Modern model's source-derived orphan residual bands instead of classic `do_synthesis` residual audio.
- [x] Keep captured transient buffers isolated to the partial layer and mute partial/transient playback in the residual layer so residual-only controls audition untracked source energy.
- [x] Extend the source-path guard and synthesis docs so Modern preview cannot silently fall back to classic residual rendering again.

### Commit 43 - `fix(synth): preserve residual band shape`

**PRD:** section 5.4, section 5.7, section 7.3

- [x] Replace the broadband orphan residual noise collapse with deterministic per-Bark-band noise shaping shared by Modern synth and the analysis helper.
- [x] Add a Modern synth regression proving high-band residual energy renders brighter/faster than low-band residual energy.
- [x] Refresh synthesis documentation and representative budgets for the shared band-shaped residual renderer.

### Commit 44 - `fix(analysis): suppress tracked-peak residual leakage`

**PRD:** section 5.4, section 7.3

- [x] Subtract each accepted peak's local FFT main lobe from source-derived residual bands instead of subtracting only scalar peak amplitude.
- [x] Add a deterministic clean-sine regression that fails if a tracked sinusoid's window skirts become orphan residual energy.
- [x] Preserve the structured residual fixture so untracked spectral components still remain in orphan residual bands.

### Commit 45 - `fix(analysis): keep pruned tracks in residual bands`

**PRD:** section 5.3-section 5.4, section 7.3

- [x] Move Modern residual-band estimation after assignment tracking and short-track pruning so only active partials claim spectral energy.
- [x] Keep rejected/pruned short tracks represented in orphan residual bands instead of dropping their energy from both Modern partials and residual.
- [x] Add a deterministic forced-pruning regression while preserving the tracked-sine leakage and structured-residual tests.

### Commit 46 - `fix(synth): bound modern preview headroom`

**PRD:** section 5.7, section 7.3

- [x] Add a shared deterministic ATS soft-knee limiter so Modern partial/residual/transient snapshot sums cannot emit unchecked full-scale overs.
- [x] Route the plugin's final preview mix through the same limiter after partial gain, residual gain, residual-only mode, and output trim are applied.
- [x] Add a Modern synth regression that proves below-knee samples are unchanged, dense partial sums are bounded to full scale, and diagnostic unlimited renders remain opt-in.

### Commit 47 - `fix(analysis): preserve close residual tones`

**PRD:** section 5.4, section 7.3

- [x] Make tracked-peak residual lobe subtraction adaptive so broad clean-sine lobe suppression stops at the valley before a strong neighboring spectral maximum.
- [x] Preserve close untracked source components in orphan residual bands when `maxPeaksPerFrame` forces them out of the Modern partial layer.
- [x] Add a deterministic close-two-tone regression while keeping the clean tracked-sine leakage and pruned-track residual guards passing.

### Commit 48 - `fix(tracking): solve modern assignments globally`

**PRD:** section 5.3, section 7.3

- [x] Replace greedy sorted candidate acceptance with a deterministic per-frame min-cost assignment that weighs track-to-peak matches against explicit track-death and peak-birth dummy choices.
- [x] Preserve bounded candidate costs per active track while preventing one cheap local match from causing unnecessary death/birth churn in neighboring tracks.
- [x] Add a regression fixture where the greedy resolver would choose one local low-cost match, but the global assignment keeps both existing Modern tracks alive.

### Commit 49 - `fix(tracking): fill short modern dropouts`

**PRD:** section 5.3, section 7.3

- [x] Carry direct Modern assignment tracks through bounded short peak dropouts using the analysis `min_gap_len` setting instead of immediately emitting death/birth churn.
- [x] Backfill returning short gaps as `Interpolated` partial frames and `GapFill` lifecycle events so model diagnostics and synthesis preserve continuity.
- [x] Keep transient-break resets authoritative so attack protection still prevents gap filling across marked onsets.

### Commit 50 - `fix(acceptance): score gap-filled sleeps as continuous`

**PRD:** section 7.3

- [x] Update Modern continuity metrics so `Sleep` lifecycle events repaired by same-frame `GapFill` events do not count as track breaks.
- [x] Add an acceptance regression proving a short dropout repaired by Modern assignment gap fill preserves the continuity score.
- [x] Document the synthetic acceptance gate semantics for gap-filled short dropouts.

### Commit 51 - `fix(analysis): store oscillator-start phases`

**PRD:** section 5.1, section 5.3, section 7.3

- [x] Convert direct Modern STFT peak phases from raw cosine-basis FFT bin phase into sine oscillator phase at the analysis frame start.
- [x] Correct the off-bin phase rotation introduced by symmetric analysis windows before storing partial frame `phase_radians`.
- [x] Add a direct-source regression proving an off-bin sine with known initial phase is stored close to the oscillator-start phase.

### Commit 52 - `fix(tracking): wrap gap-filled phase interpolation`

**PRD:** section 5.3, section 7.3

- [x] Interpolate short-dropout `GapFill` frame phases along the wrapped shortest path instead of linearly crossing through zero.
- [x] Add an assignment-tracker regression where neighboring phases straddle the `pi`/`-pi` boundary.
- [x] Document that synthetic continuity coverage includes phase-safe gap-filled dropouts.

### Commit 53 - `fix(tracking): add phase-aware assignment cost`

**PRD:** section 5.3, section 7.3

- [x] Add phase-continuity weighting to Modern assignment costs when analysis sample-rate and hop-size context are available.
- [x] Persist `phase_cost` in assignment diagnostics so modern tracker decisions expose frequency, amplitude, SMR, phase, and slope terms.
- [x] Add a deterministic tracker regression where frequency/amplitude ties are resolved by phase identity instead of peak order.

### Commit 54 - `fix(synth): scale modern hops to output rate`

**PRD:** section 5.7, section 7.3

- [x] Scale Modern partial/residual render hops from model sample-rate samples to the requested output sample rate before applying `timeFactor`.
- [x] Keep second-based transient markers aligned with partial/residual frames when Modern renders target a different sample rate.
- [x] Add a Modern synth regression proving output-rate hop scaling and time-stretch composition.

### Commit 55 - `fix(synth): phase-lock modern frame renders`

**PRD:** section 5.7, section 7.3

- [x] Use stored Modern partial phases as per-frame trajectory targets during phase-enabled synthesis instead of only seeding track births.
- [x] Apply a bounded per-sample phase correction so oscillator phase lands on the next active frame phase without hard discontinuities.
- [x] Add a Modern synth regression proving stored phase bends a zero-frequency frame to the next phase and remains disabled when `useStoredPhase` is false.

### Commit 56 - `fix(synth): preserve transposed phase trajectories`

**PRD:** section 5.7, section 7.3

- [x] Keep stored phase seeding for transposed Modern renders while disabling per-frame phase-lock correction when `frequencyFactor` changes pitch.
- [x] Prevent next-frame analysis phases from canceling intentional pitch-shift trajectories.
- [x] Add a Modern synth regression proving pitch-shifted renders free-run instead of landing on untransposed stored phase targets.

### Commit 57 - `test(acceptance): fail incomplete external gates`

**PRD:** section 7.1-section 7.3

- [x] Make enabled external real-audio acceptance fail when configured fixtures are skipped for missing audio or reference metrics.
- [x] Fail external fixture runs that emit zero gate checks so unavailable metrics cannot masquerade as a passing real-audio comparison.
- [x] Add a deterministic Python driver contract CTest that exercises missing-asset and zero-check failures without requiring real audio assets.

### Commit 58 - `feat(tools): add modern offline candidate gate`

**PRD:** section 5.1-section 5.7, section 7.1-section 7.3

- [x] Add `tools/ats_modern_offline` so external real-audio acceptance can run direct Modern ATS analysis instead of invoking the classic-only offline tool as the candidate.
- [x] Emit flat Modern candidate metadata metrics for continuity, residual ratio, and transient sharpness alongside the rendered Modern snapshot WAV.
- [x] Route the external acceptance CTest to the Modern candidate tool and add a deterministic CLI smoke test proving the tool analyzes and renders a generated two-tone fixture.

### Commit 59 - `test(fixtures): enable first-party external samples`

**PRD:** section 7.1-section 7.3

- [x] Add four first-party Casey Wescott audio fixtures for external Modern-vs-ATSA acceptance testing.
- [x] Replace PENDING fixture provenance with committed first-party sample provenance and actual sample-rate/channel expectations.
- [x] Generate ATSA reference summaries for the committed fixtures and run the external gate against the Modern candidate tool.

### Commit 60 - `test(acceptance): gate real fixture quality metrics`

**PRD:** section 7.1-section 7.3

- [x] Emit comparable Classic and Modern metadata for continuity, identity-discontinuity, residual energy, rendered residual/noise ratio, and attack sharpness.
- [x] Update real-audio external acceptance to use rendered-audio residual and transient proxies instead of placeholder summary fields.
- [x] Enable non-parity real-audio gates on the first-party fixtures, including tracker continuity for the sustained vowel phrase.

### Commit 61 - `fix(synth): preserve transient-leading attacks`

**PRD:** section 5.7, section 7.3

- [x] Stop transient-buffer fade-in from attenuating attacks whose marker onset is at the start of the captured buffer.
- [x] Add a Modern synth regression proving onset-at-buffer-start samples remain full strength while later-onset buffers still fade in before the marker.
- [x] Tighten first-party external fixture tolerances for residual highpass ratio, plucked/crotale spectral distance, and crotale transient sharpness.

### Commit 62 - `fix(analysis): reject modern identity jumps`

**PRD:** section 5.3, section 7.3

- [x] Reject Modern assignment continuations whose last-active-to-next frequency jump crosses the acceptance identity-break threshold, even when slope prediction makes the candidate cheap.
- [x] Add a deterministic assignment regression proving a large harmonic jump births a replacement track instead of corrupting an existing partial identity.
- [x] Tighten the sustained first-party vowel phrase gate to allow zero Modern identity errors against the ATSA reference.

### Commit 63 - `test(acceptance): score modern continuity comparably`

**PRD:** section 7.1-section 7.3

- [x] Keep Modern lifecycle break counts as diagnostics while removing terminal death/prune events from the active-to-active continuity score.
- [x] Emit Modern offline continuity submetrics, including lifecycle breaks and mean confidence, so track churn remains visible outside the score.
- [x] Tighten the sustained first-party vowel phrase continuity gate to allow no regression against the ATSA reference.

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
| Direct transient-buffer capture | 36 |
| Modern preview transient playback | 37 |
| Diagnostic UI stability | 39 |
| Direct modern residual estimation | 40 |
| Plugin preservation of modern residual bands | 41 |
| Modern preview source residual playback | 42 |
| Bark-shaped residual synthesis | 43 |
| Tracked-peak residual leakage suppression | 44 |
| Pruned-track residual preservation | 45 |
| Modern preview headroom safety | 46 |
| Close residual tone preservation | 47 |
| Global modern assignment solving | 48 |
| Short modern dropout gap filling | 49 |
| Gap-filled continuity scoring | 50 |
| Direct modern oscillator phase storage | 51 |
| Wrapped gap-fill phase interpolation | 52 |
| Phase-aware modern assignment cost | 53 |
| Output-rate modern render hop scaling | 54 |
| Phase-locked modern frame rendering | 55 |
| Pitch-shift-safe modern phase rendering | 56 |
| Enforced external real-audio gate readiness | 57 |
| Modern offline external candidate tool | 58 |
| First-party external audio fixtures | 59 |
| Real fixture quality metric gates | 60 |
| Transient-leading attack preservation | 61 |
| Modern assignment identity-jump rejection | 62 |
| Comparable real-audio continuity scoring | 63 |

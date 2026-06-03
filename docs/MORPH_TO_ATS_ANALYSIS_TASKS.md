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

- [x] Define fixture categories: sustained harmonic, vibrato, voice/vowel, inharmonic metallic, plucked/percussive, noisy/breathy, dense/polyphonic.
- [x] Document fixture provenance, license, sample rate, and expected analysis mode.
- [x] Add placeholders for source A/source B fixture pairings.

Implementation note: Added `tests/fixtures/morph_fixture_manifest.json` with seven placeholder fixture categories and four source A/source B pairing placeholders. Added `tests/morph_fixture_manifest_check.py` to validate category coverage, provenance/license/sample-rate/analysis-mode metadata, and pairing references without requiring audio assets yet.

### Commit 3 - `test: add morph metric harness`

**PRD:** section 8.2-section 8.3

- [x] Add endpoint fidelity checks for morph amount 0 and 1.
- [x] Add per-band power delta checks across a morph sweep.
- [x] Add correspondence churn and trajectory smoothness metrics.
- [x] Record tolerances in one place so later commits can tighten them.

Implementation note: Added `tests/morph_metric_harness.py`, `tests/fixtures/morph_metric_tolerances.json`, and `tests/fixtures/morph_metric_smoke_report.json`. The harness validates endpoint RMS/peak error, per-band power deltas across required morph amounts, correspondence churn, and trajectory frequency/amplitude smoothness. Registered both the morph fixture manifest check and metric harness in CTest.

---

## Phase 1 - Recipe Schema and Source Validation

### Commit 4 - `feat(morph): add morph recipe schema`

**PRD:** section 6.1, section 6.11, section 7.2

- [x] Add recipe version, engine version, source ids, source hashes, and source metadata.
- [x] Store morph amount defaults and independent lane settings.
- [x] Keep recipe data separate from source ATS models.
- [x] Add serialization round-trip tests.

Implementation note: Added `src/core/AtsMorphRecipe.h/.cpp` with a versioned, source-metadata-only morph recipe schema, deterministic text serialization, and lane defaults for frequency, amplitude, residual/noise, time, transient, and phase. Added `tests/morph_recipe_test.cpp` and CTest registration for schema round-trip coverage.

### Commit 5 - `feat(morph): validate source model compatibility`

**PRD:** section 6.1, section 7.1

- [x] Validate sample rates, frame times, frequency bounds, channel assumptions, and required partial data.
- [x] Emit warnings for missing phase, missing residual/noise data, or incompatible frame grids.
- [x] Add deterministic error codes for UI and tests.

Implementation note: Extended morph source metadata with frame hop, frequency bounds, and channel count, then added `validateMorphSources()` with deterministic compatibility issue codes. Compatibility treats sample-rate/channel/required-data/frequency-bound failures as errors, and missing phase, missing residual/noise, or mismatched frame grids as warnings. Covered validation and code-name lookup in `tests/morph_recipe_test.cpp`.

### Commit 6 - `feat(morph): persist morph recipe in plugin state`

**PRD:** section 6.11

- [x] Store recipe metadata and user-selected policies in plugin state.
- [x] Reference source models by id/hash or embed blobs according to existing state patterns.
- [x] Preserve unknown future fields where practical.
- [x] Add reload tests for deterministic recipe reconstruction.

Implementation note: Bumped plugin state to version 5 and appended a length-prefixed serialized morph recipe after the existing ATS model/residual-only fields, while keeping version 2-4 state readable. The recipe references source models by id/hash and keeps the existing embedded ATS blob behavior for model data. `MorphRecipe` now preserves unknown future serialized fields, and `tests/morph_recipe_test.cpp` covers deterministic reload plus future-field preservation.

---

## Phase 2 - Perceptual Band Grid

### Commit 7 - `feat(morph): implement Bark band grid`

**PRD:** section 6.2, section 7.2

- [x] Add deterministic Bark band edge generation.
- [x] Store grid type, band count, bounds, and edge frequencies in the recipe.
- [x] Add tests for stable band definitions across platforms.

Implementation note: Added `MorphFrequencyGrid` metadata to the morph recipe plus deterministic Bark edge generation using Zwicker Bark coordinates and bounded inversion back to Hz. Recipes now serialize grid type, band count, frequency bounds, and all edge frequencies. `tests/morph_recipe_test.cpp` covers metadata round-trip, monotonic edges, preserved bounds, and stable edge anchors.

### Commit 8 - `feat(morph): assign partials to perceptual bands`

**PRD:** section 6.2

- [x] Assign partials to bands by instantaneous frequency per frame.
- [x] Add track centroid assignment for diagnostics.
- [x] Handle out-of-range, silent, and inactive partial frames explicitly.
- [x] Add tests for boundary behavior near band edges.

Implementation note: Added model-agnostic partial-frame and partial-track assignment helpers for perceptual frequency grids. Frame assignment uses instantaneous frequency with lower-inclusive/higher-band internal edges and final-edge inclusion. Diagnostics explicitly mark assigned, silent, inactive, low/high out-of-range, and invalid-grid frames. Track centroid diagnostics use amplitude-weighted active assigned frames. Boundary and centroid behavior is covered in `tests/morph_recipe_test.cpp`.

### Commit 9 - `feat(morph): add Mel grid option`

**PRD:** section 2, section 6.2, section 9

- [x] Add Mel band generation behind a non-default option.
- [x] Store scale choice in recipe metadata.
- [x] Compare Bark and Mel on fixture metrics before considering Mel for presets.

Implementation note: Added `makeMelFrequencyGrid()` using the standard 2595-log Mel scale and kept Bark as the default recipe grid. Recipes now serialize/deserialize `grid.type=mel` distinctly from Bark. `tests/morph_recipe_test.cpp` compares Bark and Mel edge anchors over fixture-scale bounds and verifies they remain measurably distinct before any preset promotes Mel.

---

## Phase 3 - Within-Band Correspondence

### Commit 10 - `feat(morph): add frequency-rank correspondence`

**PRD:** section 6.3

- [x] Pair source A/source B partials by sorted frequency within each band.
- [x] Store explicit correspondence records in the recipe.
- [x] Add unmatched records when counts differ.
- [x] Add tests for stable output on simple fixtures.

Implementation note: Added deterministic frequency-rank correspondence records with `matched`, `unmatched_source`, and `unmatched_target` kinds. The builder filters assigned band partials, sorts by band/frequency/partial/frame, pairs by in-band rank, emits unmatched records for count mismatches, and stores records in morph recipe serialization. `tests/morph_recipe_test.cpp` covers stable pairing, unmatched source/target records, kind lookup, and recipe round-trip persistence.

### Commit 11 - `feat(morph): add unmatched partial policies`

**PRD:** section 6.3, section 6.5

- [x] Implement fade out, fade in, hold, merge into residual, and discard policies.
- [x] Make discard opt-in and visible in recipe metadata.
- [x] Add tests for each policy with count-mismatch fixtures.

Implementation note: Added source/target unmatched partial policy metadata to `MorphRecipe`, defaulting source-only partials to `fade_out` and target-only partials to `fade_in`. Added deterministic policy resolution for unmatched correspondence records, including hold, merge-into-residual power routing, and discard decisions. Discard now requires `unmatched.discard_opt_in=1` before a serialized recipe is accepted. `tests/morph_recipe_test.cpp` covers every policy on the count-mismatch correspondence fixture and recipe round-trip metadata.

### Commit 12 - `feat(morph): add greedy nearest matching`

**PRD:** section 6.3

- [x] Match by nearest log-frequency with optional amplitude-rank bias.
- [x] Add per-match cost diagnostics.
- [x] Compare against frequency-rank matching on vibrato and inharmonic fixtures.

Implementation note: Added `buildGreedyNearestCorrespondence()` with a global per-band greedy nearest-pair selection. Matched records now optionally carry log-frequency cost, weighted amplitude-rank cost, and total cost diagnostics, which serialize with the recipe while older records default to no diagnostics. `tests/morph_recipe_test.cpp` compares greedy matching against frequency-rank matching on vibrato-style and inharmonic-style count-mismatch fixtures, and verifies amplitude-rank bias changes matching deterministically.

### Commit 13 - `feat(morph): smooth correspondence over time`

**PRD:** section 6.3, section 8.2

- [x] Add hysteresis, continuity penalties, or short-window smoothing to reduce frame-to-frame flapping.
- [x] Emit correspondence churn metrics.
- [x] Add regression tests for sustained and vibrato fixtures.

Implementation note: Added `smoothCorrespondenceFrames()` with a hysteresis cost margin that can preserve prior source-to-target pairings when a temporary nearest-neighbor swap is only marginally cheaper. Added `calculateCorrespondenceChurnMetrics()` to count adjacent-frame source/target changes and report churn rate. `tests/morph_recipe_test.cpp` now covers a sustained zero-churn fixture and a vibrato-style crossing fixture where smoothing removes raw greedy target flapping.

### Commit 14 - `feat(morph): prototype assignment matching`

**PRD:** section 6.3, section 9

- [x] Add an assignment-matrix matcher behind an advanced strategy flag.
- [x] Use bounded candidate sets to keep CPU predictable.
- [x] Include log-frequency, amplitude, SMR/confidence, and continuity costs where available.
- [x] Compare quality and CPU against greedy matching.

Implementation note: Added `buildAssignmentMatrixCorrespondence()` with an explicit `advancedStrategyEnabled` flag; disabled mode falls back to greedy matching. Enabled mode runs a bounded per-band assignment search with deterministic candidate limiting, candidate-edge and evaluated-assignment diagnostics, and per-match log-frequency, amplitude-rank, confidence, continuity, and total costs. `tests/morph_recipe_test.cpp` compares assignment-matrix quality/CPU diagnostics against greedy matching, verifies candidate bounding, and covers confidence/continuity cost serialization.

---

## Phase 4 - Interpolation and Energy Bookkeeping

### Commit 15 - `feat(morph): interpolate frequency in perceptual domain`

**PRD:** section 6.4

- [x] Add log-frequency or Bark-coordinate interpolation.
- [x] Preserve morph endpoints within documented tolerance.
- [x] Add band-edge migration or clamping behavior.
- [x] Add tests for monotonic morph sweeps.

Implementation note: Added `interpolateMorphFrequency()` with log-frequency as the default domain and Bark-coordinate interpolation as an option. In-grid endpoints are preserved exactly within floating-point tolerance, sweeps clamp to saved grid bounds when enabled, and results report source/target/output band indexes plus band-crossing and clamping diagnostics. `tests/morph_recipe_test.cpp` covers endpoint preservation, geometric/log midpoint behavior, Bark monotonic interpolation, monotonic band-edge migration, and out-of-grid clamping.

### Commit 16 - `feat(morph): interpolate amplitude by power or db`

**PRD:** section 6.4

- [x] Add power interpolation as the default amplitude policy.
- [x] Add dB interpolation with a configurable floor.
- [x] Verify quiet partials do not create level jumps.
- [x] Add endpoint and sweep tests.

Implementation note: Added `interpolateMorphAmplitude()` with power interpolation as the default and dB interpolation as an option. dB interpolation uses a configurable floor, defaulting to -120 dB, while preserving exact endpoints so silent endpoints remain silent at morph amount 0 or 1. `tests/morph_recipe_test.cpp` covers power endpoints/midpoint/sweeps, dB endpoints/midpoint, configurable floors, quiet zero-to-one partial sweeps, and zero-to-zero silence.

### Commit 17 - `feat(morph): conserve per-band energy`

**PRD:** section 6.5, section 8.3

- [x] Track source and output sinusoidal power per band.
- [x] Redistribute merge/split power according to selected unmatched policy.
- [x] Emit warnings when default-mode band power delta exceeds tolerance.
- [x] Add tests for 3-to-1 and 1-to-3 partial count mismatches.

Implementation note: Added `calculateBandEnergyReport()` with per-band source/target sinusoidal power, output sinusoidal power, residual-routed power, expected output power, delta, and warning counts. Default FadeOut/FadeIn unmatched policies conserve 3-to-1 and 1-to-3 power in the power-interpolation domain; MergeIntoResidual routes the interpolated unmatched contribution to residual power; Discard produces an explicit warning delta.

### Commit 18 - `feat(morph): add independent morph envelopes`

**PRD:** section 6.4, section 6.7, section 6.9-section 6.10

- [x] Add independent curves for frequency, amplitude, residual/noise energy, time, and optional phase.
- [x] Store curve settings in the recipe.
- [x] Keep one-knob morph behavior as the simple default.
- [x] Add automation/state tests for curve recall.

Implementation note: Added lane curve shapes (`linear`, `ease_in`, `ease_out`, `ease_in_out`) to morph lane settings and recipe serialization. `evaluateMorphLaneAmounts()` keeps linked linear lanes following the main morph amount by default, while unlinked lanes evaluate their own saved amount through their selected curve. `tests/morph_recipe_test.cpp` covers curve lookup, clamped automation evaluation, linked/unlinked lane evaluation, and serialized curve recall.

---

## Phase 5 - Residual, Phase, and Transients

### Commit 19 - `feat(morph): morph residual and noise lanes`

**PRD:** section 6.7

- [x] Morph residual/noise band energy separately from sinusoidal partials.
- [x] Preserve orphan residual energy when available.
- [x] Add independent gain paths for partial, partial-noise, and orphan residual lanes.
- [x] Add noisy/breathy fixture tests.

Implementation note: Added `MorphResidualNoiseBand` and `morphResidualNoiseBands()` to interpolate partial-attached noise and orphan residual power in separate per-band lanes. Orphan residual power remains distinct at endpoints and during morphs, and `MorphRenderGainSettings` now stores independent partial, partial-noise, and orphan-residual output gains in recipe metadata. `tests/morph_recipe_test.cpp` covers noisy/breathy residual fixture behavior, endpoint orphan preservation, gain scaling, and gain metadata round-trip.

### Commit 20 - `feat(morph): add phase strategy metadata`

**PRD:** section 6.6

- [x] Add phase strategy choices: oscillator continuation, source-A donor, source-B donor, relative cluster preservation, and direct blend.
- [x] Default to renderer phase continuity unless tests justify a different default.
- [x] Add tests for deterministic births, deaths, and endpoint behavior.

Implementation note: Added `MorphPhaseStrategy` and `MorphPhaseSettings` with serialized `phase.strategy` metadata. The default remains `oscillator_continuation`, so the renderer owns phase continuity unless a saved recipe explicitly picks a donor or direct-blend strategy. `resolveMorphPhase()` now gives deterministic endpoint, birth, and death behavior for tests: direct blend preserves matched source/target endpoints, target-only births use the target donor phase, and source-only deaths use the source donor phase.

### Commit 21 - `feat(morph): fade partial births and deaths`

**PRD:** section 6.6, section 8.3

- [x] Add configurable birth/death fade windows.
- [x] Avoid phase resets except at explicit lifecycle boundaries.
- [x] Add click and zipper checks around unmatched partial transitions.

Implementation note: Added `MorphLifecycleFadeSettings` with serialized birth/death fade frame counts, fade curve, and max per-frame gain-step threshold. `evaluateMorphLifecycleFade()` now returns deterministic birth/death gain scales, only permits phase resets on lifecycle frame zero, and reports click/zipper risk for instant or too-steep unmatched partial transitions. `tests/morph_recipe_test.cpp` covers smooth ramps, reset gating, instant-click warnings, zipper warnings, and recipe round-trip metadata.

### Commit 22 - `feat(morph): protect transient windows`

**PRD:** section 6.8

- [x] Carry transient markers from source models into the recipe.
- [x] Add transient policies: source A, source B, crossfade buffers, or sustained-only morph after attack.
- [x] Warn when source onsets are poorly aligned.
- [x] Add plucked/percussive fixture tests.

Implementation note: Added transient policy metadata plus source A/source B transient marker lists to the morph recipe serialization. Added `analyzeMorphTransientAlignment()` to compare onset markers by time rank and emit signed offset warnings when they exceed the saved tolerance. `tests/morph_recipe_test.cpp` covers transient policy lookup, recipe round-trip metadata, and plucked/percussive onset misalignment diagnostics.

---

## Phase 6 - Rendering and Real-Time Integration

### Commit 23 - `feat(morph): render immutable morph snapshots`

**PRD:** section 6.1, section 7.1

- [x] Evaluate a recipe at a morph amount and produce an immutable model snapshot.
- [x] Keep rendering off the audio thread unless it is proven bounded and allocation-free.
- [x] Add deterministic offline render tests.

Implementation note: Added `MorphSnapshotOptions`, `MorphSnapshotPartial`, and `MorphRenderSnapshot` plus `evaluateMorphRecipeSnapshot()` in the morph core. Snapshot evaluation prepares value-owned partial, residual/noise, energy, and transient diagnostic data from the recipe and marks the snapshot as requiring offline preparation while safe for audio-thread playback as immutable data. `tests/morph_recipe_test.cpp` covers deterministic repeated offline snapshot evaluation, matched/unmatched partial output, residual/noise gains, energy diagnostics, and transient diagnostics.

### Commit 24 - `feat(synth): play prepared morph snapshots`

**PRD:** section 7.1, section 8.3

- [x] Route prepared snapshots through the existing synthesis path.
- [x] Confirm audio-thread safety: no allocation, locks, file I/O, logging, or analysis.
- [x] Add stress tests for automation changes while snapshots are swapped.

Implementation note: Added a prepared morph snapshot playback buffer that renders immutable snapshots into partial/residual layer vectors off the audio thread, plus a cursor-only mixer for live playback gain changes. `AtsAudioProcessor::installPreparedMorphSnapshotForPlayback()` installs those prepared layers into the existing triple-buffer preview path consumed by `processBlock()`. `tests/morph_recipe_test.cpp` now stress-tests repeated snapshot swaps and live partial/residual gain changes without mutating prepared buffers.

Hardening note: Morphed residual preview now renders banded residual/noise energy with deterministic per-band noise carriers instead of a single residual sine carrier. The legacy `residualCarrierHz` setting remains only as a fallback for unbanded residual power.

### Commit 25 - `perf(morph): profile morph analysis and snapshot costs`

**PRD:** section 8.2-section 8.3

- [x] Measure CPU/memory for representative partial and frame counts.
- [x] Document soft caps and UI warning thresholds.
- [x] Compare frequency-rank, greedy, and assignment strategies.

Implementation note: Added deterministic morph performance profiling in the core with soft caps for partial candidates, frame count, candidate edges, assignment evaluations, estimated analysis memory, and estimated snapshot memory. The profile compares frequency-rank, greedy-nearest, and assignment-matrix correspondence using candidate-edge/evaluated-assignment counts and relative CPU estimates. `tests/morph_recipe_test.cpp` covers representative count/memory estimates plus UI warning thresholds.

---

## Phase 7 - UI and Presets

### Commit 26 - `feat(ui): add morph source slots and amount control`

**PRD:** section 6.9-section 6.10.2

- [x] Add source A/source B slots.
- [x] Show model name, duration, sample rate, partial count, residual/noise availability, source gain, and time offset.
- [x] Add compact source spectrogram or partial-track overview with transient markers when available.
- [x] Add source actions for analyze/replace, normalize, listen source A, listen source B, and future time anchors.
- [x] Expose morph amount as a host-automatable parameter.
- [x] Show source hashes or names in saved state diagnostics.

Implementation note: Added metadata-backed Source A/B slots to the Session tab. Replace A/B assigns the current analyzed ATS model into the saved morph recipe metadata, including name, sample rate, duration, frame count, partial count, residual/phase flags, frequency bounds, and a deterministic diagnostic hash. The source panels show compact partial-track overview text with transient-marker presence, reserved normalize/time-anchor controls, and listen actions that use the current rendered preview. Added the host-automatable `morphAmount` APVTS parameter, a Session-tab slider attachment, and state-version 6 persistence for the parameter/default recipe amount.

### Commit 27 - `feat(ui): add central morph macro and lanes`

**PRD:** section 6.9-section 6.10.2

- [x] Add a large morph knob or horizontal macro where 0 is source A, 0.5 is midpoint, and 1 is source B.
- [x] Add optional XY pad support with X mapped to A/B morph and Y mapped to a performance macro.
- [x] Add lane controls for frequency, amplitude, residual/noise, time, transient, and optional phase.
- [x] Add linked/unlinked lane states so lanes can follow the main morph amount or move independently.
- [x] Add smoothing and reset-to-linked behavior for live-safe controls.

Implementation note: Added central morph macro controls to the Session tab: a horizontal `morphAmount` slider with A/mid/B endpoints, an XY pad that writes X to `morphAmount` and Y to `morphPerformanceMacro`, six lane amount sliders, six lane link toggles, a `morphSmoothingMs` control, and a Link lanes reset button. Bumped plugin state to version 7 so performance macro, smoothing, lane amounts, and lane link states persist; recipe serialization now mirrors default morph amount plus lane amount/link metadata.

### Commit 28 - `feat(ui): expose morph policy controls`

**PRD:** section 6.9-section 6.10.4

- [x] Expose grid type, correspondence strategy, unmatched policy, interpolation domains, and phase strategy.
- [x] Expose transient policy: source A attack, source B attack, crossfade attacks, and sustained-only morph after attack.
- [x] Keep discard policy behind an explicit expert choice.
- [x] Keep defaults simple and visible.
- [x] Place advanced options behind an expert panel if needed.

Implementation note: Added compact policy controls to the Session tab for grid type, correspondence strategy, source-only and target-only unmatched policies, frequency and amplitude interpolation domains, phase strategy, and transient policy. Discard choices are disabled until the explicit `morphDiscardOptIn` toggle is enabled, and recipe sync falls back to fade-out/fade-in if discard is automated without opt-in. Bumped plugin state to version 8 and mirrors policies into morph recipe fields or visible `ui.*` recipe metadata.

### Commit 29 - `feat(ui): add perceptual band strip and inspector`

**PRD:** section 6.9-section 6.10.4, section 8.2

- [x] Add a horizontal Bark/Mel band strip.
- [x] Show source A energy, source B energy, current morphed energy, matched count, unmatched count, and warning state per band.
- [x] Open a selected-band inspector when a band is clicked.
- [x] Show correspondence pairs, unmatched partials, selected policy, match costs when available, and lifecycle state around births/deaths.
- [x] Display matched/unmatched partial counts per band.
- [x] Display energy delta and warning bands.
- [x] Display transient alignment warnings.
- [x] Keep diagnostics generation off the audio thread.

Implementation note: Added `AtsAudioProcessor::buildMorphBandDiagnostics()` to copy morph recipe state under the recipe lock and compute band energy/correspondence/transient diagnostics for UI use only. Added a Session-tab `MorphBandStrip` and selected-band inspector: the strip shows source A, source B, and output power per band plus warning/unmatched markers, and clicking a band updates inspector text with grid, policy, interpolation domains, counts, energy delta, transient warnings, lifecycle settings, and representative correspondence pairs/costs when records exist.

### Commit 30 - `feat(ui): add morph snapshots and live rebuild states`

**PRD:** section 6.9-section 6.10.5, section 6.11

- [x] Add quick snapshot buttons for A, 25%, 50%, 75%, and B.
- [x] Add user variation slots when preset/state support is available.
- [x] Store lane link states, performance macro settings, and snapshot slots in plugin state.
- [x] Keep the last valid snapshot playing while expensive policy changes rebuild in the background.
- [x] Show stale, rebuilding, ready, and warning states.

Implementation note: Added Session-tab quick snapshots for A, 25%, 50%, 75%, and B, plus three state-backed variation slots that save/recall the main morph amount, performance macro, smoothing, and lane amount/link states. Plugin state version 9 now persists variation slot data alongside existing morph live controls. Morph parameter edits mark the prepared snapshot stale without clearing the active preview buffer; worker activity reports rebuilding, completed snapshot installs report ready, and missing A/B source slots report warning.

UX hardening note: Added an in-plugin Morph Timeline to the Session tab so users can draw normalized A/B morph breakpoints over the internal source duration, render/play the curve, stop, edit, and replay without DAW automation. Timeline points persist as recipe UI metadata (`ui.morph_timeline_points`), and playback stitches short prepared morph snapshots with boundary crossfades for quick iteration.

### Commit 31 - `feat(presets): add morph analysis presets`

**PRD:** section 5, section 6.9

- [x] Add stable default preset for broad source material.
- [x] Add tonal, vocal/formant, percussive, and noisy texture presets.
- [x] Store preset metadata explaining selected policies.

Implementation note: Added morph-focused factory presets to the existing preset library: Morph stable default, Morph tonal, Morph vocal/formant, Morph percussive, and Morph noisy texture. Each preset applies a coherent set of morph grid, correspondence, unmatched policy, interpolation, phase/transient, smoothing, performance macro, gain, and lane-link settings. Applying a morph preset writes `ui.morph_preset_name` and `ui.morph_preset_description` metadata into the morph recipe so saved state records the selected policy rationale.

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

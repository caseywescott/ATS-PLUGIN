# Task List: Timbre Descriptor Bundle in Coherent Commits

**Source:** [TIMBRE_DESCRIPTOR_BUNDLE_MASTER_SPEC.md](./TIMBRE_DESCRIPTOR_BUNDLE_MASTER_SPEC.md)
**Related:** [SPECTRAL_ENVELOPE_TASKS.md](./SPECTRAL_ENVELOPE_TASKS.md),
[SPECTRAL_ENVELOPE_FIXTURES.md](./SPECTRAL_ENVELOPE_FIXTURES.md), mainline
`docs/MORPH_TO_ATS_ANALYSIS_TASKS.md`, mainline `docs/DSP_FIDELITY_TASKS.md`
**How to use:** Each commit section is intended to be independently reviewable, buildable, and
testable. Split a commit only along the same ownership boundary. Do not combine analysis-only
descriptor work with audible render changes.

---

## Branch and Delivery Rules

Every commit is tagged with its intended integration line:

| Tag | Meaning |
|-----|---------|
| **[hybrid-function]** | Implement in this worktree using `EnvelopeFrameInput`, `FrameEnvelope`, and the generalized spectral-envelope stack |
| **[mainline]** | Implement on the main repo / `dsp-fidelity-tier1` line using the morph recipe, transport, and trajectory renderer |
| **[after-reconcile]** | Requires the generalized envelope stack and mainline morph stack to be reconciled first |
| **[research]** | Prototype and gate before product integration |
| **[ear]** | Audible change requiring listening validation in addition to numerical tests |

Standing rules:

- Analysis descriptors are deterministic, off-thread, and output finite values plus explicit
  status/confidence.
- Default weighting is energy: partial `amplitude^2`, residual stored band energy.
- Descriptor domain, weighting, floor, axis, and version are serialized.
- Harmonic descriptors return unavailable without reliable harmonic assignment.
- Missing residual data is not interpreted as zero residual energy.
- No descriptor fitting, series analysis, allocation, locking, or file I/O runs in
  `processBlock`.
- Mainline morph endpoint, energy, transport, and churn gates remain pinned.
- Old recipes and sessions render unchanged unless the user opts into a new audible control.
- Fixed synthesis `MorphLane` values remain separate from dynamic descriptor target curves.
- Arbitrary descriptor curves/routes live in versioned recipe state; only a small stable macro set
  expands the APVTS/host parameter surface.

---

## Phase 0 - Planning, Coverage, and Baselines

### Commit 1 - `docs(timbre): add repo-fit descriptor spec and task list` **[hybrid-function]**

**Spec:** sections 1-13

- [x] Review the original descriptor proposal against both repository implementation lines.
- [x] Record the canonical energy model, descriptor domains, validity rules, and branch split.
- [x] Add the complete descriptor inventory, render-control boundaries, and this task list.
- [x] Record descriptors that cannot be recovered from current residual `band_energy`.

### Commit 2 - `test(timbre): add descriptor coverage manifest` **[hybrid-function]**

**Spec:** sections 3-5, section 10

- [ ] Add `tests/fixtures/timbre_descriptor_manifest.json`.
- [ ] List every descriptor with stable id, version, domain, units, weighting, prerequisites,
  implementation phase, and expected status on each fixture category.
- [ ] Mark descriptors as `general`, `conditional_harmonic`, `conditional_waveform`,
  `experimental`, or `unsupported_current_model`.
- [ ] Add `tests/timbre_descriptor_manifest_check.py`.
- [ ] Fail when a descriptor lacks a fixture, prerequisite policy, units, or implementation phase.
- [ ] Register the manifest check in CTest.

### Commit 3 - `test(timbre): add analytical descriptor fixture suite` **[hybrid-function]**

**Spec:** section 10.1

- [ ] Add deterministic synthetic frame builders for single partial, two partials, flat bands,
  dominant band, partial/residual split, known tilt, known fine deviation, and resonance peaks.
- [ ] Add deterministic series builders for moving bumps, attacks, decays, modulation, track
  births/deaths, and residual color motion.
- [ ] Add harmonic, stretched-harmonic, inharmonic, and low-confidence-F0 fixtures.
- [ ] Store expected analytical values and tolerances in one versioned fixture file or helper.
- [ ] Extend [SPECTRAL_ENVELOPE_FIXTURES.md](./SPECTRAL_ENVELOPE_FIXTURES.md) with descriptor
  coverage and provenance rules.

### Commit 4 - `test(timbre): add descriptor regression report schema` **[hybrid-function]**

**Spec:** section 6.4, section 10

- [ ] Define versioned descriptor-report JSON with domain, status, confidence, options, and values.
- [ ] Define tolerance groups for analytical correctness, recorded-fixture regression, series
  trajectories, and endpoint fidelity.
- [ ] Add a report checker that rejects non-finite values, unknown ids, missing statuses, and
  silently changed formulas/versions.
- [ ] Treat analytical fixtures as correctness gates and recorded fixtures as regression gates.

---

## Phase 1 - Shared Core Contract and Energy Distributions

### Commit 5 - `feat(timbre): add descriptor data model and status contract` **[hybrid-function]**

**Spec:** section 6.2

- [ ] Add `src/core/TimbreDescriptors.h/.cpp`.
- [ ] Add `TimbreDescriptorDomain`, `TimbreDescriptorStatus`, `TimbreDescriptorValue`,
  `TimbreDescriptorFrame`, `TimbreDescriptorSeries`, and options structures.
- [ ] Include stable descriptor ids, units, formula/version metadata, and confidence/coverage.
- [ ] Add string conversion and parsing helpers for serialized enums.
- [ ] Keep the module independent of JUCE and `ATS_SOUND`.
- [ ] Add deterministic model/enum serialization tests.

### Commit 6 - `feat(timbre): add canonical frame and track adapters` **[hybrid-function]**

**Spec:** section 6.3, section 7.1

- [ ] Add reusable helpers that normalize `EnvelopeFrameInput` partials and residual bands into
  validated energy samples.
- [ ] Factor the local frame extraction logic currently in
  `PluginProcessor::buildEnvelopeOverlayData` into a reusable off-thread adapter.
- [ ] Add track/series input structures with frame time, partial identity, amplitude/frequency
  trajectories, and optional transient boundaries.
- [ ] Explicitly preserve missing residual, missing waveform, sparse frame, and invalid-point
  states.
- [ ] Add tests for sorting, filtering, selection masks, amplitude floors, and invalid inputs.

### Commit 7 - `feat(timbre): add shared energy distribution helpers` **[hybrid-function]**

**Spec:** section 3.2-section 3.3

- [ ] Build partial-only, residual-only, and combined energy distributions.
- [ ] Add uniform Linear/Bark/Mel/ERB comparison-grid projection using `EnvelopeAxisSpec`.
- [ ] Add fitted-envelope sampling helpers with documented amplitude-to-energy conversion.
- [ ] Preserve total energy within tolerance when projecting onto a comparison grid.
- [ ] Record coverage: active point count, supported frequency span, and represented energy ratio.
- [ ] Add tests for order independence, gain scaling, energy conservation, and empty domains.

### Commit 8 - `feat(timbre): serialize and export descriptor analysis` **[hybrid-function]**

**Spec:** section 6.4

- [ ] Add deterministic JSON or project-native structured serialization for frame and series
  descriptor results.
- [ ] Include descriptor options, axis, floors, weighting, status, confidence, and formula version.
- [ ] Preserve unknown future fields where practical.
- [ ] Add round-trip and stable-order tests.
- [ ] Add an offline export entry point without coupling the core to plugin UI.

---

## Phase 2 - General Per-Frame Descriptors

### Commit 9 - `feat(timbre): compute energy and noisiness descriptors` **[hybrid-function]**

**Spec:** section 5.6

- [ ] Compute partial energy, residual energy, combined energy, global noisiness, and harmonic-to-
  noise ratio compatibility view.
- [ ] Report orphan residual ratio when available.
- [ ] Compute bandwise noisiness on the canonical critical-band/comparison grid.
- [ ] Return `MissingResidual` when residual analysis is unavailable.
- [ ] Add exact partial/residual split tests and global-gain invariance checks.

### Commit 10 - `feat(timbre): compute centroid moments and rolloff` **[hybrid-function]**

**Spec:** section 5.1, section 5.3

- [ ] Compute partial, residual, and combined centroid in Hz.
- [ ] Compute centroid and spread on configured Bark/ERB/Mel axis.
- [ ] Compute spread, skewness, and kurtosis with explicit undefined/sparse behavior.
- [ ] Compute configurable rolloff, including default 85% and 95% values.
- [ ] Add exact analytical tests for single, two-point, and combined distributions.
- [ ] Confirm global gain leaves normalized moments unchanged.

### Commit 11 - `feat(timbre): compute grid shape descriptors` **[hybrid-function]**

**Spec:** section 5.4, section 4.4, section 4.6

- [ ] Compute flatness, crest, entropy, spectral decrease, and spectral strong-peak ratio on a
  regular energy grid.
- [ ] Compute spectral complexity/peak count using an energy-relative threshold.
- [ ] Support residual-only and fitted-envelope domains; do not compute flatness over sparse raw
  partial lists by default.
- [ ] Serialize floors, thresholds, and grid policy.
- [ ] Add flat-grid, single-dominant-band, sloped-grid, and threshold-boundary tests.

### Commit 12 - `feat(timbre): compute fitted-envelope tilt` **[hybrid-function]**

**Spec:** section 5.2

- [ ] Add robust dB-per-octave regression over uniformly sampled reliable `FrameEnvelope` values.
- [ ] Record reference frequency, fit span, fit error, and confidence.
- [ ] Return `MissingEnvelope` or low confidence on unreliable/sparse envelopes.
- [ ] Confirm global gain changes intercept but not tilt.
- [ ] Add known-slope analytical and estimator-comparison tests.

### Commit 13 - `feat(timbre): compute envelope deviation and spectral irregularity` **[hybrid-function]**

**Spec:** section 4.2, section 5.9

- [ ] Compute envelope-deviation RMS in dB for partials relative to the fitted envelope.
- [ ] Implement one named/versioned adjacent-partial irregularity definition.
- [ ] Add optional research comparison implementations for Jensen/Krimphoff-style irregularity
  behind explicit ids; never silently substitute formulas.
- [ ] Distinguish general adjacent-partial irregularity from harmonic spectral deviation.
- [ ] Add smooth-envelope, alternating-amplitude, sparse, and globally scaled tests.

### Commit 14 - `feat(timbre): compute spectral contrast and resonance peaks` **[hybrid-function]**

**Spec:** section 5.5

- [ ] Add estimator-independent peak extraction from sampled `FrameEnvelope`.
- [ ] Report peak frequency, perceptual-axis position, prominence, bandwidth, and confidence.
- [ ] Compute spectral contrast/peak-to-valley depth and peak count above prominence.
- [ ] Compute subband spectral centroids on a serialized grid.
- [ ] Add single/multiple resonance, close-peak, flat-envelope, and unreliable-envelope tests.

---

## Phase 3 - General Series and Temporal Descriptors

### Commit 15 - `feat(timbre): add descriptor series alignment and normalization` **[hybrid-function]**

**Spec:** section 5.7, section 6.2-section 6.3

- [ ] Add `analyzeTimbreDescriptorSeries`.
- [ ] Validate frame hop, source identity, comparison grid, and envelope/frame alignment.
- [ ] Normalize frame energy according to a serialized policy for shape-only comparisons.
- [ ] Accept transient-boundary markers and reset/gate series metrics by default.
- [ ] Add missing-frame, mismatched-grid, and transient-reset tests.

### Commit 16 - `feat(timbre): compute spectral flux and variation trajectories` **[hybrid-function]**

**Spec:** section 5.7

- [ ] Compute positive flux, signed flux, and cosine-distance spectral variation.
- [ ] Compute trajectories separately for partial, residual, and combined energy grids.
- [ ] Add attack-window flux summaries.
- [ ] Verify variation discounts global level changes while flux retains them under the selected
  normalization policy.
- [ ] Add steady, gain-only, moving-bump, onset, and transient-boundary tests.

### Commit 17 - `feat(timbre): compute temporal envelope shape` **[hybrid-function]**

**Spec:** section 4.2, section 5.7

- [ ] Derive a documented event energy envelope from partial plus residual energy.
- [ ] Compute log attack time, temporal centroid, attack slope, decay slope, effective duration,
  maximum-time ratio, and before/after-maximum energy ratio.
- [ ] Serialize thresholds and event-boundary policy.
- [ ] Add fast/slow attack, short/long decay, silence, and multi-event rejection tests.

### Commit 18 - `feat(timbre): compute partial track lifecycle descriptors` **[hybrid-function]**

**Spec:** section 4.6, section 5.11

- [ ] Compute partial onset and release asynchrony.
- [ ] Compute track lifetime mean/spread, energy-weighted lifetime, birth rate, death rate, and
  active partial-density trajectories.
- [ ] Compute per-partial temporal centroid.
- [ ] Gate on valid track continuity and frame timing.
- [ ] Add synchronous/asynchronous attack, diffuse release, short-lived texture, and sustained
  track tests.

### Commit 19 - `feat(timbre): compute partial modulation and coherence` **[hybrid-function]**

**Spec:** section 4.6

- [ ] Compute per-partial AM rate/depth from amplitude trajectories.
- [ ] Compute FM rate/depth from frequency trajectories.
- [ ] Compute cross-partial amplitude-envelope coherence for selected/high-energy tracks.
- [ ] Add optional adapters for mainline DDM AM/FM slope arrays after reconciliation.
- [ ] Serialize window length, rate bands, track selection, and confidence.
- [ ] Add tremolo, vibrato, common-envelope, independent-flutter, and insufficient-duration tests.

### Commit 20 - `feat(timbre): compute residual motion and stationarity` **[hybrid-function]**

**Spec:** section 4.6, section 5.12

- [ ] Compute residual centroid/spread/rolloff/slope/flatness/crest/entropy per frame using shared
  helpers.
- [ ] Compute residual flux, spectral variation, lagged self-similarity/stationarity, and color
  distance.
- [ ] Compute per-band/grouped-band temporal centroid and effective duration.
- [ ] Compute residual modulation energy only in frame-rate-supported modulation bands.
- [ ] Add steady hiss, moving noise color, scrape-like burst, and modulation-Nyquist tests.

---

## Phase 4 - Conditional Harmonic Descriptor Stack

### Commit 21 - `feat(timbre): add reliable-F0 and harmonic assignment gate` **[hybrid-function]**

**Spec:** section 4.3, section 5.8

- [ ] Define harmonic assignment inputs, tolerances, explained-energy ratio, and confidence.
- [ ] Reuse reliable F0 metadata when available; do not infer harmonic labels silently for general
  material.
- [ ] Support slightly inharmonic assignment with bounded deviation.
- [ ] Return `MissingReliableF0` or low confidence on inharmonic/polyphonic/ambiguous frames.
- [ ] Add harmonic, stretched-harmonic, missing-fundamental, inharmonic, and polyphonic tests.

### Commit 22 - `feat(timbre): compute harmonicity and inharmonicity` **[hybrid-function]**

**Spec:** section 4.3, section 5.8

- [ ] Compute harmonic explained-energy ratio/harmonicity.
- [ ] Compute energy-weighted inharmonicity and assignment deviation.
- [ ] Record assigned/unassigned energy and harmonic count.
- [ ] Add tests for exact harmonic, controlled stretch, missing harmonics, and invalid F0.

### Commit 23 - `feat(timbre): compute harmonic spectral shape` **[hybrid-function]**

**Spec:** section 4.3, section 4.6, section 5.8-section 5.10

- [ ] Compute harmonic spectral centroid and spread.
- [ ] Compute harmonic spectral deviation using one named/versioned local-envelope policy.
- [ ] Compute odd/even energy ratio and odd/even harmonic centroids.
- [ ] Compute fundamental dominance and configurable low/mid/high harmonic energy ratios.
- [ ] Compute tristimulus N1/N2/N3.
- [ ] Add square/triangle/saw-like, clarinet-like odd-heavy, missing-harmonic, and level-scaling
  tests.

### Commit 24 - `feat(timbre): compute harmonic spectral variation` **[hybrid-function]**

**Spec:** section 5.10

- [ ] Align consecutive harmonic-amplitude vectors by harmonic number.
- [ ] Serialize missing-harmonic floor/omit policy.
- [ ] Compute harmonic spectral variation and optional attack/sustain summaries.
- [ ] Gate when either frame lacks reliable assignment.
- [ ] Add steady harmonic, moving formant, odd/even change, and missing-harmonic tests.

### Commit 25 - `feat(timbre): add transparent F0-normalized comparisons` **[hybrid-function]**

**Spec:** section 4.3

- [ ] Add centroid/F0, rolloff/F0, and log-frequency-offset comparisons with explicit units.
- [ ] Keep learned per-instrument normalization out until a labeled corpus exists.
- [ ] Add cross-register same-source fixtures and confirm fewer false brightness mismatches.
- [ ] Document where F0 normalization is invalid or misleading.

---

## Phase 5 - Hybrid QA, Fixtures, and Analysis Integration

### Commit 26 - `test(timbre): extend spectral-envelope fixture metrics` **[hybrid-function]**

**Spec:** phase 1, section 10.2

- [ ] Extend `spectral_envelope_fixture_metrics_test.cpp` with centroid, tilt, rolloff, noisiness,
  envelope deviation, contrast, and resonance metrics.
- [ ] Compare target-only transfer against de-color/re-color using envelope-deviation and
  descriptor-target error.
- [ ] Compare residual policies using bandwise noisiness, residual color distance, and residual
  stationarity.
- [ ] Compare transient policies using attack flux, log attack time, and temporal centroid.
- [ ] Record tolerances without weakening existing envelope-fit gates.

### Commit 27 - `test(timbre): add recorded-fixture descriptor baselines` **[hybrid-function]**

**Spec:** section 10

- [ ] Generate descriptor reports for committed ATSA reference fixtures.
- [ ] Include sustained/voice, plucked, inharmonic metal, and residual-heavy examples.
- [ ] Record toolchain/platform tolerance policy; do not require cross-platform bit identity.
- [ ] Add regression checks for descriptor drift and status/confidence changes.

### Commit 28 - `feat(timbre): add worker-side descriptor cache` **[hybrid-function]**

**Spec:** phase 2, section 7.1

- [ ] Cache frame/series descriptors by model revision, descriptor options, envelope estimator
  settings, and frequency axis.
- [ ] Build/invalidate the cache on worker threads.
- [ ] Reuse fitted envelopes and sampled envelope caches where valid.
- [ ] Add cancellation, stale-result, and cache-invalidation tests.
- [ ] Confirm no descriptor analysis runs in `processBlock`.

### Commit 29 - `feat(ui): add compact timbre descriptor readout` **[hybrid-function]**

**Spec:** phase 2

- [ ] Add a compact current-frame/series readout near the envelope workspace.
- [ ] Show centroid, tilt, noisiness, rolloff, irregularity/deviation, and confidence/status.
- [ ] Clearly mark unavailable harmonic/residual descriptors.
- [ ] Avoid exposing every expert descriptor in the first UI.
- [ ] Add accessibility names/help text and stale/rebuilding indicators.

### Commit 30 - `feat(ui): add descriptor trajectory overlays and export` **[hybrid-function]**

**Spec:** phase 2

- [ ] Add optional centroid, noisiness, flux/variation, and residual-stationarity trajectories.
- [ ] Add deterministic descriptor-report export from the UI/offline tool.
- [ ] Add controls for axis/domain selection without changing analysis defaults silently.
- [ ] Keep rendering and export off the audio thread.

---

## Phase 6 - Envelope/Morph Branch Reconciliation

### Commit 31 - `refactor(envelope): define shared envelope compatibility layer` **[after-reconcile]**

**Spec:** section 7.3

- [ ] Inventory overlapping behavior between generalized `SpectralEnvelope*` and mainline
  `AtsSpectralEnvelope.*`.
- [ ] Define one shared Bark conversion, floor, peak-prominence, and sampling contract.
- [ ] Add adapters so mainline `BarkSpectralEnvelope` and generalized `FrameEnvelope` can both feed
  `TimbreDescriptors`.
- [ ] Preserve existing mainline Hybrid/EnvelopeMorph output and tests.
- [ ] Add cross-path descriptor agreement fixtures before replacing either implementation.

### Commit 32 - `test(envelope): gate shared envelope path agreement` **[after-reconcile]**

- [ ] Compare centroid, tilt, resonance peaks, contrast, and sampled envelope values across both
  envelope implementations on shared fixtures.
- [ ] Define acceptable differences where estimator semantics genuinely differ.
- [ ] Fail on duplicated Bark/floor/prominence behavior drifting independently.
- [ ] Confirm mainline spectral-envelope transfer and formant-aligned morph tests remain green.

### Commit 33 - `build(timbre): integrate descriptor core into mainline targets` **[mainline]**

- [ ] Add descriptor core sources to mainline plugin/test CMake targets.
- [ ] Add mainline adapters from `ATS_SOUND`, morph partial tracks, residual bands, transient
  markers, and DDM slope arrays.
- [ ] Keep descriptor analysis in render preparation/offline metrics.
- [ ] Add deterministic adapter tests without changing rendered audio.

---

## Phase 7 - Mainline Morph Metrics and Diagnostics

### Commit 34 - `test(morph): extend metric schema with timbre descriptors` **[mainline]**

**Spec:** phase 4, section 10.2

- [ ] Extend existing morph metric report/tolerances rather than adding a competing harness.
- [ ] Add source A/B and morph endpoint descriptor checks.
- [ ] Add trajectory checks for centroid, tilt, noisiness, deviation, formant peaks, flux, and
  residual stationarity.
- [ ] Pin existing endpoint, transport-cost, churn, count-mismatch, and per-band-power gates.
- [ ] Reject descriptor status/confidence regressions at endpoints.

### Commit 35 - `feat(morph): analyze prepared and trajectory morph renders` **[mainline]**

- [ ] Analyze prepared snapshot outputs and main trajectory outputs on the same descriptor grid.
- [ ] Compare baseline Morph, Hybrid, and EnvelopeMorph behavior.
- [ ] Confirm descriptor endpoints match source A/B within tolerance.
- [ ] Add descriptor diagnostics without modifying correspondence or rendered output.

### Commit 36 - `test(morph): evaluate descriptor-informed correspondence diagnostics` **[mainline]**

- [ ] Add lifetime track centroid, partial onset/lifetime, DDM AM/FM, and band descriptors to
  correspondence reports.
- [ ] Measure whether candidate descriptor costs reduce churn or listening-test failures.
- [ ] Do not add any descriptor to the transport/correspondence cost by default.
- [ ] Require lower churn or better blinded listening results without endpoint/energy regression
  before promoting a new cost.

---

## Phase 8 - Timbre Motion Architecture and First Render Targets

### Commit 37 - `feat(morph): add Timbre Motion recipe schema and control registry` **[mainline]**

**Spec:** sections 3.5, 6.5-6.6, 7.4, 8.4, 8.11

- [ ] Add typed/versioned `TimbreMotionPlan`, target-curve, macro-route, and target-result models.
- [ ] Add a descriptor-control registry with stable ids, roles, units, normalization,
  interpolation, prerequisites, transform ids, bounds, priority, and cost/capability metadata.
- [ ] Keep the six fixed `MorphLane` values unchanged and separate from dynamic descriptor ids.
- [ ] Store audio-affecting curves/routes as typed recipe state; keep focus/pin/filter presentation
  state in deterministic `ui.*` metadata where appropriate.
- [ ] Preserve unknown descriptor ids/future fields and old recipe/session rendering.
- [ ] Add schema migration, disabled-plan output-identity, and deterministic round-trip tests.

### Commit 38 - `feat(morph): evaluate descriptor target curves and modes` **[mainline]**

**Spec:** sections 8.5, 8.9, 8.11

- [ ] Reuse normalized timeline time and `MorphCurveShape` for arbitrary descriptor target curves.
- [ ] Implement `FollowNatural`, `InterpolateAB`, `OffsetNatural`, and `AbsoluteTarget`.
- [ ] Use the registry's descriptor-specific interpolation/normalization and physical bounds.
- [ ] Endpoint-lock all target curves by default and keep endpoint unlock out of the first UI.
- [ ] Add sanitization, point-count limits, curve lookup, smoothing, and deterministic evaluation.
- [ ] Add mode semantics, endpoint, malformed/unknown-id, and curve serialization tests.

### Commit 39 - `feat(morph): add shared motion curves and macro routing` **[mainline]**

**Spec:** sections 8.7-8.8, 8.11

- [ ] Add reusable named motion curves and one-to-many routes with depth, polarity, range,
  smoothing, enable state, and priority.
- [ ] Add versioned factory macros for Brightness, Warmth/Tilt, Tonal/Noise, and analysis-only or
  experimental placeholders for Focus, Resonance, Texture, Attack Character, and Harmonicity.
- [ ] Reject routes to analysis-only/unavailable targets and reject cyclic routing.
- [ ] Resolve multiple routes to one target using a documented deterministic rule.
- [ ] Preserve saved macro mappings when factory definitions evolve.
- [ ] Add routing, polarity, depth, cycle, unavailable-target, and version-recall tests.

### Commit 40 - `feat(morph): add bounded descriptor target resolver and reports` **[mainline]**

**Spec:** sections 8.6, 8.10

- [ ] Measure the natural result, resolve requested targets, apply transforms, then measure
  achieved values.
- [ ] Enforce hard invariants before soft target priorities: endpoints, finite output, pitch,
  configured energy, residual availability, and safe bounds.
- [ ] Use versioned target ordering and a small fixed number of bounded correction passes.
- [ ] Return natural/requested/achieved values, normalized error, confidence, clamp,
  missing-prerequisite, unreachable, and conflict statuses.
- [ ] Preserve the base/last-valid render when a target cannot be resolved.
- [ ] Add coupled-target, priority, clamp, unavailable, conflict, determinism, and performance
  tests.

### Commit 41 - `feat(ui): add Timbre Motion macro rack and focused editor` **[mainline]**

**Spec:** sections 8.7-8.9, 8.12

- [ ] Add a Timbre Motion panel to the existing morph workspace rather than a competing timeline.
- [ ] Add compact macro/target cards with enable, A/B values, current/requested/achieved value,
  units, mini trajectory, confidence/status, depth, pin, focus, and reset.
- [ ] Add one reusable focused editor with normalized/physical-unit views and editable requested
  curve.
- [ ] Add family filter, favorites/search, show-available-only, and up to four compact pinned
  lanes.
- [ ] Permit analysis-only descriptors as focused/pinned overlays without editable handles.
- [ ] Reuse existing undo coalescing and stale/rebuilding/ready states.
- [ ] Add accessible names, keyboard editing/focus, textual status, non-color-only warnings, and
  help text.

### Commit 42 - `feat(morph): add bounded brightness target` **[mainline] [ear]**

**Spec:** section 8.1

- [ ] Target combined Bark/ERB centroid without moving partial frequencies.
- [ ] Apply a bounded gain curve around a serialized pivot frequency.
- [ ] Use deterministic bounded solving during render preparation, not unbounded audio-thread work.
- [ ] Apply configured normalization after the target transform.
- [ ] Add target-error, endpoint, partial-frequency-preservation, and energy tests.
- [ ] Compare Bark versus ERB target behavior and complete listening validation.

### Commit 43 - `feat(morph): add bounded tilt target` **[mainline] [ear]**

**Spec:** section 8.2

- [ ] Apply bounded dB-per-octave gains to partial and residual paths.
- [ ] Normalize partial and residual paths independently according to recipe policy.
- [ ] Keep partial frequencies unchanged.
- [ ] Add exact tilt-target, endpoint, gain-bound, and trajectory-continuity tests.
- [ ] Complete listening validation on bowed/string, voice, and bright metal fixtures.

### Commit 44 - `feat(morph): add tonal-noise balance target` **[mainline] [ear]**

**Spec:** section 8.3

- [ ] Interpolate a target noisiness and redistribute partial/residual energy.
- [ ] Preserve configured total power and orphan residual policy.
- [ ] Report unreachable targets when residual data is missing; do not synthesize residual
  silently.
- [ ] Add endpoint, energy, bandwise noisiness, and missing-residual tests.
- [ ] Complete listening validation on tonal/noisy and breathy fixtures.

### Commit 45 - `feat(ui): show Timbre Motion achieved results and stable automation macros` **[mainline]**

**Spec:** sections 8.10-8.12

- [ ] Overlay natural, requested, and achieved trajectories in the focused editor and mini cards.
- [ ] Show normalized target error plus available, clamped, unavailable, stale/rebuilding,
  unreachable, conflict, and experimental states without relying on color alone.
- [ ] Add stable APVTS/host-automatable depth controls only for brightness, warmth/tilt, and
  tonal/noise; keep arbitrary descriptor curves/routes in recipe state.
- [ ] Make host macro automation modulate the saved motion plan without rewriting breakpoint data.
- [ ] Persist focus, pinned lanes, filters, favorites, and presentation state.
- [ ] Add UI state, automation-recall, and old-session compatibility tests.

### Commit 46 - `test(morph): compare snapshot and trajectory Timbre Motion targets` **[mainline]**

**Spec:** phase 5, sections 8.6, 8.11, 10

- [ ] Confirm preview, bounce, prepared snapshot, and main trajectory paths evaluate one saved
  motion plan identically within tolerance.
- [ ] Confirm descriptor controls do not weaken endpoint fidelity, transport, churn, or power
  gates.
- [ ] Add automation/timeline tests for click-free target and macro changes.
- [ ] Add natural/requested/achieved report, coupled-target, conflict, and unavailable-target
  regression fixtures.
- [ ] Confirm arbitrary descriptor curves do not expand or reorder the host parameter surface.
- [ ] Record listening-test notes and accepted bounds.

---

## Phase 9 - Additional Classic-Era Analysis Descriptors

### Commit 47 - `feat(timbre): add sensory dissonance and roughness analysis` **[research]**

- [ ] Implement a named/versioned pairwise-partial critical-band dissonance model.
- [ ] Keep O(N^2) analysis worker/offline and add bounded candidate optimization if needed.
- [ ] Distinguish instantaneous sensory dissonance from AM-rate roughness.
- [ ] Add known interval/partial-pair and dense-frame performance tests.
- [ ] Do not add an audible roughness lane in this commit.

### Commit 48 - `feat(timbre): add calibrated sharpness and perceptual spread` **[research]**

- [ ] Define/calibrate the critical-band specific-loudness input contract.
- [ ] Implement sharpness and perceptual spread only when calibration prerequisites are met.
- [ ] Return explicit unsupported/unavailable status for raw uncalibrated ATS energy.
- [ ] Add calibrated analytical and scale-sensitivity tests.

### Commit 49 - `feat(timbre): add partial phase coherence diagnostics` **[research]**

- [ ] Gate on retained reliable phases and, where required, harmonic assignment.
- [ ] Compute named phase-coherence/phase-deviation diagnostics for attacks and stable frames.
- [ ] Record phase availability/confidence.
- [ ] Add deterministic synthetic phase-alignment tests.
- [ ] Keep QPC/SOS/bispectrum out unless a separate proposal proves value.

### Commit 50 - `feat(timbre): add spectral basis and cepstral projections` **[research]**

- [ ] Add MFCC/GFCC-style projections from a documented regular band-energy grid.
- [ ] Add optional fixed PCA/spectral-basis projections with serialized basis id/version/hash.
- [ ] Fit bases only offline on a defined corpus/session.
- [ ] Use projections for retrieval/distance/visualization, not direct render controls.
- [ ] Add basis round-trip, scale policy, and determinism tests.

### Commit 51 - `research(timbre): evaluate semantic timbre models` **[research]**

- [ ] Prototype hardness, depth, warmth, boominess, and related semantic predictions offline.
- [ ] Document model provenance, license, required waveform features, and failure domains.
- [ ] Compare against deterministic descriptors and listening labels.
- [ ] Do not add a runtime/plugin dependency or product control without a separate approval gate.

---

## Phase 10 - Experimental Audible Controls

Each commit in this phase requires a separate go/no-go decision after its analysis descriptor has
proven useful. None belongs in the initial descriptor-control release.

### Commit 52 - `research(morph): prototype spread/focus control` **[mainline] [research] [ear]**

- [ ] Prototype gain-only spectral focus before any partial-frequency remapping.
- [ ] Measure centroid, pitch, energy, and endpoint preservation.
- [ ] Reject or redesign if a useful spread change cannot be achieved without pitch/formant
  damage.
- [ ] Add listening validation before promotion.

### Commit 53 - `research(morph): prototype roughness control` **[mainline] [research] [ear]**

- [ ] Define whether the control uses detuning, AM, or both.
- [ ] Preserve endpoint and energy contracts and bound introduced beating.
- [ ] Compare target roughness descriptor against audible artifacts.
- [ ] Keep disabled by default until listening validation passes.

### Commit 54 - `research(morph): prototype inharmonicity target` **[mainline] [research] [ear]**

- [ ] Gate on reliable harmonic assignment and suitable material.
- [ ] Define bounded partial-frequency offsets with pitch/register protection.
- [ ] Add endpoint, crossing, phase-continuity, and correspondence-stability tests.
- [ ] Reject use on general inharmonic/polyphonic material.

### Commit 55 - `research(morph): prototype transient hardness target` **[mainline] [research] [ear]**

- [ ] Tie the target to attack flux/log-attack-time descriptors and the explicit transient path.
- [ ] Preserve source transient policy and avoid double-counting attack energy.
- [ ] Add attack sharpness, peak, energy, and click tests.
- [ ] Promote only if listening benefit exceeds the existing transient-policy controls.

### Commit 56 - `research(morph): evaluate formant descriptor target integration` **[mainline] [research] [ear]**

- [ ] Use resonance position/prominence/bandwidth descriptors to evaluate existing EnvelopeMorph.
- [ ] Prefer improving existing formant-aligned envelope morph over adding a competing formant
  lane.
- [ ] Add resonance trajectory and double-image/dip metrics.
- [ ] Complete voice/vowel and instrument-resonance listening validation.

---

## Phase 11 - Residual Representation Extensions for Unsupported Descriptors

The current residual model stores critical-band energy, not residual samples or phase. These
commits are optional representation work required before sample-distribution descriptors can be
implemented honestly.

### Commit 57 - `docs(residual): specify retained residual analysis metadata` **[research]**

- [ ] Decide whether to retain residual waveform excerpts, per-band moments, AR coefficients,
  modulation summaries, or another compact representation.
- [ ] Define file/state compatibility, memory limits, determinism, and privacy/licensing impact.
- [ ] Keep existing ATS files and models backward-readable.
- [ ] Select only metadata with a demonstrated product or QA use.

### Commit 58 - `feat(residual): add optional residual waveform/statistics metadata` **[research]**

- [ ] Implement the approved additive representation behind an explicit analysis option.
- [ ] Zero-initialize missing metadata and preserve legacy rendering.
- [ ] Add save/load, memory, deterministic analysis, and fallback tests.

### Commit 59 - `feat(timbre): add residual waveform descriptors` **[research]**

- [ ] Compute only descriptors supported by the retained representation: possible kurtosis,
  impulsiveness, zero-crossing rate, AR/colored-noise coefficients, or higher-order statistics.
- [ ] Compute dense-spectrum high-frequency content only when the retained waveform/spectrum
  representation and analysis bandwidth support it.
- [ ] Record exact source representation and prerequisites.
- [ ] Add waveform-ground-truth fixtures and comparison against band-energy-only unavailable
  statuses.
- [ ] Keep HOS/bispectrum/QPC experimental unless a focused gate proves value.

---

## Phase 12 - Performance, Release, and Documentation

### Commit 60 - `perf(timbre): benchmark descriptor analysis and caching` **[hybrid-function]**

- [ ] Benchmark basic frame, envelope-relative, series, harmonic, residual, roughness, and export
  costs separately.
- [ ] Benchmark Timbre Motion curve/routing evaluation, target solving, achieved-result analysis,
  and target-cache invalidation separately after mainline integration.
- [ ] Confirm basic frame descriptors remain O(partials + residual bands + grid size).
- [ ] Confirm series analysis remains worker/offline and bounded by frames * grid size.
- [ ] Confirm the target resolver uses a fixed bounded correction-pass count and no audio-thread
  fitting/analysis.
- [ ] Add advisory warnings/limits for O(N^2) roughness and large series exports.
- [ ] Record soft limits in a descriptor performance document or existing performance docs.

### Commit 61 - `test(timbre): add determinism and compatibility sweep` **[after-reconcile]**

- [ ] Run all descriptor analytical, fixture, envelope, hybrid, morph, transport, and round-trip
  tests together.
- [ ] Confirm old plugin state and recipes render unchanged by default.
- [ ] Confirm disabled/follow-natural Timbre Motion plans are output preserving and old host
  parameter ordering remains stable.
- [ ] Confirm missing prerequisites produce statuses rather than crashes/NaNs.
- [ ] Confirm cross-platform policy uses tolerances rather than bit hashes where appropriate.

### Commit 62 - `docs(timbre): publish descriptor reference and implementation status` **[after-reconcile]**

- [ ] Document every descriptor id, formula version, domain, units, prerequisites, and
  interpretation.
- [ ] Document analysis-only versus render-control status.
- [ ] Document Timbre Motion target modes, macro mappings, routing, target priority, hard
  invariants, natural/requested/achieved feedback, and host-automation boundaries.
- [ ] Document unsupported descriptors and required model extensions.
- [ ] Mark completed commits and record listening-validation outcomes.
- [ ] Add release notes and user-facing help for shipped controls/readouts.

---

## Dependency Index

| Concern | Depends on |
|---------|------------|
| Any descriptor implementation | Commits 2-7 |
| Serialization/export | Commits 5-8 |
| General per-frame descriptors | Commits 5-7 |
| Envelope-relative descriptors | Existing envelope estimators plus Commits 5-7 |
| General series descriptors | Commits 6-7, 15 |
| Partial lifecycle/modulation descriptors | Valid track-series adapter from Commit 6 |
| Residual motion descriptors | Residual-band series from Commits 6-7 |
| Harmonic descriptors | Reliable-F0/harmonic assignment from Commit 21 |
| Plugin UI/cache | Commits 8-20 |
| Mainline morph diagnostics | Commits 31-35 |
| Timbre Motion control architecture and UI | Commits 31-41 |
| Audible brightness/tilt/noise targets | Commits 31-46 |
| Roughness/sharpness/basis experiments | Stable analysis core plus their explicit prerequisites |
| Residual sample-distribution descriptors | Commits 57-58 |

---

## Descriptor Coverage Index

| Descriptor family | Primary commits |
|-------------------|-----------------|
| Descriptor status, confidence, and coverage | 2, 4-8 |
| Energy, noisiness, harmonic-to-noise, bandwise noisiness | 9 |
| Centroid, spread, skewness, kurtosis, rolloff | 10 |
| Flatness, crest, entropy, decrease, strong peak, complexity | 11 |
| Tilt | 12 |
| Envelope deviation and adjacent-partial irregularity | 13 |
| Contrast, resonance/formant peaks, SSC | 14 |
| Flux, variation, attack flux | 15-16 |
| Log attack time, temporal centroid, attack/decay/effective duration | 17 |
| Onset/release asynchrony, lifetimes, density, births/deaths | 18 |
| Partial AM/FM and cross-partial coherence | 19 |
| Residual color, variation, stationarity, modulation, temporal shape | 20 |
| Harmonicity and inharmonicity | 21-22 |
| Harmonic centroid/spread/deviation, odd/even, dominance, tristimulus | 23 |
| Harmonic spectral variation | 24 |
| F0-normalized comparisons | 25 |
| Roughness/sensory dissonance | 47 |
| Sharpness and perceptual spread | 48 |
| Phase coherence/deviation | 49 |
| MFCC/GFCC/PCA/spectral basis | 50 |
| Semantic hardness/depth/warmth/boominess | 51 |
| Timbre Motion schema, modes, routing, resolver, UX, and feedback | 37-41, 45-46 |
| Brightness, tilt, tonal/noise render targets | 42-46 |
| Spread, roughness, inharmonicity, hardness, formant experiments | 52-56 |
| Residual waveform-only descriptors/HOS/AR/ZCR/dense-spectrum HFC | 57-59 |

---

## Cross-Cutting Definition of Done

- [ ] Every shipped descriptor has stable id, version, domain, units, options, prerequisite
  policy, status, confidence, analytical fixture, and regression coverage.
- [ ] Every normalized descriptor is invariant to uniform global gain within tolerance.
- [ ] Every series descriptor documents frame-rate and transient-boundary behavior.
- [ ] Harmonic descriptors never run silently on unreliable or general inharmonic/polyphonic
  material.
- [ ] Residual-band descriptors never claim access to residual waveform statistics.
- [ ] All serialized values are finite and deterministic within the documented toolchain/platform
  tolerance policy.
- [ ] No descriptor analysis runs on the audio thread.
- [ ] Fixed synthesis lanes and dynamic descriptor controls remain distinct in code, recipe state,
  host automation, and UI.
- [ ] Disabled and `FollowNatural` motion plans preserve the base render.
- [ ] Target curves are endpoint locked by default; conflicts and unreachable targets are explicit.
- [ ] Natural, requested, achieved, error, confidence, and target status are available for every
  active render target.
- [ ] Preview, bounce, snapshot, and timeline rendering evaluate the same saved motion plan.
- [ ] Existing envelope, hybrid, morph, transport, endpoint, energy, and round-trip tests remain
  green.
- [ ] Every audible commit marked **[ear]** receives recorded listening validation before
  promotion.
- [ ] Experimental descriptors and controls remain opt-in until they pass their stated numerical
  and listening gates.

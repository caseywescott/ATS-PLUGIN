# Task List: Spectral-Envelope Transfer in Coherent Commits

**Source:** [SPECTRAL_ENVELOPE_PRD.md](./SPECTRAL_ENVELOPE_PRD.md)
**How to use:** Each section is intended to become one reviewable commit. Keep commits buildable and testable. If a section grows too large, split it along the same ownership boundary.

---

## Phase 0 - Planning, Fixtures, and Baselines

### Commit 1 - `docs: add spectral envelope transfer requirements`

**PRD:** section 1-section 9

- [x] Add/maintain the spectral-envelope PRD and this task list.
- [x] Record the default estimator decision: weighted regularized discrete cepstrum for ATS partial frames.
- [x] Record the non-speech requirement explicitly: no F0 or harmonic grid required by default.
- [x] Link the spectral-envelope plan from the modern ATSH planning docs if useful.

### Commit 2 - `test: add spectral envelope fixture manifest`

**PRD:** section 7.1-section 7.2

- [x] Define fixture categories: sustained pitched, voice, singing, inharmonic metal/bell, noisy texture, plucked/percussive, dense polyphony, sparse synthetic edge cases.
- [x] Document provenance and license for each fixture.
- [x] Define metrics: fit error, smoothness, shimmer, transfer discontinuities, residual energy preservation, transient sharpness.

### Commit 3 - `test: add target-only versus decolor-transfer baselines`

**PRD:** section 5.4, section 7.3

- [x] Implement a simple target-only amplitude scaling baseline for comparison.
- [x] Implement an offline decolor-plus-target-color reference script or test helper.
- [x] Add fixture expectations showing why double-coloring is a risk.

---

## Phase 1 - Envelope Data Model and Interface

### Commit 4 - `feat(envelope): add frame envelope model and diagnostics`

**PRD:** section 5.1, section 6.3

- [x] Add serializable frame-envelope structures with estimator id/version, frame id, frequency axis, coefficients or sampled values, and diagnostics.
- [x] Store fit point count, weighted error, fallback reason, and warning flags.
- [x] Add deterministic serialization tests.

### Commit 5 - `feat(envelope): introduce estimator interface`

**PRD:** section 5.3, section 6.1

- [x] Add a common estimator interface for prepare, estimate, evaluate, serialize, and diagnostics.
- [x] Implement an estimator registry with stable ids.
- [x] Add a no-op/sample envelope estimator for tests and UI scaffolding.

### Commit 6 - `feat(envelope): add frequency warp utilities`

**PRD:** section 5.2, section 5.5

- [x] Add linear, Bark, mel, ERB, and normalized-Nyquist frequency mapping helpers.
- [x] Add inverse/evaluate helpers where needed for display and lookup.
- [x] Add tests for monotonicity, range handling, and edge frequencies.

---

## Phase 2 - Weighted Regularized Discrete Cepstrum

### Commit 7 - `feat(envelope): implement regularized discrete cepstrum solver`

**PRD:** section 5.2

- [x] Fit log-amplitude/dB points using cosine cepstral basis functions.
- [x] Add Tikhonov regularization with first-derivative and second-derivative penalties.
- [x] Solve with numerically stable linear algebra available in the project/toolchain.
- [x] Add synthetic tests with known smooth envelopes.

### Commit 8 - `feat(envelope): add ATS partial weighting policies`

**PRD:** section 5.2

- [x] Derive weights from SMR, amplitude, track confidence, user selection, and optional stability data.
- [x] Add clamping and floor policies for low amplitudes and unreliable partials.
- [x] Add tests showing weak/noisy partials influence the envelope less under the default policy.

### Commit 9 - `feat(envelope): handle sparse and ill-conditioned frames`

**PRD:** section 5.2, section 8

- [x] Add order reduction when usable point count is low.
- [x] Increase regularization or fall back to sampled/interpolated envelopes when diagnostics fail.
- [x] Record fallback decisions in diagnostics.
- [x] Add tests for missing low bands, clustered partials, sparse high-pitch-like frames, and inharmonic partial sets.

### Commit 10 - `feat(envelope): add temporal smoothing for envelope series`

**PRD:** section 5.4, section 7.2

- [x] Smooth coefficients or sampled log-envelope values across frames.
- [x] Avoid smoothing across strong transient markers by default.
- [x] Add shimmer metrics for sustained fixtures.

### Research follow-up - `research(envelope): perceptual curvature and gap fill`

**PRD:** section 5.2, section 8

- [x] Replace the old second-derivative `k^4` coefficient prior with an explicit curvature penalty measured on a Bark-warped frequency grid.
- [x] Reuse the previous frame's fitted envelope as low-weight virtual support points when the current frame is spatially clustered.
- [x] Add regression coverage for Bark-space curvature reduction and cross-frame conditioning improvement.

---

## Phase 3 - Envelope Evaluation and Visualization Support

### Commit 11 - `feat(envelope): evaluate envelopes at arbitrary frequencies`

**PRD:** section 5.3, section 5.5

- [x] Evaluate fitted envelopes at source partial frequencies, target mapped frequencies, and visualization grids.
- [x] Guard out-of-range lookup with documented extrapolation policies.
- [x] Add tests for direct Hz, normalized, perceptual, and range-warped lookups.

### Commit 12 - `feat(envelope): add sampled envelope cache`

**PRD:** section 5.1, section 8

- [x] Add optional cache generation for UI overlays and render preparation.
- [x] Keep coefficients as the serialized source of truth where possible.
- [x] Add cache invalidation when estimator settings or source model ids change.

### Commit 13 - `feat(ui): show source target and transfer envelope overlays`

**PRD:** section 5.8

- [x] Add read-only overlays for source envelope, target envelope, and resulting transfer gain.
- [x] Display estimator warnings and unreliable frame states.
- [x] Keep visualization computation off the audio thread.

---

## Phase 4 - Hybridization Recipe and Transform

### Commit 14 - `feat(hybrid): add spectral envelope transfer recipe`

**PRD:** section 5.4, section 6.2

- [x] Add serializable recipe with source/target model ids, estimator settings, de-color amount, target-color amount, envelope mix, normalization, mapping, residual policy, and transient policy.
- [x] Keep recipe separate from source models and generated model snapshots.
- [x] Add state recall tests.

### Commit 15 - `feat(hybrid): implement log-domain decolor and recolor`

**PRD:** section 5.4, section 7.3

- [x] Apply source de-color and target color in log amplitude with amplitude floors.
- [x] Support target-only mode as an explicit creative option, not the default.
- [x] Add tests that compare transfer curves against known envelopes.

### Commit 16 - `feat(hybrid): implement frequency mapping modes`

**PRD:** section 5.5

- [x] Add direct Hz, normalized Nyquist, perceptual-warp, range-warp, and correspondence-based lookup modes.
- [x] Make harmonic-number mapping optional and gated by reliable harmonicity/F0 metadata.
- [x] Add non-harmonic fixture tests to ensure no harmonic assumption leaks into defaults.

### Commit 17 - `feat(hybrid): add gain normalization policies`

**PRD:** section 5.4, section 7.2

- [x] Add none, frame RMS, energy-preserving, and loudness-like normalization options.
- [x] Apply normalization separately for sinusoidal partials and residual/noise paths.
- [x] Add tests for avoiding large frame-to-frame gain jumps.

---

## Phase 5 - Residual, Noise, and Transient Policies

### Commit 18 - `feat(hybrid): add residual envelope transfer policies`

**PRD:** section 5.6

- [x] Support residual unchanged, target-colored, decolored-plus-colored, crossfaded, and independent residual-envelope morph policies.
- [x] Preserve orphan residual-band energy from modern ATSH models.
- [x] Add tests for noisy and residual-heavy fixtures.

### Commit 19 - `feat(hybrid): add residual-band envelope estimator`

**PRD:** section 5.6

- [x] Estimate a broad residual/noise color curve from critical-band energy.
- [x] Evaluate residual transfer independently from partial envelope transfer.
- [x] Add deterministic noise seed handling for render comparisons.

### Commit 20 - `feat(hybrid): add transient-aware transfer policies`

**PRD:** section 5.7

- [x] Consume transient markers from modern ATSH analysis.
- [x] Add preserve, target-color, crossfade, and bypass policies for transient windows.
- [x] Prevent default time smoothing across strong transients.
- [x] Add plucked/percussive fixture checks for attack smearing and clicks.

---

## Phase 6 - Optional Estimators and Comparisons

### Commit 21 - `feat(envelope): add true-envelope estimator option`

**PRD:** section 2.3, section 5.3

- [x] Implement or wrap an iterative true-envelope estimator for dense spectra or sampled model frames.
- [x] Add Hamming/lifter smoothing options if useful.
- [x] Compare against RDC on high-pitched/sparse and dense fixtures.

### Commit 22 - `feat(envelope): add LPC envelope option`

**PRD:** section 2.3

- [x] Add LPC/all-pole estimator as a lightweight option.
- [x] Document cases where it may overfit isolated partials.
- [x] Add fixture comparisons against RDC and true envelope.

### Commit 23 - `research: evaluate F0-adaptive waveform envelope import`

**PRD:** section 2.3, section 3.2

- [x] Prototype importing or generating CheapTrick/WORLD-style envelopes only when waveform and reliable F0 are available.
- [x] Keep this path optional and non-default.
- [x] Document where it helps singing/voice and where it fails general material.

---

## Phase 7 - Plugin Integration and Presets

### Commit 24 - `feat(ui): add spectral envelope transfer workspace`

**PRD:** section 5.8

- [x] Add source and target model slots.
- [x] Add estimator preset selector and transfer controls.
- [x] Add render/preview controls with A/B comparison.

### Commit 25 - `feat(params): expose automatable envelope transfer controls`

**PRD:** section 5.4, section 5.8

- [x] Expose de-color amount, target-color amount, envelope mix, residual mix/color, and transient policy where host automation makes sense.
- [x] Smooth control changes to avoid zippering in prepared renders or real-time snapshots.
- [x] Add state migration for older sessions.

### Commit 26 - `feat(presets): add general-source envelope presets`

**PRD:** section 5.8

- [x] Add presets for sustained instrument, voice/singing, inharmonic metal, noisy texture, plucked/percussive, and conservative all-purpose material.
- [x] Make the all-purpose preset non-speech-specific.
- [x] Store preset metadata explaining estimator, warp, regularization, residual, and transient choices.

### Commit 27 - `perf(envelope): document fitting and render budgets`

**PRD:** section 6.1, section 7.2-section 7.3

- [x] Profile envelope fitting and hybrid render preparation on representative fixtures.
- [x] Document soft limits for frames, partials, envelope order, and sampled cache size.
- [x] Add guardrails and UI warnings for expensive settings.

---

## Dependency Index

| Concern | First commit |
| --- | --- |
| Non-speech default stance | 1 |
| Fixture coverage | 2 |
| Frame envelope model | 4 |
| Estimator interface | 5 |
| Frequency warps | 6 |
| Weighted RDC estimator | 7 |
| Sparse-frame fallback | 9 |
| Hybrid recipe | 14 |
| Decolor/recolor transform | 15 |
| Residual/noise handling | 18 |
| Transient-aware transfer | 20 |
| Optional estimators | 21-23 |
| Plugin workspace and presets | 24-26 |

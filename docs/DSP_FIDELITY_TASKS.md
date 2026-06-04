# Task List: DSP Core Fidelity in Coherent Commits

**Source:** [DSP_FIDELITY_PRD.md](./DSP_FIDELITY_PRD.md)
**Branch:** `dsp-fidelity-tier1`
**How to use:** Each numbered section is intended to become one reviewable commit. Keep commits buildable and testable. C-core sections (Phase 1–6) ship as patches under `patches/` and must keep the CMake marker guards + CI green (see PRD §9). Sections that change rendered audio are flagged **[ear]** — green tests are necessary but not sufficient.

---

## Phase 0 — Spec & Measurement Baseline

### Commit 1 — `docs: add DSP fidelity PRD and task list` ✅

**PRD:** §1–§11

- [x] Add the DSP fidelity PRD and this task list.
- [x] Record the baseline policy: numerical-truth-first, representation stability, vendored-core patch discipline.
- [x] Document current-state ground truth (estimator/phase/tracking/residual/synthesis/morph) with file refs.
- [x] Link the morph PRD, Smearnov spec, determinism, and performance docs.

Implementation note: Added `docs/DSP_FIDELITY_PRD.md` + this list. Ground-truth table cites real refs (`peak-detection.c:101` parabolic, `:112` the `// wrong method` phase). PRD §9 codifies that qatsh C changes ship as `patches/` enforced by CMake markers + CI.

### Commit 2 — `test: add sinusoidal estimator accuracy harness` ✅

**PRD:** §8

- [x] Add a synthetic-signal generator: pure tone and two closely-spaced tones with exactly known frequency/amplitude/phase. (Chirp/AM/tone-in-noise cases deferred to Phase 2/3 where the estimators that need them land.)
- [x] Add measurement of frequency/amplitude error against ground truth across fractional-bin sweeps and two zero-pad regimes.
- [x] Record baseline error of the *current* `parabolic_interp` + `phase_interp`.
- [x] Register `estimator_accuracy_test` as a new CTest target (links `atsa`, replicates the tracker FFT layout).

Implementation note: `tests/estimator_accuracy_test.c`. **Key finding:** the live analyzer (Blackman-Harris, 2× zero-pad) is already near-exact on isolated stationary tones — 0.064 cents / 0.0018 dB worst-case. Unpadded regime is 0.534 cents / 0.031 dB (the QIFFT target). Two tones ~1.5 bins apart collapse to **1** peak (the reassignment/multi-res target). So Tier 1's real payoff is concentrated in Phase 2 (reassignment), Phase 3 (DDM non-stationary), and Phase 4 (multi-res) — not Phase 1 on clean tones. 11/11 CTest green.

### Commit 3 — `test: extend morph metrics with transport cost and continuity`

**PRD:** §8

- [ ] Add transport-cost and correspondence-churn metrics to the morph metric harness.
- [ ] Add a pop-free continuity check across the morph sweep for count-mismatched source pairs.
- [ ] Pin endpoint-fidelity tolerances (amount 0/1) so OT work cannot regress them.

---

## Phase 1 — QIFFT Bias Correction

### Commit 4 — `feat(atsa): window-aware QIFFT peak estimation`

**PRD:** §6.1, §9

- [ ] Implement bias-corrected quadratic-interpolation FFT estimation (Abe & Smith) in `peak-detection.c`, parameterized by window type.
- [ ] Replace/guard the dB-parabolic path so it degrades to current behavior when correction is disabled.
- [ ] Regenerate `patches/qatsh-atsa-fidelity-tier1.patch`; add a marker guard in `CMakeLists.txt` and the apply script.
- [ ] Show measurable frequency/amplitude error reduction in the estimator harness; update CI clone+patch step.

---

## Phase 2 — Spectral Reassignment / Correct Phase

> **Reordered to front:** the Phase 0 harness showed the parabolic estimator is
> already near-exact on padded isolated tones, so QIFFT (Phase 1) buys little.
> Reassignment is the higher-value change — it both sharpens frequency and gives
> the correct phase (fixing the `// wrong method` shortcut) — so it goes first.

### Commit 5a — `test: prototype derivative-window reassignment (numerical gate)` ✅

**PRD:** §6.2, §8

- [x] Add a derivative-of-window auxiliary FFT and reassigned-frequency estimator to `estimator_accuracy_test.c`.
- [x] Demonstrate it beats parabolic in the unpadded regime (PRD §8 gate before touching the vendored core).

Implementation note: Reassignment cut worst-case frequency error from **0.534 → 0.019 cents (~29×)** unpadded and **0.064 → 0.010 cents (~6×)** in the live 2× regime. Sign of the Auger-Flandrin correction flips because qatsh `fft(...,1)` uses the `+j` convention (per `to_polar`'s `atan2(-y,x)`). Two closely-spaced tones still collapse to one peak — reassignment sharpens a *resolved* peak but adds no resolution; that remains the Phase 4 target. The gate is met, so the qatsh integration (5b) is justified.

### Commit 5b — `feat(atsa): reassigned instantaneous frequency and true phase`  **[ear]**

**PRD:** §6.2, §9

- [ ] Port the validated reassignment estimator into `peak-detection.c` (derivative window built once alongside the analysis window in `tracker.c`).
- [ ] Replace the linear `phase_interp` shortcut with the correct peak phase; remove the `// wrong method` path.
- [ ] Validate on moving-partial fixtures (chirp/vibrato) and confirm round-trip/determinism.
- [ ] Regenerate the patch, add CMake markers + apply-script entry, update CI.

---

## Phase 3 — DDM Within-Frame Slopes  **[ear]**

### Commit 6 — `feat(atsa): estimate per-partial AM/FM slopes (DDM)`

**PRD:** §6.3, §9

- [ ] Implement Distribution Derivative Method estimation of per-partial frequency slope (chirp) and amplitude slope per hop.
- [ ] Add additive, zero-initialized slope aux arrays to the model; ensure save/load is backward-readable.
- [ ] Feed slopes into cubic-phase / linear-AM synthesis interpolation.
- [ ] Harness: reduced error on chirp and vibrato fixtures; round-trip preserves slope data.

---

## Phase 4 — Multi-Resolution Analysis Front-End

### Commit 7 — `feat(atsa): multi-window peak analysis and cross-resolution merge`

**PRD:** §6.4, §9

- [ ] Add a multi-window (or constant-Q / Gabor) analysis pass: long windows low, short windows high.
- [ ] Merge peaks across resolutions before tracking, with a de-duplication/selection rule.
- [ ] Parameterize so single-resolution remains the default/fallback.
- [ ] Harness: improved resolution of closely-spaced low partials without smearing highs; patch/markers/CI.

---

## Phase 5 — Sines + Transients + Noise  **[ear]**

### Commit 8 — `feat(atsa): explicit transient layer in the analysis model`

**PRD:** §6.5, §9

- [ ] Reuse energy-flux onset detection (`detectMorphTransients`) to extract a transient layer separate from sinusoids and band noise.
- [ ] Store transient events additively (time, band/energy descriptor); zero-init backward-readable.
- [ ] Re-inject transients at synthesis so attacks are preserved rather than smeared.
- [ ] Onset-time / attack-energy preservation metrics on percussive fixtures; coordinate with Smearnov path.

---

## Phase 6 — IFFT Synthesis

### Commit 9 — `feat(atsa): overlap-add inverse-FFT synthesis`

**PRD:** §6.6, §9

- [ ] Implement FFT⁻¹ (Rodet-Depalle) overlap-add synthesis as an alternative to the oscillator bank.
- [ ] Validate output parity against the oscillator bank within tolerance on harmonic + transient fixtures.
- [ ] Benchmark cost vs. partial count (target: flat/lower) per [PERFORMANCE.md](./PERFORMANCE.md).
- [ ] Keep oscillator-bank path available as fallback; patch/markers/CI.

---

## Phase 7 — Optimal-Transport Morph Correspondence  **[ear]**

### Commit 10 — `feat(morph): optimal-transport partial correspondence`

**PRD:** §7.1

- [ ] Implement a 1D optimal-transport plan between A/B partial distributions on a perceptual frequency axis weighted by amplitude/energy (optionally entropic/Sinkhorn).
- [ ] Handle count mismatch by splitting/merging mass rather than abrupt birth/death.
- [ ] Integrate at `buildFrequencyRankCorrespondence`; feed `renderMorphTrajectoryBuffer`. Keep it deterministic and off-thread.
- [ ] Preserve per-band energy bookkeeping; no endpoint-fidelity regression (Commit 3 tolerances).

### Commit 11 — `test: optimal-transport morph numerical checks`

**PRD:** §7.1, §8

- [ ] Unit-test the transport plan: identity when A==B, mass conservation, monotone continuity in morph amount.
- [ ] Verify pop-free sweeps and reduced churn vs. greedy pairing on count-mismatched pairs.
- [ ] Confirm determinism across runs and round-trip from saved recipe state.

---

## Cross-Cutting / Definition of Done

- [ ] All 10 existing CTest targets stay green at every commit; new targets added per phase.
- [ ] Determinism and `ats_roundtrip` guarantees intact; all new aux data is additive and zero-initialized.
- [ ] Every C-core change is patch-delivered, marker-guarded in CMake, and enforced on a fresh `qatsh/` clone in CI.
- [ ] **[ear]**-flagged commits (6, 8, 10) get listening validation before being called done.

# Product Requirements Document: DSP Core Fidelity (Tier 1 + Optimal-Transport Morph)

**Document status:** Draft
**Last updated:** 2026-06-04
**Related docs:** [DSP_FIDELITY_TASKS.md](./DSP_FIDELITY_TASKS.md), [MORPH_TO_ATS_ANALYSIS_PRD.md](./MORPH_TO_ATS_ANALYSIS_PRD.md), [SMEARNOV_SPEC.md](./SMEARNOV_SPEC.md), [DETERMINISM.md](./DETERMINISM.md), [PERFORMANCE.md](./PERFORMANCE.md)

---

## 1. Executive Summary

The ATSA analysis/synthesis core is a faithful 1990s SMS / McAulay-Quatieri pipeline: a single fixed-window STFT, parabolic (dB-domain) peak interpolation, linearly-interpolated peak phase, stationary-sinusoid-per-frame partials, nearest-frequency greedy tracking, and a critical-band stochastic residual. Each of those stages has a well-understood, numerically-verifiable upgrade that raises analysis/resynthesis fidelity **without leaving the deterministic, editable partial+residual representation** that is the entire point of ATS.

This document scopes that upgrade into two bands of work:

- **Tier 1 — in-paradigm front-end and synthesis refinements.** Better sinusoidal parameter estimation (QIFFT bias correction, spectral reassignment, DDM within-frame slopes), multi-resolution analysis, an explicit transient layer (Sines + Transients + Noise), and IFFT-based synthesis.
- **Tier 2 (one item) — optimal-transport morph correspondence.** Replace greedy band-local partial pairing with a mass-conserving optimal-transport plan so morphs interpolate spectra continuously and stop popping when partial counts differ.

Everything here keeps ATS's reason for existing: a transparent, inspectable, editable list of partials and a separable noise model. Neural / DDSP approaches are explicitly out of scope (they trade away editability — see §4).

This work is the realistic perceptual ceiling of the ATS paradigm. The biggest concrete wins are: a real transient layer instead of smear-compensation, DDM slopes for clean vibrato/glides, and optimal-transport correspondence for pop-free morphs.

---

## 2. Baseline Policy

1. **Numerical truth first.** No estimator or synthesis change merges without a synthetic-signal accuracy test that demonstrates the improvement against a known ground truth (frequency, amplitude, phase, chirp rate, transport cost). This honors the project's standing "literature-correct, back changes with numerical checks" rule.
2. **Representation stability.** Prefer changes that improve the *estimate* feeding the existing `ATS_SOUND` model over changes that alter its on-disk schema. Where a schema/aux-data extension is unavoidable (DDM slopes, transient layer), it must be additive, zero-initialized, and backward-readable.
3. **Vendored-core discipline.** All changes to `qatsh/src/atsa/*.c|*.h` ship as patches under `patches/`, are applied by `scripts/apply_*.sh`, are enforced by `ATS_REQUIRE_QATSH_*` marker checks in `CMakeLists.txt`, and are exercised in CI. `qatsh/` itself stays gitignored and CI-cloned (see §9).
4. **Off-thread, deterministic.** All new analysis/morph work runs on worker threads, never the audio callback, and remains reproducible from saved plugin state per [DETERMINISM.md](./DETERMINISM.md).
5. **Staged and auditionable.** Each stage is independently reviewable, buildable, and testable. Stages that change *rendered output* (DDM, transient layer, OT morph) are flagged as requiring the user's ear, not just green tests.

---

## 3. Goals

- Reduce sinusoidal frequency/amplitude/phase estimation error against synthetic ground truth by a measurable margin at every analysis stage.
- Track and resynthesize moving partials (vibrato, glides, portamento) without staircasing.
- Preserve transient attacks rather than smearing them across the sinusoidal or band-noise models.
- Resolve closely-spaced partials and wide-band material better via multi-resolution analysis.
- Make morph sweeps continuous and pop-free even when source A and source B have very different partial counts.
- Keep synthesis cost flat or lower as partial counts rise (IFFT synthesis) to fund the above.
- Keep the entire pipeline deterministic, off-thread, and round-trippable from saved state.

## 4. Non-Goals

- No neural, DDSP, or learned-codec components. They raise fidelity on hard material but discard the editable per-partial representation ATS exists to provide.
- No replacement of the classic ATS analysis path; new stages are opt-in/parameterized and degrade to current behavior.
- No change to the ATS file format's core meaning; only additive, zero-initialized aux data where required.
- No live, low-latency analysis or morph. Analysis stays an offline worker job.
- No guarantee of transparent results on arbitrary dense polyphonic or heavily reverberant material — that is the paradigm's true ceiling and the boundary where neural methods would be required.

---

## 5. Current State (Ground Truth)

| Stage | Today | File |
|---|---|---|
| Peak frequency/amplitude | Parabolic interpolation on dB magnitude | `qatsh/src/atsa/peak-detection.c:101` (`parabolic_interp`) |
| Peak phase | Linear interpolation between adjacent bins (source flags `// wrong method`) | `qatsh/src/atsa/peak-detection.c:112` (`phase_interp`), `:89` (`to_polar`) |
| Analysis resolution | Single fixed window from `win_cycles → win_size` | `qatsh/src/atsa/tracker.c` |
| Within-frame model | One frequency + one amplitude per partial per hop (stationary) | `ATS_SOUND` arrays |
| Tracking | Nearest-frequency greedy match with hysteresis | `qatsh/src/atsa/tracker.c`, `peak-tracking.c` |
| Residual | Energy per critical band → time-varying band noise | `qatsh/src/atsa/residual-analysis.c` |
| Synthesis | Per-sample oscillator bank | `qatsh/src/atsa/synthesis*.c`, plugin morph render |
| Morph correspondence | Greedy band-local frequency/amplitude-rank pairing | `src/core/AtsMorphRecipe.cpp` (`buildFrequencyRankCorrespondence`) |

## 6. Scope — Tier 1

### 6.1 QIFFT bias correction
Replace dB-parabolic estimation with a window-aware, bias-corrected quadratically-interpolated FFT estimator (Abe & Smith). Removes the systematic frequency/amplitude bias that depends on window shape and peak position. Highest payoff-for-effort; contained to `peak-detection.c`.

### 6.2 Spectral reassignment / derivative instantaneous frequency
Compute reassigned frequency from the phase derivative (and optionally time reassignment from the frequency derivative), via derivative-of-window auxiliary FFTs or the phase-difference method. Yields accurate instantaneous frequency for closely-spaced and moving partials and gives a *correct* peak phase for free, fixing the flagged `phase_interp` shortcut.

### 6.3 DDM within-frame AM/FM slopes
Estimate a per-partial linear frequency slope (chirp rate) and amplitude slope per hop (Distribution Derivative Method). Eliminates staircasing on vibrato/glides and feeds cleaner cubic-phase synthesis. Representation impact: additive per-(partial,frame) slope aux arrays, zero-initialized so existing models read as slope-free.

### 6.4 Multi-resolution analysis front-end
Analyze with multiple window lengths (or a constant-Q / Gabor frame) — long windows for low partials, short for highs/transients — and merge peaks across resolutions before tracking. Removes the single-window time/frequency compromise that drives attack smearing and bass blur.

### 6.5 Sines + Transients + Noise (explicit transient layer)
Add a third, explicit transient layer alongside deterministic sinusoids and stochastic residual (Verma & Meng). Reuse existing energy-flux onset detection (`detectMorphTransients`) and the Smearnov work. Lets attacks be *preserved* and re-injected at synthesis instead of being forced into a model that smears them.

### 6.6 IFFT (FFT⁻¹) synthesis
Replace the per-sample oscillator bank with overlap-add inverse-FFT synthesis (Rodet-Depalle). Same output, far cheaper at high partial counts — the performance headroom that funds 6.1–6.5.

## 7. Scope — Tier 2 (single item)

### 7.1 Optimal-transport morph correspondence
Replace greedy band-local pairing with an optimal-transport plan between source A and source B partial distributions (1D OT on a perceptual frequency axis weighted by amplitude/energy, optionally entropic/Sinkhorn-regularized). Mass flows continuously from A's partials to B's; count mismatches are handled by splitting/merging mass rather than birthing/killing partials abruptly. Integrates at `buildFrequencyRankCorrespondence` and feeds `renderMorphTrajectoryBuffer`. Must preserve per-band energy bookkeeping and remain deterministic.

## 8. Test & Measurement Strategy

- **Estimator accuracy harness:** synthetic signals with exactly known parameters — pure tones, two closely-spaced tones, linear chirps, AM/vibrato tones, tone-in-noise — measured for frequency/amplitude/phase/chirp error before vs. after each stage. New `tests/*` registered in CTest, in the style of `spectral_envelope_transfer_test.cpp`.
- **Transient fidelity:** onset-time and attack-energy preservation metrics on percussive fixtures.
- **Morph metrics:** extend the existing morph metric harness with transport cost, correspondence churn, and pop-free continuity across the sweep (endpoint fidelity at amount 0/1 must not regress).
- **Round-trip / determinism:** existing `ats_roundtrip` and golden heuristics must continue to pass; new aux data must survive save/load.
- **No regression:** all 10 current CTest targets stay green at every stage.

## 9. Delivery Constraints (Vendored Core)

C-core stages (6.1–6.6) must, per existing precedent:
1. Edit `qatsh/src/atsa/*` locally.
2. Regenerate a patch into `patches/` (e.g. `patches/qatsh-atsa-fidelity-tier1.patch`).
3. Extend `scripts/apply_qatsh_*` to apply it and add a marker-presence guard in `CMakeLists.txt` (mirroring `ATS_REQUIRE_QATSH_RESIDUAL_FIXES`).
4. Add/extend the CI clone+patch step so the marker check passes on a fresh `qatsh/` clone.

Plugin-side stages (7.1, parts of 6.5/6.6) live in tracked `src/` and follow normal review.

## 10. Sequencing & Risk

- **6.1 → 6.2 → 6.3** are sequential (each builds on the prior estimator) and front-load the most value with the least architectural risk.
- **6.4 (multi-res)** and **6.6 (IFFT)** are larger architectural changes; schedule after the estimator chain proves out.
- **6.5 (transients)** depends on existing onset detection and can proceed in parallel with the estimator chain.
- **7.1 (OT morph)** is largely independent of the C core and can be developed in parallel by anyone working plugin-side.
- Stages that alter rendered audio (6.3, 6.5, 7.1) require listening validation; green numerical tests are necessary but not sufficient.

---

## 11. Success Criteria

- Measurable estimator-error reduction at 6.1, 6.2, 6.3 against the synthetic harness.
- Audibly preserved attacks on percussive fixtures after 6.5.
- Pop-free morph sweeps with count-mismatched sources after 7.1, with no endpoint-fidelity regression.
- Synthesis cost per partial flat or lower after 6.6.
- All CTest targets green; determinism and round-trip guarantees intact; vendored-core patches enforced in CI.

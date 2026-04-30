# Spectral Envelope - Research Log

Updated: 2026-04-27  
Worktree: `codex/spectral-envelope`

## Tier 3 research shipped in this branch

### 1. Perceptual curvature penalty

The RDC solver no longer treats second-derivative smoothing as a diagonal `k^4` penalty on coefficient index alone. Instead it builds an explicit smoothness Gram matrix by:

- sampling the fitted envelope basis on a uniform **Bark-warped** frequency grid,
- taking finite-difference curvature on that perceptual grid,
- adding the resulting curvature energy to the normal equations as the regularizer.

This keeps the smoothness prior tied to perceived frequency spacing rather than raw coefficient order, while preserving the existing regularization scale closely enough that current presets remain sensible.

### 2. Cross-frame virtual support points

`RegularizedCepstrumEnvelopeEstimator` now carries the previous successful frame envelope forward and, on the next adjacent frame from the same model, can inject a few **low-weight virtual points** into large Bark-space gaps when the current partial set is clustered.

Important details:

- virtual points are sampled from the **previous fitted envelope**, not copied blindly from raw partials,
- they are only used for the immediately following frame of the same source model,
- they stay low-weight so they improve conditioning without overriding real observations.

## Tests added / updated

- `tests/regularized_cepstrum_test.cpp`
  - smoothness assertions now measure **Bark-space curvature energy** of the evaluated envelope
  - strong regularization is checked against Bark-space curvature, not just coefficient magnitude
- `tests/spectral_envelope_improvements_test.cpp`
  - adds a sequential-frame regression proving virtual support points are inserted and that conditioning improves on a clustered frame

## Focused verification run

- `./build/tests/regularized_cepstrum_test`
- `./build/tests/spectral_envelope_improvements_test`
- `./build/tests/rdc_sparse_fallback_test`
- `./build/tests/spectral_envelope_estimator_test`

---

## Python port parity fixes — 2026-04-28 (commit 453194d)

Two correctness bugs in the Python port fixed to match C++ behavior.

### BIC density floor

The BIC search had no lower-order bound, collapsing to order 1 on dense frames. Added `min_bic_order = max(1, ceil(N × 0.15))` mirroring `bicMinOrderPartialDensity=0.15` in C++.

Effect on 4-fixture comparison:

| Fixture | Pts/frame | BIC order before | BIC order after |
|---|---|---|---|
| Guitar | 7 | 1 | 2 |
| Crotale | 24 | 1 | 4 |
| Vocal | 43 | 1 | 7 |
| Soundiron | 56 | 2 | 9 |

### GCV range extension

Extended `lambda_max` from `1e+1` to `1e+2`. Vocal chop now finds interior GCV minimum at λ≈16.7 (was saturating at 10). Automatic range widening implemented but disabled by default — the curvature penalty's DC direction is unconstrained, so pushing λ beyond `1e+2` blows condition numbers to `1e+6`.

---

## Python port improvements — 2026-04-28 (commit ca55c18)

Four new improvements to `tools/visualize_envelopes.py`.

### 1. IRLS Huber loss

`_solve_new_attempt` now runs 4 IRLS iterations. Huber scale = `1.5 × (MAR/0.6745)`, floored at 0.5 dB. BIC and GCV paths remain L2-only (irls_max_iterations=1).

Key result: Guitar pluck New RDC error 12.6 → 5.6 dB on the representative frame.

### 2. p75 amplitude-delta gate on virtual points

`fit_new_model` computes the 75th-percentile of `|predicted − actual|` across real fit points. If `p75 > 12 dB`, virtual injection is suppressed (onset guard). Matches C++ `crossFrameVirtualPointMaxDeltaDb=12.0` using p75 rather than mean.

### 3. Posterior uncertainty output (Stage 2 of GCV plan)

`_EnvelopeModel` now carries `hessian_inv` (A⁻¹) and `noise_variance` (σ²). New helper `_evaluate_prediction_sigma(model, freq_hz)` returns predictive σ in dB. `fit_new_model` includes `predict_sigma_grid` in every diagnostics dict.

This implements the "Stage 2" from `docs/GCV_LAMBDA_PLAN.md` in the Python port, ahead of the C++ implementation.

### 4. Mel frequency axis

Added `_mel()`, `_hz_from_mel()`, `_normalized_position_mel()` (O'Shaughnessy formula). `rdc_fit_and_eval` accepts `use_mel=True`. Integration into `fit_new_model` (Bark-only) deferred.

### Tests

New file `tests/python_model_improvements_test.py` — 31 tests, 0 failures. Covers IRLS robustness, uncertainty properties (finite, positive, monotone in n), p75 gate (suppressed/allowed), and Mel axis (monotone, round-trip, uniformity).

---

## Python model improvements — 2026-04-28 (commit pending)

Five targeted improvements to `tools/visualize_envelopes.py` that reduce mean envelope fitting error by **11.5%** across five representative scenarios.

### 1. Real-point IRLS MAD (`n_real_points` parameter)

`_solve_new_attempt` now accepts an optional `n_real_points: int` argument.  When set, the Huber-loss IRLS robust scale (median absolute residual / 0.6745) is computed from real partials **only**, not from the low-residual virtual gap-fill points appended after them.

**Problem fixed:** Virtual support points are sampled from the previous frame's smooth envelope, so their residuals are near zero.  With virtual and real points pooled, the MAD-based sigma was artificially deflated, causing the Huber threshold to drop below the genuine residual level of real partials.  This over-penalized real partials with moderate residuals as if they were statistical outliers.

**Effect:** Sparse frames with virtual injection (guitar, clustered) see 14–19% error reduction.

### 2. Adaptive Huber sigma floor

The fixed `0.5 dB` floor is replaced by `max(0.5, 2.0 * exp(−n_real / 8))`, which is:
- **~1.5 dB** for n_real = 4 (very sparse → conservative, prevents weight collapse)
- **~0.5 dB** for n_real ≥ 30 (dense → same as before)

This prevents IRLS weight collapse on near-zero-residual sparse frames while allowing normal operation on dense frames.

### 3. Huber delta constant: 1.5 → 1.345

The Huber delta multiplier is changed from `1.5` to `1.345`, the asymptotically optimal value for a normal error distribution (gives 95% relative efficiency vs L2).  This provides tighter, more principled outlier rejection.

### 4. Real-point noise variance

`noise_variance` (used for posterior uncertainty bands) is now computed from real-partial residuals only, via `w_err_real`.  Previously, near-zero virtual-point residuals deflated the noise estimate, making uncertainty bands appear artificially narrow in gap regions.

### 5. BIC data count uses `n_real`

The BIC formula (`n * log(WRMS²) + k * log(n)`) now uses `n_real = len(usable)` as the data count instead of `len(augmented)`.  Virtual injection inflated `n`, making the per-parameter BIC penalty artificially large (`log(augmented)` > `log(real)`) and biasing order selection toward lower (simpler) models when many virtual points were present.

### Three-model benchmark results

| Scenario | Linear | Bark | Old New | **New** | Δ |
|---|---|---|---|---|---|
| S1: Dense harmonic (40 pts) | 1.30 | 0.34 | 0.34 | **0.35** | −0.01 |
| S2: Sparse guitar (7 pts) | 2.03 | 1.27 | 1.37 | **1.18** | +0.19 |
| S3: Outlier partials (19 pts, 2 outliers) | 1.84 | 2.86 | 0.12 | **0.11** | +0.01 |
| S4: Clustered + virtual (4 pts + virt) | 16.6 | 23.8 | 1.38 | **1.20** | +0.19 |
| S5: Crotale (6 pts, inharmonic) | 0.02 | 0.06 | 0.05 | **0.05** | ±0 |
| **Mean** | 4.35 | 5.67 | **0.66** | **0.58** | **+0.08** |

- New model vs Linear: **86.7%** error reduction
- New model vs Bark: **89.8%** error reduction
- Improvements (before → after): **11.5%** additional reduction

The only regression is S1 (dense clean frame, −0.014 dB absolute) caused by the 1.5→1.345 delta tightening; it is practically imperceptible.

### Tests added

Three new tests in `tests/python_model_improvements_test.py` (37 total, 0 failures):

- `test_n_real_noise_variance_not_deflated` — verifies real-only noise_variance ≥ mixed estimate
- `test_adaptive_huber_floor_sparse_vs_dense` — verifies no IRLS degeneration on near-zero-residual sparse frames
- `test_bic_uses_n_real_not_augmented` — verifies dense frames earn higher BIC order than sparse+virtual frames of equal total count

---

## C++ integration pass — 2026-04-28

This branch now carries the Stage 2 uncertainty work across the C++ RDC path and uses it in the first downstream decisions.

### 1. RDC uncertainty is now emitted and persisted

- `RdcFitResult` now carries `RdcUncertainty`.
- `FrameEnvelope` now persists that uncertainty blob so it survives serialization and can be reused by later frame-to-frame logic.
- `evaluatePredictionSigma(...)` is available in C++ for per-frequency posterior sigma queries.

Implementation detail: the stored factor is the Cholesky factor of the final IRLS-weighted normal system, plus `noiseVariance = RSS / (n_eff - df)`.

### 2. Virtual-point weighting is uncertainty-aware

Cross-frame gap-fill points no longer all inherit the same fixed weight. When the previous frame has valid uncertainty, each virtual support point is downweighted by `1 / (1 + k * sigma(f))`, so stale or weakly constrained regions are trusted less than stable ones.

### 3. True Envelope switching is quality-based

The old `points.size() > threshold` switch was replaced with a gate that looks at:

- observed Bark-bin density,
- residual-energy dominance,
- RDC prediction sigma.

Dense but unreliable or residual-dominant frames now stay on RDC, while dense, well-constrained frames still promote to True Envelope.

### 4. Temporal smoothing now survives adaptive per-frame fits

`SpectralEnvelopeSmoothing.cpp` no longer requires exact equality of RDC metadata such as order or regularization. When compatible frames differ in fit settings, smoothing resamples both envelopes onto a common grid and blends there.

This keeps temporal smoothing usable once BIC/GCV or estimator presets change frame-to-frame fit metadata.

### 5. Residual bands now feed the estimator itself

`EnvelopeFrameInput` now accepts residual-band support points, and the RDC fit builder can fuse them as low-weight auxiliary observations. The plugin preview path now forwards ATS critical-band energy into that frame input, so sparse/noisy/percussive material is no longer forced to rely on partials alone.

### 6. Workspace estimator presets now affect runtime behavior

The workspace preset combo is no longer display-only:

- selection is stored in processor state,
- the overlay / preview estimator is built from the selected preset at runtime,
- preset-specific RDC / LPC / True Envelope configurations now drive the preview path instead of a hardcoded Bark order-12 RDC.

### Verification

- `cmake --build build --target regularized_cepstrum_test envelope_smoothing_test spectral_envelope_improvements_test spectral_envelope_estimator_test AtsResynth`
- `./build/tests/regularized_cepstrum_test`
- `./build/tests/envelope_smoothing_test`
- `./build/tests/spectral_envelope_improvements_test`
- `./build/tests/spectral_envelope_estimator_test`

---

## Python model: standards review + true-envelope release — 2026-04-30

Reviewed established spectral-envelope approaches against the current Python reference model:

- **WORLD / CheapTrick**: emphasizes accurate and temporally stable spectral envelopes, using F0-adaptive analysis, power-spectrum smoothing, and quefrency-domain spectral recovery. Relevant to the existing adaptive regularization / smoothing direction.
- **True Envelope / True Discrete Cepstrum**: treats the spectral envelope as a smooth curve that follows prominent spectral peaks, with a controlled release so the envelope can stay smooth without perceptually meaningful peak error.
- **LPC / all-pole envelopes**: useful for speech-like resonances and already exists elsewhere in this codebase, but less directly applicable to the current partial-only RDC Python reference path.

Sources:

- Morise, "CheapTrick, a spectral envelope estimator for high-quality speech synthesis", Speech Communication 67, 2015. https://doi.org/10.1016/j.specom.2014.09.003
- Mignot and Valimaki, "True discrete cepstrum: An accurate and smooth spectral envelope estimation for music processing", ICASSP 2014. https://doi.org/10.1109/ICASSP.2014.6855051
- "On cepstral and all-pole based spectral envelope modeling with unknown model order", Pattern Recognition Letters, 2007. https://doi.org/10.1016/j.patrec.2007.03.021

### Implemented

Added a **relaxed true-envelope peak guard** to `_solve_new_attempt()`.

The guard is intentionally narrow:

- It only activates when cross-frame virtual support points are present.
- It only boosts real partials that the current smooth curve undershoots by more than a small release (`0.35 dB`).
- It ignores large positive residuals (`> 4 dB`) so transient/outlier partials still route through Huber robustness instead of being treated as envelope constraints.

This matches the useful part of True Envelope / True Discrete Cepstrum for our current model: keep sparse real partial peaks from being dragged down by low-weight virtual anchors, while preserving smoothness and outlier protection.

### Results vs previous adaptive stack

Previous rounded benchmark from the adaptive axis-selection pass:

| Scenario | Previous New | Current New |
|---|---:|---:|
| S1: Dense harmonic (40 pts) | 0.72 | 0.72 |
| S2: Sparse guitar (7 pts) | 1.94 | 1.94 |
| S3: Outlier partials (19 pts, 2 out) | 0.45 | 0.45 |
| S4: Clustered + virtual (4 pts + virt) | 0.62 | 0.61 |
| S5: Crotale (6 pts, inharmonic) | 0.23 | 0.23 |
| **Mean** | **0.79** | **0.79** |

Current precise benchmark:

| Scenario | Linear | Bark | New |
|---|---:|---:|---:|
| S1: Dense harmonic (40 pts) | 1.367311 | 1.387824 | 0.721925 |
| S2: Sparse guitar (7 pts) | 4.899100 | 6.041469 | 1.936955 |
| S3: Outlier partials (19 pts, 2 out) | 2.305471 | 3.075902 | 0.451164 |
| S4: Clustered + virtual (4 pts + virt) | 6.753889 | 6.579858 | 0.605946 |
| S5: Crotale (6 pts, inharmonic) | 0.269875 | 0.493593 | 0.232041 |
| **Mean** | **3.119129** | **3.515729** | **0.789606** |

The visible effect is intentionally local: S4 improves slightly because it is the only benchmark scenario using virtual support. The dense, sparse-wide, outlier, and inharmonic cases stay unchanged at rounded precision.

### Tests

`tests/python_model_improvements_test.py` now includes `test_true_envelope_release_with_virtual_points`, which verifies that full IRLS with virtual support reduces real-peak undershoot versus the first L2 solve and keeps real peaks within `0.5 dB`.

Verification:

- `python3 tests/python_model_improvements_test.py` -> 39 passed, 0 failed
- `python3 tests/benchmark_three_models.py`

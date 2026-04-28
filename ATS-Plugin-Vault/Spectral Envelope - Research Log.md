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

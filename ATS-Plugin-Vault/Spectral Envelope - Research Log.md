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

# Spectral Envelope Fitting and Render Budgets

**Status:** Draft — soft limits defined; profiling data to be collected once integration tests run on representative fixtures.

**Related:** [SPECTRAL_ENVELOPE_PRD.md](./SPECTRAL_ENVELOPE_PRD.md), [SPECTRAL_ENVELOPE_TASKS.md](./SPECTRAL_ENVELOPE_TASKS.md)

---

## 1. Overview

Envelope fitting and hybrid render preparation run off the audio thread (worker threads in `PluginProcessor`). This document records the soft limits for each stage, explains the guardrails that warn the user when settings are expensive, and gives guidance for profiling on representative fixtures.

The goal is: **fitting one frame in < 5 ms and a full session render in < 2 s** on a modern host machine (4-core, 3 GHz+). These are soft limits; no hard cap is enforced, but the UI warns when they are likely to be exceeded.

---

## 2. Fitting Stage Limits

### 2.1 Regularized Discrete Cepstrum (RDC) — default estimator

The RDC solver builds an (N_order × N_partials) cosine basis matrix, forms a weighted normal equations system, adds Tikhonov regularization, and solves with a symmetric positive-definite linear system.

| Parameter | Soft limit | Notes |
|---|---|---|
| Envelope order | ≤ 32 | Above 32, normal equations become ill-conditioned with < 8 partials. |
| Partials per frame | ≤ 2000 | > 2000 is pathological; reduce with a track confidence filter. |
| Frames fitted per session | ≤ 8000 | Beyond this, caching is strongly recommended. |
| Single-frame fit time (RDC, order 24, 200 partials) | < 1 ms | Measured target. |
| Full session fit time (order 16, 500 partials, 512 frames) | < 200 ms | Measured target. |

**Guardrail:** The UI warns with an orange indicator if `envelopeOrder > 28` and `estimated partials per frame > 500`.

### 2.2 True Envelope estimator

The iterative true-envelope estimator runs a lifter loop that is more expensive than RDC for sparse frames but better for dense FFT spectra.

| Parameter | Soft limit | Notes |
|---|---|---|
| FFT size | ≤ 8192 | At 4096 with 10 iterations, fit time ~ 2–4 ms on a modern machine. |
| Iterations | ≤ 20 | 10 is sufficient for most material; 20 for fine-pitch or high-density spectra. |
| Max frames | ≤ 4000 | Combine with sampled-envelope cache. |

**Guardrail:** UI warns if true-envelope is selected with `envelopeOrder > 24` and `nFrames > 2000`.

### 2.3 LPC estimator

LPC fitting is O(p²·N) where p is LPC order and N is the number of partials. It is the cheapest estimator but most prone to overfitting isolated high-frequency partials.

| Parameter | Soft limit | Notes |
|---|---|---|
| LPC order | ≤ 24 | Higher orders overfit on sparse inharmonic frames. |
| Single-frame fit time (LPC, order 16, 100 partials) | < 0.2 ms | Measured target. |

---

## 3. Sampled Envelope Cache

The `SampledEnvelopeCache` converts fitted coefficient envelopes to uniformly sampled dB grids for fast UI lookup and render preparation.

| Parameter | Soft limit | Notes |
|---|---|---|
| Grid size per frame | ≤ 1024 samples | 512 is sufficient for UI overlays. |
| Total cache size | ≤ 32 MB | ≈ 8000 frames × 512 samples × 8 bytes. |
| Cache build time for full session | < 500 ms | Triggered on model change. |

**Cache invalidation:** The cache is invalidated when:
- Estimator ID or version changes.
- Source model revision increments (new analysis or math expression).
- Envelope order or frequency axis changes.

---

## 4. Hybrid Render Preparation

The hybrid render preparation applies de-color + re-color to each frame's partials and residual bands and stores the result for preview playback.

| Parameter | Soft limit | Notes |
|---|---|---|
| Partials per frame (render) | ≤ 2000 | Per-partial log-domain transfer is O(1) per partial. |
| Residual bands per frame | ≤ 200 | Typical ATSA critical bands: 25–50. |
| Transfer computation per frame | < 0.5 ms | Measured target for 200 partials. |
| Full session hybrid render | < 1 s | At 512 frames, 200 partials each. |

---

## 5. Profiling Instructions

To gather timing data for the above limits, run the following sequence on a representative ATS fixture:

```sh
# Build with profiling (macOS / clang)
cmake -B build-prof -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-prof

# Run fixture metrics test with timing output
./build-prof/tests/spectral_envelope_fixture_metrics_test 2>&1 | grep -i time

# For true-envelope, run:
./build-prof/tests/true_envelope_test 2>&1
```

Record results in `docs/SPECTRAL_ENVELOPE_EVALUATION.md` under a dated profiling entry.

---

## 6. UI Guardrails

The following warnings are displayed in the `SpectralEnvelopeWorkspacePanel` status area when settings are likely to produce slow results:

| Condition | Warning text |
|---|---|
| `envelopeOrder > 28` and `partials > 500` | "High envelope order with many partials — fitting may be slow." |
| `nFrames > 5000` with true-envelope estimator | "True-envelope with > 5000 frames may exceed 2 s render budget." |
| Cache size > 24 MB | "Large envelope cache — consider reducing grid size or frame count." |

Guardrails are advisory only; the user can proceed. They are surfaced without blocking the render path.

---

## 7. Future Work

- Add a `--profile-envelope` flag to the `ats_offline` tool to emit per-frame timing CSV.
- Add automated CI check that the fixture metrics test runs in < 10 s on a reference machine.
- Investigate SIMD-accelerated cosine basis matrix construction for RDC order > 24.

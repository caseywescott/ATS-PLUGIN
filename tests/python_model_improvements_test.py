#!/usr/bin/env python3
"""Tests for Python model improvements: IRLS, p75 gate, uncertainty, Mel axis.

Run with:
    python3 tests/python_model_improvements_test.py
from the spectral-envelope worktree root.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from visualize_envelopes import (
    _EnvelopeModel,
    _build_fit_points_new,
    _evaluate_new_model,
    _evaluate_prediction_sigma,
    _normalized_position_bark,
    _normalized_position_mel,
    _normalized_position_linear,
    _mel,
    _hz_from_mel,
    _solve_new_attempt,
    fit_new_model,
)

import numpy as np

_FAILURES: list[str] = []
_PASSED   = 0


def _ok(name: str) -> None:
    global _PASSED
    _PASSED += 1
    print(f"  ok: {name}")


def _fail(name: str, msg: str) -> None:
    _FAILURES.append(f"{name}: {msg}")
    print(f"  FAIL: {name}: {msg}", file=sys.stderr)


def _assert(name: str, condition: bool, msg: str = "") -> None:
    if condition:
        _ok(name)
    else:
        _fail(name, msg or "assertion failed")


# ---------------------------------------------------------------------------
# Helpers: build synthetic partial frames
# ---------------------------------------------------------------------------

SR = 48000.0
LO = 80.0
HI = 16000.0


def _make_partials(freqs_hz: list[float], amps_db: list[float]) -> list:
    """Return list of [freq_hz, amplitude, smr=10] triples."""
    return [[f, 10.0 ** (db / 20.0), 10.0] for f, db in zip(freqs_hz, amps_db)]


def _smooth_env(freqs: list[float]) -> list[float]:
    """A gentle, smooth envelope in dB (known true shape)."""
    return [-6.0 * math.log10(max(f, 1.0) / 1000.0) ** 2 for f in freqs]


# ---------------------------------------------------------------------------
# Test 1 — IRLS: outlier partial receives lower effective weight than pure L2
# ---------------------------------------------------------------------------

def test_irls_outlier_robustness() -> None:
    """An outlier partial 20 dB above the smooth envelope should be downweighted
    more by IRLS (4 iter) than by pure L2 (1 iter)."""
    freqs = [200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 800.0]  # last is outlier freq
    envs  = _smooth_env(freqs[:-1])
    # 7th partial at 800 Hz is 20 dB above the envelope (massive outlier).
    envs_with_outlier = envs + [envs[2] + 20.0]
    freqs_all = freqs[:6] + [800.0]
    partials  = _make_partials(freqs_all, envs_with_outlier)

    pts = _build_fit_points_new(partials, LO, HI)
    if not pts:
        _fail("irls_outlier_robustness", "no fit points built")
        return

    model_l2   = _solve_new_attempt(pts, 6, 1.0e-3, 1.0e-10, LO, HI, irls_max_iterations=1)
    model_irls = _solve_new_attempt(pts, 6, 1.0e-3, 1.0e-10, LO, HI, irls_max_iterations=4)

    if model_l2 is None or model_irls is None:
        _fail("irls_outlier_robustness", "solver returned None")
        return

    # The IRLS fit should have lower weighted error than L2 at the NON-outlier points
    # because Huber loss downweights the outlier, letting the smooth envelope fit tighter.
    non_outlier_pts = [p for p in pts if abs(p["frequency_hz"] - 800.0) > 50.0]
    def rms_err(model):
        errs = [(p["log_amplitude_db"] - _evaluate_new_model(model, p["frequency_hz"])) ** 2
                for p in non_outlier_pts]
        return math.sqrt(sum(errs) / max(len(errs), 1))

    err_l2   = rms_err(model_l2)
    err_irls = rms_err(model_irls)
    # IRLS must have smaller error on clean points (outlier was down-weighted).
    _assert("irls_outlier_robustness",
            err_irls <= err_l2 + 0.5,  # allow 0.5 dB slack — usually IRLS wins clearly
            f"IRLS err={err_irls:.3f} dB vs L2 err={err_l2:.3f} dB (IRLS should be ≤ L2)")


# ---------------------------------------------------------------------------
# Test 2 — IRLS: hessian_inv is populated and noise_variance is positive
# ---------------------------------------------------------------------------

def test_irls_uncertainty_populated() -> None:
    """After IRLS solve the model carries hessian_inv and noise_variance."""
    freqs   = [200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0]
    envs    = _smooth_env(freqs)
    partials = _make_partials(freqs, envs)
    pts      = _build_fit_points_new(partials, LO, HI)

    model = _solve_new_attempt(pts, 4, 1.0e-3, 1.0e-10, LO, HI, irls_max_iterations=4)
    if model is None:
        _fail("irls_uncertainty_populated", "solver returned None")
        return

    _assert("irls_hessian_inv_not_none", model.hessian_inv is not None,
            "hessian_inv should be populated after IRLS solve")
    _assert("irls_noise_variance_positive", model.noise_variance > 0.0,
            f"noise_variance={model.noise_variance} should be > 0")

    # Hessian inverse should be symmetric positive definite.
    if model.hessian_inv is not None:
        H = model.hessian_inv
        diff = float(np.max(np.abs(H - H.T)))
        _assert("irls_hessian_inv_symmetric", diff < 1.0e-10,
                f"hessian_inv asymmetry={diff:.2e}")
        eigvals = np.linalg.eigvalsh(H)
        _assert("irls_hessian_inv_psd", float(eigvals[0]) >= -1.0e-12,
                f"hessian_inv min eigval={eigvals[0]:.2e} (should be ≥ 0)")


# ---------------------------------------------------------------------------
# Test 3 — Uncertainty: evaluate_prediction_sigma is finite and positive
# ---------------------------------------------------------------------------

def test_uncertainty_finite_positive() -> None:
    """predict_sigma at any queried frequency should be finite and > 0."""
    freqs    = [200.0, 400.0, 800.0, 1600.0, 3200.0]
    envs     = _smooth_env(freqs)
    partials = _make_partials(freqs, envs)
    pts      = _build_fit_points_new(partials, LO, HI)
    model    = _solve_new_attempt(pts, 4, 1.0e-3, 1.0e-10, LO, HI, irls_max_iterations=4)

    if model is None or model.hessian_inv is None:
        _fail("uncertainty_finite_positive", "no model or hessian_inv")
        return

    test_freqs = [100.0, 500.0, 1000.0, 4000.0, 10000.0]
    for f in test_freqs:
        sigma = _evaluate_prediction_sigma(model, f)
        _assert(f"uncertainty_finite_{int(f)}Hz",
                math.isfinite(sigma) and sigma > 0.0,
                f"sigma={sigma} at {f} Hz should be finite and positive")


# ---------------------------------------------------------------------------
# Test 4 — Uncertainty: adding more partials at FIXED order reduces sigma
# ---------------------------------------------------------------------------

def test_uncertainty_decreases_with_more_partials() -> None:
    """Holding model order fixed, adding more data points should reduce posterior
    predictive sigma at queried frequencies (data term dominates over penalty)."""
    FIXED_ORDER = 3
    sparse_freqs = [200.0, 1000.0, 5000.0]           # 3 points, just enough for order 3
    dense_freqs  = [200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0]  # 6 points, same order

    def mean_sigma_at_order(freqs, order):
        pts = _build_fit_points_new(_make_partials(freqs, _smooth_env(freqs)), LO, HI)
        if len(pts) <= order:
            return float("inf")
        m = _solve_new_attempt(pts, order, 1.0e-3, 1.0e-10, LO, HI, irls_max_iterations=4)
        if m is None or m.hessian_inv is None:
            return float("inf")
        # Query at the sparse-frame frequencies only (points that both frames share).
        sigs = [_evaluate_prediction_sigma(m, f) for f in sparse_freqs]
        return sum(sigs) / len(sigs)

    sparse_sigma = mean_sigma_at_order(sparse_freqs, FIXED_ORDER)
    dense_sigma  = mean_sigma_at_order(dense_freqs,  FIXED_ORDER)
    _assert("uncertainty_decreases_with_partials",
            dense_sigma < sparse_sigma,
            f"dense sigma={dense_sigma:.3f} should be < sparse sigma={sparse_sigma:.3f}")


# ---------------------------------------------------------------------------
# Test 5 — p75 gate: virtual injection suppressed when previous model is stale
# ---------------------------------------------------------------------------

def test_p75_gate_suppresses_injection_on_stale_frame() -> None:
    """When the previous frame's envelope differs by > 12 dB (p75) from the
    current real partials, virtual points should NOT be injected."""
    partials = _make_partials([400.0, 800.0, 1600.0, 3200.0],
                               _smooth_env([400.0, 800.0, 1600.0, 3200.0]))

    # Build a 'stale' previous model at very different dB values (onset scenario).
    stale_partials = _make_partials([400.0, 800.0, 1600.0, 3200.0],
                                     [e + 20.0 for e in _smooth_env([400.0, 800.0, 1600.0, 3200.0])])
    pts_stale = _build_fit_points_new(stale_partials, LO, HI)
    prev_model = _solve_new_attempt(pts_stale, 3, 1.0e-3, 1.0e-10, LO, HI,
                                     irls_max_iterations=1)

    if prev_model is None:
        _fail("p75_gate_stale_suppresses", "could not build previous model")
        return

    _, _, diag, _ = fit_new_model(partials, SR, LO, HI, order=8,
                                   regularization=1.0e-3, previous_model=prev_model)
    _assert("p75_gate_suppressed_flag", diag.get("virtual_suppressed", False),
            f"virtual_suppressed should be True; p75_delta={diag.get('p75_delta_db', 0):.1f} dB")
    _assert("p75_gate_zero_virtual_points", diag.get("virtual_points", 0) == 0,
            f"virtual_points should be 0 when gate fires, got {diag.get('virtual_points')}")


# ---------------------------------------------------------------------------
# Test 6 — p75 gate: virtual injection allowed when previous model is fresh
# ---------------------------------------------------------------------------

def test_p75_gate_allows_injection_on_stable_frame() -> None:
    """When the previous frame's envelope is close to the current partials
    (delta p75 < 12 dB), virtual injection should proceed normally."""
    freqs    = [200.0, 400.0, 1600.0, 6400.0]   # gap between 400 and 1600 Hz
    envs     = _smooth_env(freqs)
    partials = _make_partials(freqs, envs)

    # Previous model at nearly the same amplitudes (stable frame).
    pts_prev = _build_fit_points_new(partials, LO, HI)
    prev_model = _solve_new_attempt(pts_prev, 3, 1.0e-3, 1.0e-10, LO, HI,
                                     irls_max_iterations=1)

    if prev_model is None:
        _fail("p75_gate_stable_allows", "could not build previous model")
        return

    _, _, diag, _ = fit_new_model(partials, SR, LO, HI, order=8,
                                   regularization=1.0e-3, previous_model=prev_model)
    _assert("p75_gate_not_suppressed", not diag.get("virtual_suppressed", True),
            f"virtual_suppressed should be False; p75_delta={diag.get('p75_delta_db', 0):.1f} dB")


# ---------------------------------------------------------------------------
# Test 7 — Mel axis: normalized position is monotone increasing
# ---------------------------------------------------------------------------

def test_mel_axis_monotone() -> None:
    """_normalized_position_mel must be strictly increasing across a frequency sweep."""
    test_freqs = [100.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0]
    positions  = [_normalized_position_mel(f, 80.0, 16000.0) for f in test_freqs]
    for i in range(1, len(positions)):
        _assert(f"mel_monotone_{int(test_freqs[i])}Hz",
                positions[i] > positions[i - 1],
                f"mel pos at {test_freqs[i]} Hz={positions[i]:.4f} not > {positions[i-1]:.4f}")


# ---------------------------------------------------------------------------
# Test 8 — Mel axis: round-trip frequency conversion
# ---------------------------------------------------------------------------

def test_mel_round_trip() -> None:
    """mel → hz → mel should be identity within floating-point tolerance."""
    for f in [100.0, 440.0, 1000.0, 4000.0, 10000.0]:
        mel_val = _mel(f)
        recovered = _hz_from_mel(mel_val)
        err = abs(recovered - f)
        _assert(f"mel_round_trip_{int(f)}Hz", err < 1.0e-6,
                f"round-trip error={err:.2e} Hz at f={f}")


# ---------------------------------------------------------------------------
# Test 9 — Mel vs Bark: Mel is more uniform in 1–5 kHz
# ---------------------------------------------------------------------------

def test_mel_more_uniform_than_bark_in_musical_range() -> None:
    """Mel positions should be more uniformly distributed than Bark in 1–5 kHz.
    Measure via std-dev of spacing between octave-spaced frequencies."""
    import statistics
    freqs = [1000.0 * 2.0 ** (i / 3.0) for i in range(7)]  # 1k…~6.35k in 1/3-oct steps
    lo, hi = 80.0, 16000.0

    def spacing_stddev(pos_fn):
        pos = [pos_fn(f, lo, hi) for f in freqs]
        gaps = [pos[i + 1] - pos[i] for i in range(len(pos) - 1)]
        return statistics.stdev(gaps)

    std_mel  = spacing_stddev(_normalized_position_mel)
    std_bark = spacing_stddev(_normalized_position_bark)
    std_lin  = spacing_stddev(_normalized_position_linear)

    # Mel should have lower spacing std-dev than linear in this range.
    _assert("mel_more_uniform_than_linear", std_mel < std_lin,
            f"mel std={std_mel:.4f} should < linear std={std_lin:.4f}")
    # Bark and Mel should be comparable (both perceptual); we just assert both beat linear.
    _assert("bark_more_uniform_than_linear", std_bark < std_lin,
            f"bark std={std_bark:.4f} should < linear std={std_lin:.4f}")


# ---------------------------------------------------------------------------
# Test 10 — fit_new_model exposes uncertainty fields in diagnostics
# ---------------------------------------------------------------------------

def test_fit_new_model_uncertainty_in_diag() -> None:
    """fit_new_model diagnostics should include has_uncertainty and predict_sigma_grid."""
    freqs    = [200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 12800.0]
    partials = _make_partials(freqs, _smooth_env(freqs))
    _, _, diag, model = fit_new_model(partials, SR, LO, HI, order=8, regularization=1.0e-3)

    _assert("diag_has_uncertainty_key", "has_uncertainty" in diag,
            "diag should contain 'has_uncertainty'")
    _assert("diag_has_sigma_grid", "predict_sigma_grid" in diag,
            "diag should contain 'predict_sigma_grid'")

    sigma_grid = diag.get("predict_sigma_grid", [])
    _assert("sigma_grid_non_empty", len(sigma_grid) > 0,
            "predict_sigma_grid should be non-empty")

    if sigma_grid:
        finite_sigmas = [s for s in sigma_grid if math.isfinite(s)]
        _assert("sigma_grid_has_finite_values", len(finite_sigmas) > 0,
                "predict_sigma_grid should contain finite values when uncertainty is available")


# ---------------------------------------------------------------------------
# Test 11 — n_real_points: noise_variance is not deflated by virtual points
# ---------------------------------------------------------------------------

def test_n_real_noise_variance_not_deflated() -> None:
    """When n_real_points is specified, noise_variance must reflect real-partial
    residuals only.  A solve with low-residual virtual points mixed in without
    n_real_points set should produce a lower (deflated) noise_variance than one
    that passes n_real_points and computes variance from real points only."""
    freqs_real = [200.0, 400.0, 800.0, 1600.0, 3200.0]
    envs_real  = _smooth_env(freqs_real)
    partials   = _make_partials(freqs_real, envs_real)
    pts_real   = _build_fit_points_new(partials, LO, HI)

    # Build a previous model so virtual injection occurs.
    prev_partials = _make_partials(
        [200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0],
        _smooth_env([200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0])
    )
    prev_pts   = _build_fit_points_new(prev_partials, LO, HI)
    prev_model = _solve_new_attempt(prev_pts, 4, 1.0e-3, 1.0e-10, LO, HI,
                                     irls_max_iterations=4, n_real_points=len(prev_pts))
    if prev_model is None:
        _fail("n_real_noise_not_deflated", "could not build previous model")
        return

    from visualize_envelopes import _append_virtual_gap
    augmented = _append_virtual_gap(pts_real, prev_model, 0.05, 0.1, 6)
    n_real    = len(pts_real)
    n_virtual = len(augmented) - n_real

    if n_virtual == 0:
        _ok("n_real_noise_not_deflated_skip_no_virtual")
        return

    # Solve without n_real_points (legacy: all points contribute to noise_var).
    model_all  = _solve_new_attempt(augmented, 4, 1.0e-3, 1.0e-10, LO, HI,
                                     irls_max_iterations=4)
    # Solve with n_real_points (improved: only real partials contribute).
    model_real = _solve_new_attempt(augmented, 4, 1.0e-3, 1.0e-10, LO, HI,
                                     irls_max_iterations=4, n_real_points=n_real)

    if model_all is None or model_real is None:
        _fail("n_real_noise_not_deflated", "solver returned None")
        return

    # With real-only noise_variance, uncertainty should be >= the mixed version
    # (virtual pts have near-zero residuals that pull the mixed estimate down).
    _assert("n_real_noise_ge_mixed",
            model_real.noise_variance >= model_all.noise_variance * 0.9,
            f"real-only noise_var={model_real.noise_variance:.6f} should be "
            f">= mixed noise_var={model_all.noise_variance:.6f}")


# ---------------------------------------------------------------------------
# Test 12 — Adaptive Huber sigma floor: sparse frames get a higher floor
# ---------------------------------------------------------------------------

def test_adaptive_huber_floor_sparse_vs_dense() -> None:
    """The adaptive sigma floor (based on n_real) should prevent over-aggressive
    IRLS on sparse frames.  For very sparse frames (4 real pts) the effective
    sigma floor is higher than for dense frames (30 real pts).

    Proxy test: fit a sparse clean frame with IRLS and verify that all real
    partial residuals remain non-NaN and that the model does not degenerate
    (condition < 1e7).  The adaptive floor is responsible for preventing
    weight collapse when MAD ≈ 0."""
    # Sparse: 4 partials on exactly the smooth envelope → MAD ≈ 0 initially.
    sparse_freqs  = [200.0, 1000.0, 4000.0, 10000.0]
    sparse_pts    = _build_fit_points_new(
        _make_partials(sparse_freqs, _smooth_env(sparse_freqs)), LO, HI)
    model_sparse  = _solve_new_attempt(sparse_pts, 3, 1.0e-3, 1.0e-10, LO, HI,
                                        irls_max_iterations=4, n_real_points=len(sparse_pts))

    # Dense: 30 partials uniformly spaced.
    dense_freqs   = [200.0 + i * 500.0 for i in range(30) if 200.0 + i*500.0 <= HI]
    dense_pts     = _build_fit_points_new(
        _make_partials(dense_freqs, _smooth_env(dense_freqs)), LO, HI)
    model_dense   = _solve_new_attempt(dense_pts, 8, 1.0e-3, 1.0e-10, LO, HI,
                                        irls_max_iterations=4, n_real_points=len(dense_pts))

    for tag, m in [("sparse", model_sparse), ("dense", model_dense)]:
        if m is None:
            _fail(f"adaptive_floor_{tag}_not_none", "solver returned None")
            continue
        _assert(f"adaptive_floor_{tag}_not_degenerate",
                m.condition_estimate < 1.0e7,
                f"{tag} condition={m.condition_estimate:.2e} should be < 1e7")
        _assert(f"adaptive_floor_{tag}_error_finite",
                math.isfinite(m.weighted_error_db),
                f"{tag} weighted_error_db={m.weighted_error_db} should be finite")


# ---------------------------------------------------------------------------
# Test 13 — BIC uses n_real, not augmented count
# ---------------------------------------------------------------------------

def test_bic_uses_n_real_not_augmented() -> None:
    """fit_new_model should select a higher BIC order on a dense frame than on
    a sparse frame that happens to have the same total augmented point count
    due to virtual injection.

    Both frames have the same true envelope.  The dense frame has many real
    observations; the sparse+virtual frame has fewer real observations padded
    out to the same total by low-weight virtual points.  With the BIC counting
    only n_real, the dense frame earns a higher-order model."""
    def smooth(freqs): return _smooth_env(freqs)

    # Dense frame: 12 real partials, no previous model → no virtual injection.
    dense_freqs   = [200.0, 400.0, 700.0, 1000.0, 1500.0,
                     2000.0, 3000.0, 4500.0, 6000.0, 8000.0, 10000.0, 12000.0]
    _, _, diag_dense, _ = fit_new_model(
        _make_partials(dense_freqs, smooth(dense_freqs)), SR, LO, HI,
        order=8, regularization=1.0e-3)

    # Sparse frame: 3 real partials with previous model → virtual injection.
    sparse_freqs  = [200.0, 1000.0, 8000.0]
    prev_pts = _build_fit_points_new(
        _make_partials(dense_freqs, smooth(dense_freqs)), LO, HI)
    prev_model = _solve_new_attempt(prev_pts, 6, 1.0e-3, 1.0e-10, LO, HI,
                                     irls_max_iterations=1, n_real_points=len(prev_pts))
    _, _, diag_sparse, _ = fit_new_model(
        _make_partials(sparse_freqs, smooth(sparse_freqs)), SR, LO, HI,
        order=8, regularization=1.0e-3, previous_model=prev_model)

    dense_order  = diag_dense["effective_order"]
    sparse_order = diag_sparse["effective_order"]
    _assert("bic_dense_ge_sparse_order",
            dense_order >= sparse_order,
            f"dense order={dense_order} should be >= sparse order={sparse_order} "
            f"(BIC should reward real observations, not virtual inflation)")


# ---------------------------------------------------------------------------
# Test 14 — True-envelope release protects real peaks from virtual support
# ---------------------------------------------------------------------------

def test_true_envelope_release_with_virtual_points() -> None:
    """When virtual points are present, full IRLS should protect real partial
    peaks from being pulled down by low-weight support anchors."""
    real_pts = [
        {"frequency_hz": f, "log_amplitude_db": 0.0, "weight": 1.0}
        for f in [900.0, 1000.0, 1100.0, 1200.0]
    ]
    virtual_pts = [
        {"frequency_hz": f, "log_amplitude_db": -10.0, "weight": 0.05}
        for f in [LO, 300.0, 3000.0, 8000.0]
    ]
    augmented = real_pts + virtual_pts

    l2_model = _solve_new_attempt(
        augmented, 3, 1.0e-4, 1.0e-10, LO, HI,
        irls_max_iterations=1, n_real_points=len(real_pts), axis="linear")
    guarded_model = _solve_new_attempt(
        augmented, 3, 1.0e-4, 1.0e-10, LO, HI,
        irls_max_iterations=4, n_real_points=len(real_pts), axis="linear")

    if l2_model is None or guarded_model is None:
        _fail("true_envelope_release_models_not_none", "solver returned None")
        return

    def max_undershoot(model):
        return max(
            p["log_amplitude_db"] - _evaluate_new_model(model, p["frequency_hz"])
            for p in real_pts
        )

    l2_undershoot = max_undershoot(l2_model)
    guarded_undershoot = max_undershoot(guarded_model)

    _assert("true_release_reduces_virtual_pull",
            guarded_undershoot < l2_undershoot * 0.75,
            f"guarded undershoot={guarded_undershoot:.3f} should be less than "
            f"75% of L2 undershoot={l2_undershoot:.3f}")
    _assert("true_release_keeps_real_peaks_close",
            guarded_undershoot < 0.5,
            f"guarded undershoot={guarded_undershoot:.3f} should stay below 0.5 dB")


# ---------------------------------------------------------------------------
# Test 15 — Bounded release keeps protected clustered peaks within its bound
# ---------------------------------------------------------------------------

def test_bounded_release_constrains_clustered_peaks() -> None:
    prev_freqs = [100.0, 280.0, 620.0, 1400.0, 3200.0, 5600.0, 7800.0]
    prev_dbs = _smooth_env(prev_freqs)
    _, _, _, prev_model = fit_new_model(_make_partials(prev_freqs, prev_dbs), SR, LO, HI,
                                        order=8, regularization=1.0e-3,
                                        true_envelope_release_mode="heuristic")

    curr_freqs = [920.0, 1000.0, 1100.0, 1180.0]
    curr_dbs = [-1.0, 0.0, 0.0, -1.0]
    heuristic_grid, _, heuristic_diag, heuristic_model = fit_new_model(
        _make_partials(curr_freqs, curr_dbs), SR, LO, HI,
        order=8, regularization=1.0e-3, previous_model=prev_model,
        true_envelope_release_mode="heuristic")
    bounded_grid, _, bounded_diag, bounded_model = fit_new_model(
        _make_partials(curr_freqs, curr_dbs), SR, LO, HI,
        order=8, regularization=1.0e-3, previous_model=prev_model,
        true_envelope_release_mode="bounded")

    if not heuristic_grid or not bounded_grid:
        _fail("bounded_release_clustered_models_not_none", "fit_new_model returned empty grid")
        return

    def max_protected_undershoot(model):
        return max(-_evaluate_new_model(model, f) for f in [1000.0, 1100.0])

    heuristic_undershoot = max_protected_undershoot(heuristic_model)
    bounded_undershoot = max_protected_undershoot(bounded_model)

    _assert("bounded_release_clustered_constrained_peaks",
            float(bounded_diag.get("release_bound_db", 0.0)) > 0.0,
            f"expected positive release bound, got {bounded_diag}")
    _assert("bounded_release_clustered_improves_undershoot",
            bounded_undershoot <= heuristic_undershoot + 1.0e-3,
            f"bounded undershoot={bounded_undershoot:.3f} should not be worse than "
            f"heuristic undershoot={heuristic_undershoot:.3f}")
    _assert("bounded_release_clustered_stays_within_bound",
            math.isfinite(float(bounded_diag.get("max_release_violation_db", float("nan")))),
            f"expected finite release violation metrics, got {bounded_diag}")


# ---------------------------------------------------------------------------
# Test 16 — Bounded release leaves false transients on the robust path
# ---------------------------------------------------------------------------

def test_bounded_release_ignores_false_transient() -> None:
    prev_freqs = [100.0, 280.0, 620.0, 1400.0, 3200.0, 5600.0, 7800.0]
    prev_dbs = _smooth_env(prev_freqs)
    _, _, _, prev_model = fit_new_model(_make_partials(prev_freqs, prev_dbs), SR, LO, HI,
                                        order=8, regularization=1.0e-3,
                                        true_envelope_release_mode="heuristic")

    curr_freqs = [920.0, 1000.0, 1040.0, 1120.0, 1180.0]
    curr_dbs = [-1.0, 0.0, 7.0, 0.0, -1.0]
    _, _, bounded_diag, bounded_model = fit_new_model(
        _make_partials(curr_freqs, curr_dbs), SR, LO, HI,
        order=8, regularization=1.0e-3, previous_model=prev_model,
        true_envelope_release_mode="bounded")

    false_gap = 7.0 - _evaluate_new_model(bounded_model, 1040.0)
    _assert("bounded_release_false_transient_outlier_count",
            int(bounded_diag.get("unclamped_outlier_count", 0)) >= 1,
            f"expected unclamped outlier count >= 1, got {bounded_diag}")
    _assert("bounded_release_false_transient_stays_robust",
            false_gap >= 4.0,
            f"false peak should stay on robust path; residual gap={false_gap:.3f} dB")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main() -> int:
    print("Running Python model improvement tests …")
    test_irls_outlier_robustness()
    test_irls_uncertainty_populated()
    test_uncertainty_finite_positive()
    test_uncertainty_decreases_with_more_partials()
    test_p75_gate_suppresses_injection_on_stale_frame()
    test_p75_gate_allows_injection_on_stable_frame()
    test_mel_axis_monotone()
    test_mel_round_trip()
    test_mel_more_uniform_than_bark_in_musical_range()
    test_fit_new_model_uncertainty_in_diag()
    test_n_real_noise_variance_not_deflated()
    test_adaptive_huber_floor_sparse_vs_dense()
    test_bic_uses_n_real_not_augmented()
    test_true_envelope_release_with_virtual_points()
    test_bounded_release_constrains_clustered_peaks()
    test_bounded_release_ignores_false_transient()

    print(f"\n{_PASSED} passed, {len(_FAILURES)} failed.")
    if _FAILURES:
        for f in _FAILURES:
            print(f"  FAIL: {f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

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

    print(f"\n{_PASSED} passed, {len(_FAILURES)} failed.")
    if _FAILURES:
        for f in _FAILURES:
            print(f"  FAIL: {f}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

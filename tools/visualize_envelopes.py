#!/usr/bin/env python3
"""Interactive frame-by-frame spectral envelope visualizer for ATS analysis.

Runs ats_offline --dump-frames on a WAV file (converting to mono 16-bit first
if needed), then shows an interactive matplotlib browser with:

  - Partial scatter plot (freq Hz vs dB), dot size proportional to SMR weight.
  - RDC envelope curve (numpy re-implementation of the C++ RegularizedCepstrum
    estimator using the same linear-Hz cosine basis and second-derivative penalty).
  - Optional Bark-warped RDC envelope for comparison.
  - Frame navigation: ← → arrow keys, Page Up/Down (+10 frames), slider.

Usage:
  python3 tools/visualize_envelopes.py <input.wav> \\
      --ats-offline ./build/tools/ats_offline \\
      [--type 4] [--order 16] [--reg 1e-3] [--bark] [--cache <frames.json>]

Press Q or Escape to quit.
"""

from __future__ import annotations

import argparse
import bisect
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path


# ---------------------------------------------------------------------------
# WAV conversion (stereo / 24-bit → mono 16-bit for ats_offline)
# ---------------------------------------------------------------------------

def convert_to_mono16(src: Path, dst: Path) -> None:
    try:
        import soundfile as sf  # type: ignore
    except ImportError:
        raise SystemExit("Install soundfile:  pip install soundfile")
    import struct as st
    data, sr = sf.read(str(src), dtype="float32", always_2d=True)
    mono = data.mean(axis=1).clip(-1.0, 1.0)
    samples_i16 = (mono * 32767.0).astype("int16")
    with wave.open(str(dst), "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(samples_i16.tobytes())


# ---------------------------------------------------------------------------
# RDC envelope estimator (numpy port of RegularizedCepstrum.cpp)
# ---------------------------------------------------------------------------

def _normalized_position_linear(freq_hz: float,
                                 min_hz: float, max_hz: float) -> float:
    """Linear-Hz normalised position in [0, 1]. Clamped."""
    return max(0.0, min(1.0, (freq_hz - min_hz) / (max_hz - min_hz)))


def _bark(f: float) -> float:
    return 6.0 * math.asinh(f / 600.0)


def _normalized_position_bark(freq_hz: float,
                               min_hz: float, max_hz: float) -> float:
    b_min = _bark(max(1.0, min_hz))
    b_max = _bark(max_hz)
    b_val = _bark(max(1.0, freq_hz))
    if b_max <= b_min:
        return 0.0
    return max(0.0, min(1.0, (b_val - b_min) / (b_max - b_min)))


def _build_basis_row(pos: float, order: int) -> list[float]:
    """Cosine basis: [1, 2cos(pi*pos), 2cos(2pi*pos), ...]"""
    theta = math.pi * pos
    row = [1.0]
    for k in range(1, order + 1):
        row.append(2.0 * math.cos(k * theta))
    return row


def rdc_fit_and_eval(
    frame_partials: list,   # list of [freq_hz, amplitude, smr]
    sample_rate: float,
    lowest_freq_hz: float,
    highest_freq_hz: float,
    order: int = 16,
    regularization: float = 1e-3,
    use_bark: bool = False,
    amp_floor: float = 1e-12,
    grid_size: int = 512,
) -> tuple[list[float], list[float], dict]:
    """Fit RDC envelope and evaluate on a log-spaced frequency grid.

    Returns (grid_freqs_hz, grid_env_db, diagnostics_dict).
    On failure returns empty lists and diagnostics with 'error' key.
    """
    import numpy as np

    min_hz = max(lowest_freq_hz, 1.0)
    max_hz = min(highest_freq_hz, sample_rate / 2.0)
    if max_hz <= min_hz:
        return [], [], {"error": "invalid frequency range"}

    def norm_pos(f: float) -> float:
        if use_bark:
            return _normalized_position_bark(f, min_hz, max_hz)
        return _normalized_position_linear(f, min_hz, max_hz)

    # Build weighted fit points from active partials.
    freqs, y_db, weights = [], [], []
    for entry in frame_partials:
        freq_hz, amplitude, smr = float(entry[0]), float(entry[1]), float(entry[2])
        if amplitude <= 0.0 or freq_hz <= 0.0:
            continue

        db = 20.0 * math.log10(max(amplitude, amp_floor))

        # Amplitude weight (normalised 0-1 from -120..0 dB range)
        amp_w = max(0.0, min(1.0, (db - (-120.0)) / 120.0))
        # SMR weight (normalised 0-1 from -10..30 dB range)
        smr_w = max(0.0, min(1.0, (smr - (-10.0)) / 40.0))
        w = max(1e-4, amp_w * smr_w)

        freqs.append(freq_hz)
        y_db.append(db)
        weights.append(w)

    n_pts = len(freqs)
    if n_pts == 0:
        return [], [], {"error": "no active partials", "fit_points": 0}

    # Reduce order for sparse frames (mirror C++ reduceOrderForSparseFrames).
    eff_order = order
    eff_reg = regularization
    fallback = None
    if n_pts <= eff_order:
        eff_order = max(0, n_pts - 1)
        eff_reg = max(eff_reg, 1e-2)
        fallback = "order_reduced_sparse"

    n_coeffs = eff_order + 1

    # Build B (n_pts × n_coeffs) and weight vector.
    B = np.array([_build_basis_row(norm_pos(f), eff_order) for f in freqs])
    W = np.array(weights)
    Y = np.array(y_db)

    # Second-derivative Tikhonov penalty: diagonal [0, 1^4, 2^4, 3^4, ...]
    D = np.diag([0.0] + [float(k) ** 4 for k in range(1, n_coeffs)])

    # Weighted normal equations: (B^T W B + reg * D) c = B^T W y
    BtW = (B * W[:, None]).T          # n_coeffs × n_pts
    A = BtW @ B + eff_reg * D
    rhs = BtW @ Y

    # Solve; fall back to well-conditioned constant if singular.
    try:
        c = np.linalg.solve(A, rhs)
    except np.linalg.LinAlgError:
        mean_db = float(np.average(Y, weights=W))
        c = np.array([mean_db] + [0.0] * eff_order)
        fallback = "constant_fallback"

    # Condition estimate: (max diag of Cholesky / min)^2 proxy via eigvals.
    try:
        eigvals = np.linalg.eigvalsh(A)
        cond = float(eigvals[-1] / max(eigvals[0], 1e-30))
    except Exception:
        cond = float("inf")

    # Weighted fit error.
    predicted = B @ c
    residuals = predicted - Y
    weighted_err = float(math.sqrt(np.sum(W * residuals ** 2) / max(W.sum(), 1e-30)))

    diag = {
        "fit_points": n_pts,
        "effective_order": eff_order,
        "weighted_error_db": weighted_err,
        "condition_estimate": cond,
        "fallback": fallback,
    }

    # Evaluate on log-spaced grid.
    grid_freqs = np.exp(
        np.linspace(math.log(min_hz), math.log(max_hz), grid_size)
    ).tolist()

    grid_db = []
    for gf in grid_freqs:
        pos = norm_pos(gf)
        basis = _build_basis_row(pos, eff_order)
        grid_db.append(float(np.dot(c, basis)))

    return grid_freqs, grid_db, diag


# ---------------------------------------------------------------------------
# New estimator: Bark axis + Bark-space curvature penalty + BIC order
# selection (1e7 ill-conditioned threshold) + cross-frame gap fill.
# Mirrors the C++ RegularizedCepstrumEnvelopeEstimator with all Tier 1-3
# improvements applied.  Used by the interactive viewer so users can compare
# all three models (linear RDC, Bark legacy RDC, new RDC) in one place.
# ---------------------------------------------------------------------------

def _hz_from_bark(bark_value: float) -> float:
    return 600.0 * math.sinh(bark_value / 6.0)


def _frequency_for_bark_position(position: float, min_hz: float, max_hz: float) -> float:
    b_min = _bark(max(1.0, min_hz))
    b_max = _bark(max_hz)
    return _hz_from_bark(b_min + max(0.0, min(1.0, position)) * (b_max - b_min))


def _basis_row_ndarray(position: float, order: int):
    """Numpy cosine basis row: [1, 2cos(pi*pos), 2cos(2pi*pos), ...]"""
    import numpy as np
    theta = math.pi * max(0.0, min(1.0, position))
    row = np.ones(order + 1, dtype=np.float64)
    for k in range(1, order + 1):
        row[k] = 2.0 * math.cos(k * theta)
    return row


def _build_fit_points_new(
    frame_partials: list,
    min_hz: float,
    max_hz: float,
    amp_floor: float = 1.0e-12,
) -> list:
    """Build weighted fit points with SMR-zero disable (Tier 1.3)."""
    all_smr_zero = True
    for entry in frame_partials:
        if len(entry) >= 3 and float(entry[2]) != 0.0:
            all_smr_zero = False
            break
    points = []
    for entry in frame_partials:
        freq_hz   = float(entry[0])
        amplitude = float(entry[1])
        smr       = float(entry[2]) if len(entry) >= 3 else 0.0
        if not (math.isfinite(freq_hz) and math.isfinite(amplitude)):
            continue
        if freq_hz <= 0.0 or amplitude <= 0.0 or freq_hz < min_hz or freq_hz > max_hz:
            continue
        db = 20.0 * math.log10(max(amplitude, amp_floor))
        amp_norm = max(0.0, min(1.0, (db + 120.0) / 120.0))
        weight = amp_norm
        if not all_smr_zero:
            smr_norm = max(0.0, min(1.0, (smr + 10.0) / 40.0))
            weight *= smr_norm
        points.append({
            "frequency_hz": freq_hz,
            "log_amplitude_db": db,
            "weight": max(1.0e-4, min(1.0, weight)),
        })
    return points


def _build_curvature_penalty(order: int, min_hz: float, max_hz: float):
    """Curvature penalty in Bark-warped frequency space (Tier 3.7)."""
    import numpy as np
    n = order + 1
    matrix = np.zeros((n, n), dtype=np.float64)
    if order <= 0:
        return matrix
    sample_count = max(17, 4 * (order + 1) + 1)
    step = 1.0 / float(sample_count - 1)
    basis_samples = np.zeros((sample_count, n), dtype=np.float64)
    for i in range(sample_count):
        position = i * step
        freq_hz  = _frequency_for_bark_position(position, min_hz, max_hz)
        fit_pos  = _normalized_position_bark(freq_hz, min_hz, max_hz)
        basis_samples[i, :] = _basis_row_ndarray(fit_pos, order)
    for i in range(2, sample_count - 2):
        d = (
            -basis_samples[i + 2]
            + 16.0 * basis_samples[i + 1]
            - 30.0 * basis_samples[i]
            + 16.0 * basis_samples[i - 1]
            - basis_samples[i - 2]
        ) / (12.0 * step * step)
        matrix += np.outer(d, d) * step
    # Re-normalise to match legacy k^4 diagonal magnitude for consistent lambda.
    legacy_diag = np.array([0.0] + [float(k) ** 4 for k in range(1, n)], dtype=np.float64)
    m_diag = np.diag(matrix)
    valid = legacy_diag > 0.0
    if valid.any():
        num = float(m_diag[valid].sum())
        den = float(legacy_diag[valid].sum())
        if den > 0.0:
            matrix /= (num / den)
    return matrix


@dataclass
class _EnvelopeModel:
    """Internal model state returned by fit_new_model."""
    storage: str
    values: object          # np.ndarray
    order: int
    min_hz: float
    max_hz: float
    regularization: float
    weighted_error_db: float
    condition_estimate: float
    fit_points: int
    fallback: object        # str | None


def _evaluate_new_model(model: _EnvelopeModel, frequency_hz: float) -> float:
    import numpy as np
    if model.storage == "samples":
        if model.values.size == 1:
            return float(model.values[0])
        pos    = _normalized_position_bark(frequency_hz, model.min_hz, model.max_hz)
        scaled = pos * float(model.values.size - 1)
        lo = int(math.floor(scaled))
        hi = min(lo + 1, int(model.values.size) - 1)
        t  = scaled - float(lo)
        return float(model.values[lo] + t * (model.values[hi] - model.values[lo]))
    pos = _normalized_position_bark(frequency_hz, model.min_hz, model.max_hz)
    return float(np.dot(model.values, _basis_row_ndarray(pos, model.order)))


def _solve_new_attempt(
    points: list,
    order: int,
    regularization: float,
    ridge: float,
    min_hz: float,
    max_hz: float,
):
    import numpy as np
    n = order + 1
    if not points:
        return None
    penalty = _build_curvature_penalty(order, min_hz, max_hz)
    B = np.zeros((len(points), n), dtype=np.float64)
    W = np.zeros(len(points), dtype=np.float64)
    Y = np.zeros(len(points), dtype=np.float64)
    for i, pt in enumerate(points):
        B[i, :] = _basis_row_ndarray(
            _normalized_position_bark(pt["frequency_hz"], min_hz, max_hz), order)
        W[i] = pt["weight"]
        Y[i] = pt["log_amplitude_db"]
    BtW = (B * W[:, None]).T
    A   = BtW @ B + max(0.0, ridge) * np.eye(n, dtype=np.float64)
    if regularization > 0.0:
        A += regularization * penalty
    rhs = BtW @ Y
    try:
        coefficients = np.linalg.solve(A, rhs)
    except np.linalg.LinAlgError:
        return None
    predicted = B @ coefficients
    w_sum = max(float(W.sum()), 1.0e-30)
    w_err = math.sqrt(float(np.sum(W * (predicted - Y) ** 2) / w_sum))
    try:
        eigvals = np.linalg.eigvalsh(A)
        cond = float(eigvals[-1] / max(eigvals[0], 1.0e-30))
    except np.linalg.LinAlgError:
        cond = float("inf")
    return _EnvelopeModel(
        storage="coefficients", values=coefficients, order=order,
        min_hz=min_hz, max_hz=max_hz, regularization=regularization,
        weighted_error_db=w_err, condition_estimate=cond,
        fit_points=len(points), fallback=None,
    )


def _append_virtual_gap(
    points: list,
    prev_model: _EnvelopeModel,
    weight_scale: float,
    min_gap: float,
    max_count: int,
) -> list:
    """Cross-frame gap fill with low-weight virtual points (Tier 3.8)."""
    if prev_model is None or max_count <= 0 or not points:
        return points
    current_positions = sorted(
        _normalized_position_bark(p["frequency_hz"], prev_model.min_hz, prev_model.max_hz)
        for p in points
    )
    taken = list(current_positions)
    base_weight = max(
        1.0e-6,
        sum(p["weight"] for p in points) / len(points) * weight_scale,
    )
    support = []
    for i in range(max_count):
        pos  = 0.5 if max_count == 1 else i / float(max_count - 1)
        fhz  = _frequency_for_bark_position(pos, prev_model.min_hz, prev_model.max_hz)
        db   = _evaluate_new_model(prev_model, fhz)
        support.append({"frequency_hz": fhz, "log_amplitude_db": db, "weight": 1.0})
    candidates = []
    for sp in support:
        pos = _normalized_position_bark(sp["frequency_hz"], prev_model.min_hz, prev_model.max_hz)
        gap = min((abs(pos - cp) for cp in current_positions), default=1.0)
        if gap < min_gap:
            continue
        candidates.append((gap, pos, dict(sp, weight=base_weight)))
    candidates.sort(reverse=True)
    augmented = list(points)
    start_n = len(points)
    for _, pos, sp in candidates:
        if len(augmented) >= start_n + max_count:
            break
        if min((abs(pos - tp) for tp in taken), default=1.0) < min_gap * 0.5:
            continue
        augmented.append(sp)
        taken.append(pos)
        taken.sort()
    return augmented


def _gcv_lambda_search(
    points: list,
    order: int,
    min_hz: float,
    max_hz: float,
    lambda_min: float = 1.0e-5,
    lambda_max: float = 1.0e+2,
    n_candidates: int = 10,
    ridge: float = 1.0e-10,
    max_range_extensions: int = 0,
) -> float:
    """GCV lambda search mirroring C++ selectGcvLambda.

    Returns the lambda in [lambda_min, lambda_max] that minimises
        GCV(λ) = RSS(λ) / (n_eff · (1 − df(λ)/n_eff)²)
    where df(λ) = tr(A_λ⁻¹ · M), M = XᵀWX, A_λ = M + λP.
    Falls back to lambda_min if no candidate produces a finite score.

    When the optimum falls at lambda_max, max_range_extensions controls how many
    10× range doublings are attempted (default 0 = disabled).  Saturation at
    lambda_max is the correct answer for featureless frames (maximum regularization
    → constant envelope); extending the range can cause condition blow-up because
    the curvature penalty does not penalise the DC (k=0) direction.
    """
    import numpy as np
    if not points or order < 0:
        return lambda_min

    n = order + 1
    penalty = _build_curvature_penalty(order, min_hz, max_hz)

    B = np.zeros((len(points), n), dtype=np.float64)
    W = np.zeros(len(points),     dtype=np.float64)
    Y = np.zeros(len(points),     dtype=np.float64)
    for i, pt in enumerate(points):
        B[i, :] = _basis_row_ndarray(
            _normalized_position_bark(pt["frequency_hz"], min_hz, max_hz), order)
        W[i] = pt["weight"]
        Y[i] = pt["log_amplitude_db"]

    BtW = (B * W[:, None]).T   # (n, N_pts)
    M   = BtW @ B              # XᵀWX (n×n)
    rhs = BtW @ Y              # XᵀWy (n,)

    w_sum    = max(float(W.sum()), 1.0e-30)
    w_sum_sq = max(float((W * W).sum()), 1.0e-60)
    n_eff    = max(1.0, (w_sum * w_sum) / w_sum_sq)

    ridge_mat = max(0.0, ridge) * np.eye(n, dtype=np.float64)

    def _search_range(lo: float, hi: float) -> tuple[float, float]:
        """Search [lo, hi] with n_candidates log-spaced points.
        Returns (best_lambda, best_gcv)."""
        candidates = np.logspace(math.log10(max(lo, 1.0e-12)),
                                 math.log10(max(hi, lo * 10.0)),
                                 n_candidates)
        blam = float(candidates[0])
        bgcv = float("inf")
        for lam in candidates:
            A = M + ridge_mat + float(lam) * penalty
            try:
                beta    = np.linalg.solve(A, rhs)
                A_inv_M = np.linalg.solve(A, M)
                df      = float(np.trace(A_inv_M))
            except np.linalg.LinAlgError:
                continue
            df    = max(0.0, min(df, n_eff * 0.99))
            pred  = B @ beta
            rss   = float(np.sum(W * (pred - Y) ** 2))
            denom = 1.0 - df / n_eff
            if denom < 1.0e-6:
                continue
            gcv = rss / (n_eff * denom * denom)
            if math.isfinite(gcv) and gcv < bgcv:
                bgcv = gcv
                blam = float(lam)
        return blam, bgcv

    best_lam, best_gcv = _search_range(lambda_min, lambda_max)
    current_max = lambda_max

    # Automatic range widening: if optimum is at the upper boundary, extend.
    for _ in range(max_range_extensions):
        if best_lam < current_max * 0.99:
            break  # interior minimum found
        new_max = current_max * 10.0
        ext_lam, ext_gcv = _search_range(current_max, new_max)
        if ext_gcv < best_gcv:
            best_gcv = ext_gcv
            best_lam = ext_lam
        current_max = new_max

    return best_lam


def fit_new_model(
    frame_partials: list,
    sample_rate: float,
    lowest_freq_hz: float,
    highest_freq_hz: float,
    order: int = 16,
    regularization: float = 1.0e-3,
    previous_model=None,    # _EnvelopeModel | None
    grid_size: int = 512,
    use_gcv: bool = False,
) -> tuple:
    """Fit the new estimator: Bark axis + Bark-space curvature prior + BIC order
    selection (1e7 ill-conditioned threshold) + cross-frame gap fill (Tier 1-3).

    Returns (grid_freqs_hz, grid_db, diagnostics_dict, _EnvelopeModel).
    """
    import numpy as np

    min_hz = max(lowest_freq_hz, 1.0)
    max_hz = min(highest_freq_hz, sample_rate * 0.5)
    points = _build_fit_points_new(frame_partials, min_hz, max_hz)
    usable = [p for p in points if min_hz <= p["frequency_hz"] <= max_hz and p["weight"] > 0.0]

    if not usable:
        m = _EnvelopeModel(
            storage="samples", values=np.array([0.0]), order=0,
            min_hz=min_hz, max_hz=max_hz, regularization=0.0,
            weighted_error_db=0.0, condition_estimate=float("inf"),
            fit_points=0, fallback="too_few_points",
        )
        return [], [], {"error": "no usable fit points", "fit_points": 0}, m

    eff_order = order
    eff_reg   = regularization
    fallback  = None
    if len(usable) <= eff_order:
        eff_order = max(0, len(usable) - 1)
        eff_reg   = max(eff_reg, 1.0e-2)
        fallback  = "order_reduced_sparse"

    augmented = _append_virtual_gap(usable, previous_model, 0.05, 0.1, 6)

    # BIC order selection — mirrors C++ useBicOrderSelection with 1e7 threshold.
    # bicMinOrderPartialDensity=0.15 prevents collapse to order 1 on dense frames
    # (C++ enforces minBicOrder = ceil(n_partials * 0.15)).
    if eff_order > 1:
        min_bic_order = max(1, math.ceil(len(usable) * 0.15))
        best_order, best_bic = eff_order, float("inf")
        for trial_order in range(min_bic_order, eff_order + 1):
            t = _solve_new_attempt(augmented, trial_order, eff_reg, 1.0e-10, min_hz, max_hz)
            if t is None or t.condition_estimate > 1.0e7:
                continue
            n = max(t.fit_points, 1)
            bic = n * math.log(max(t.weighted_error_db ** 2, 1.0e-30)) + (trial_order + 1) * math.log(n)
            if bic < best_bic:
                best_bic   = bic
                best_order = trial_order
        eff_order = best_order

    # GCV lambda selection: after BIC fixes eff_order, search lambda space.
    gcv_lambda = None
    if use_gcv:
        gcv_lambda = _gcv_lambda_search(augmented, eff_order, min_hz, max_hz,
                                        lambda_min=1.0e-5, lambda_max=1.0e+2,
                                        n_candidates=10, ridge=1.0e-10)
        eff_reg = gcv_lambda

    result = _solve_new_attempt(augmented, eff_order, eff_reg, 1.0e-10, min_hz, max_hz)
    if result is None or result.condition_estimate > 1.0e7:
        retry_reg = max(eff_reg * 10.0, 1.0, 1.0e-2)
        retry = _solve_new_attempt(augmented, eff_order, retry_reg, 1.0e-10, min_hz, max_hz)
        if retry is not None:
            retry.fallback = "ill_conditioned_retry"
            result = retry
        else:
            w_sum   = max(sum(p["weight"] for p in augmented), 1.0e-30)
            mean_db = sum(p["weight"] * p["log_amplitude_db"] for p in augmented) / w_sum
            result  = _EnvelopeModel(
                storage="samples", values=np.array([mean_db]), order=0,
                min_hz=min_hz, max_hz=max_hz, regularization=0.0,
                weighted_error_db=0.0, condition_estimate=float("inf"),
                fit_points=len(augmented), fallback="constant_fallback",
            )

    if fallback and result.fallback is None:
        result.fallback = fallback

    grid_freqs = np.exp(np.linspace(math.log(min_hz), math.log(max_hz), grid_size)).tolist()
    grid_db    = [_evaluate_new_model(result, f) for f in grid_freqs]
    diag = {
        "fit_points":        len(augmented),
        "real_fit_points":   len(usable),
        "effective_order":   result.order,
        "weighted_error_db": result.weighted_error_db,
        "condition_estimate": result.condition_estimate,
        "fallback":          result.fallback,
        "virtual_points":    max(0, len(augmented) - len(usable)),
        "gcv_lambda":        gcv_lambda,
        "gcv_was_used":      use_gcv,
    }
    return grid_freqs, grid_db, diag, result


# ---------------------------------------------------------------------------
# Frame-dump runner
# ---------------------------------------------------------------------------

def run_frame_dump(wav_path: Path, ats_offline: str,
                   analysis_type: int, cache_path: str | None) -> dict:
    """Run ats_offline --dump-frames, optionally caching the JSON."""
    if cache_path and os.path.isfile(cache_path):
        print(f"  Loading cached frame dump: {cache_path}")
        with open(cache_path, "r") as f:
            return json.load(f)

    with tempfile.TemporaryDirectory() as tmp:
        mono_wav = Path(tmp) / "mono16.wav"
        try:
            convert_to_mono16(wav_path, mono_wav)
        except Exception as exc:
            raise SystemExit(f"Conversion to mono 16-bit failed: {exc}")

        dump_path = Path(tmp) / "frames.json"
        out_wav   = Path(tmp) / "resynth.wav"

        cmd = [
            ats_offline,
            "--type", str(analysis_type),
            "--dump-frames", str(dump_path),
            str(mono_wav),
            str(out_wav),
        ]
        print(f"  Running: {' '.join(cmd)}")
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

        with open(dump_path, "r") as f:
            data = json.load(f)

    if cache_path:
        with open(cache_path, "w") as f:
            json.dump(data, f)
        print(f"  Frame dump cached to: {cache_path}")

    return data


# ---------------------------------------------------------------------------
# Interactive matplotlib viewer
# ---------------------------------------------------------------------------

def run_viewer(
    dump: dict,
    wav_name: str,
    order: int,
    reg: float,
    show_bark: bool,
    show_new_model: bool = True,
    show_density: bool = True,
    use_gcv: bool = False,
) -> None:
    try:
        import matplotlib.pyplot as plt
        import matplotlib.widgets as widgets
    except ImportError:
        raise SystemExit("Install matplotlib:  pip install matplotlib")

    frame_data   = dump["frame_data"]
    sample_rate  = float(dump["sample_rate"])
    lowest_freq  = float(dump.get("lowest_freq_hz", 20.0))
    highest_freq = float(dump.get("highest_freq_hz", sample_rate / 2.0))
    n_frames     = len(frame_data)
    lo_hz        = max(lowest_freq, 20.0)

    if n_frames == 0:
        raise SystemExit("No frames in dump.")

    # Pre-compute envelopes for all frames (fast with numpy).
    print(f"  Pre-computing RDC envelopes for {n_frames} frames …", end="", flush=True)
    envelopes_lin  = []
    envelopes_bark = [] if show_bark else None
    diagnostics    = []

    for fd in frame_data:
        gf, gdb, diag = rdc_fit_and_eval(
            fd["partials"], sample_rate, lowest_freq, highest_freq,
            order=order, regularization=reg, use_bark=False,
        )
        envelopes_lin.append((gf, gdb))
        diagnostics.append(diag)
        if show_bark:
            gf_b, gdb_b, _ = rdc_fit_and_eval(
                fd["partials"], sample_rate, lowest_freq, highest_freq,
                order=order, regularization=reg, use_bark=True,
            )
            envelopes_bark.append((gf_b, gdb_b))

    # Pre-compute new model envelopes (sequential — each frame uses previous).
    envelopes_new = None
    new_diag_list = None
    if show_new_model:
        envelopes_new = []
        new_diag_list = []
        prev_model = None
        for fd in frame_data:
            gf_n, gdb_n, diag_n, prev_model = fit_new_model(
                fd["partials"], sample_rate, lowest_freq, highest_freq,
                order=order, regularization=reg, previous_model=prev_model,
                use_gcv=use_gcv,
            )
            envelopes_new.append((gf_n, gdb_n))
            new_diag_list.append(diag_n)

    # Pre-compute partial density counts for heatmap (Tier 2.6).
    n_density_bins = 40
    density_edges = [
        lo_hz * math.exp(
            i * (math.log(highest_freq) - math.log(lo_hz)) / n_density_bins
        )
        for i in range(n_density_bins + 1)
    ]
    density_centers = [
        (density_edges[i] * density_edges[i + 1]) ** 0.5
        for i in range(n_density_bins)
    ]
    density_widths = [
        density_edges[i + 1] - density_edges[i]
        for i in range(n_density_bins)
    ]

    def _density_counts(fd: dict) -> list:
        counts = [0] * n_density_bins
        for entry in fd["partials"]:
            f = float(entry[0])
            a = float(entry[1])
            if a <= 0.0 or f <= 0.0:
                continue
            b = bisect.bisect_right(density_edges, f) - 1
            if 0 <= b < n_density_bins:
                counts[b] += 1
        return counts

    all_density       = [_density_counts(fd) for fd in frame_data]
    global_max_density = max((max(c) for c in all_density if c), default=1)
    density_cmap      = plt.cm.plasma

    print(" done.")

    # ---- Figure layout ----
    fig = plt.figure(figsize=(13, 8))
    fig.suptitle(
        f"{wav_name}  —  use ← → / PgUp PgDn to navigate, Q to quit",
        fontsize=10,
    )

    if show_density:
        ax_main    = fig.add_axes([0.07, 0.27, 0.88, 0.64])
        ax_density = fig.add_axes([0.07, 0.15, 0.88, 0.09])
        ax_slide   = fig.add_axes([0.12, 0.05, 0.76, 0.04])
    else:
        ax_main    = fig.add_axes([0.07, 0.20, 0.88, 0.70])
        ax_density = None
        ax_slide   = fig.add_axes([0.12, 0.06, 0.76, 0.04])

    ax_main.set_xlabel("Frequency (Hz)")
    ax_main.set_ylabel("Amplitude (dB)")
    ax_main.set_xscale("log")
    ax_main.set_xlim(lo_hz, highest_freq)
    ax_main.grid(True, which="both", alpha=0.3)

    if ax_density is not None:
        ax_density.set_xscale("log")
        ax_density.set_xlim(lo_hz, highest_freq)
        ax_density.set_yticks([])
        ax_density.tick_params(labelbottom=False, bottom=False)
        ax_density.set_ylabel("density", fontsize=7, rotation=90, va="center")
        ax_density.grid(False)

    # Compute global dB range for stable y-axis.
    all_dbs = []
    for fd in frame_data:
        for entry in fd["partials"]:
            a = float(entry[1])
            if a > 0.0:
                all_dbs.append(20.0 * math.log10(max(a, 1e-12)))
    y_min = min(all_dbs) - 6 if all_dbs else -100
    y_max = max(all_dbs) + 6 if all_dbs else 0
    ax_main.set_ylim(y_min, y_max)

    # Initial plot objects.
    scatter, = ax_main.plot(
        [], [], "o", color="#4a9fd5", markersize=4, alpha=0.8,
        label="Partials", zorder=3,
    )
    env_line, = ax_main.plot(
        [], [], "-", color="#e05c2a", linewidth=1.8, alpha=0.9,
        label=f"RDC linear (order={order})", zorder=2,
    )
    bark_line = None
    if show_bark:
        bark_line, = ax_main.plot(
            [], [], "--", color="#2ab07a", linewidth=1.5, alpha=0.85,
            label=f"RDC Bark legacy (order={order})", zorder=2,
        )
    new_line = None
    if show_new_model:
        new_line, = ax_main.plot(
            [], [], "-", color="#7b3fc4", linewidth=2.2, alpha=0.9,
            label=f"New RDC (Bark+curvature+BIC+gap fill, order≤{order})",
            zorder=2,
        )

    ax_main.legend(loc="upper right", fontsize=7)
    info_text = ax_main.text(
        0.01, 0.97, "", transform=ax_main.transAxes,
        va="top", fontsize=7, family="monospace", color="#333333",
    )

    slider = widgets.Slider(
        ax_slide, "Frame", 0, n_frames - 1, valinit=0, valstep=1, color="#4a9fd5",
    )

    state = {"frame": 0, "updating": False}

    def draw_frame(idx: int, from_slider: bool = False) -> None:
        if state["updating"]:
            return
        state["updating"] = True
        try:
            idx = max(0, min(n_frames - 1, idx))
            state["frame"] = idx

            fd  = frame_data[idx]
            pts = fd["partials"]
            freqs = [float(e[0]) for e in pts if float(e[1]) > 0.0]
            amps  = [20.0 * math.log10(max(float(e[1]), 1e-12))
                     for e in pts if float(e[1]) > 0.0]

            scatter.set_data(freqs, amps)
            env_line.set_data(*envelopes_lin[idx])

            if bark_line is not None:
                bark_line.set_data(*envelopes_bark[idx])
            if new_line is not None:
                new_line.set_data(*envelopes_new[idx])

            diag         = diagnostics[idx]
            fallback_str = (f"  fb={diag['fallback']}"
                            if diag.get("fallback") else "")
            new_info = ""
            if new_diag_list is not None:
                nd     = new_diag_list[idx]
                new_fb = f"  fb={nd.get('fallback') or 'none'}"
                new_info = (
                    f"\nNew: pts={nd.get('fit_points', 0)}"
                    f"(+{nd.get('virtual_points', 0)}virt) "
                    f"ord={nd.get('effective_order', 0)} "
                    f"err={nd.get('weighted_error_db', 0.0):.2f}dB "
                    f"cond≈{nd.get('condition_estimate', 0.0):.1e}"
                    f"{new_fb}"
                )
            info = (
                f"Frame {idx}/{n_frames - 1}   t={fd['time_s']:.4f}s   "
                f"{len(freqs)} active partials\n"
                f"Lin: pts={diag.get('fit_points', 0)}  "
                f"ord={diag.get('effective_order', 0)}  "
                f"err={diag.get('weighted_error_db', 0.0):.2f}dB  "
                f"cond≈{diag.get('condition_estimate', 0.0):.1e}"
                f"{fallback_str}"
                f"{new_info}"
            )
            info_text.set_text(info)

            # Update density heatmap.
            if ax_density is not None:
                ax_density.cla()
                ax_density.set_xscale("log")
                ax_density.set_xlim(lo_hz, highest_freq)
                ax_density.set_yticks([])
                ax_density.tick_params(labelbottom=False, bottom=False)
                ax_density.set_ylabel("density", fontsize=7, rotation=90, va="center")
                counts  = all_density[idx]
                max_c   = max(global_max_density, 1)
                colors  = [density_cmap(c / max_c) for c in counts]
                ax_density.bar(
                    density_centers, counts,
                    width=density_widths,
                    color=colors, align="center", linewidth=0,
                )

            if not from_slider:
                slider.set_val(idx)
            fig.canvas.draw_idle()
        finally:
            state["updating"] = False

    def on_slider(val: float) -> None:
        draw_frame(int(round(val)), from_slider=True)

    def on_key(event) -> None:
        f = state["frame"]
        if event.key in ("right", "d"):
            draw_frame(f + 1)
        elif event.key in ("left", "a"):
            draw_frame(f - 1)
        elif event.key == "pagedown":
            draw_frame(f + 10)
        elif event.key == "pageup":
            draw_frame(f - 10)
        elif event.key == "home":
            draw_frame(0)
        elif event.key == "end":
            draw_frame(n_frames - 1)
        elif event.key in ("q", "escape"):
            plt.close("all")

    slider.on_changed(on_slider)
    fig.canvas.mpl_connect("key_press_event", on_key)

    draw_frame(0)
    print("  Viewer open.  ← / → arrows or slider to navigate.  Q to quit.")
    plt.show()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Interactive spectral envelope browser for ATS partial frames."
    )
    parser.add_argument("wav", help="Input WAV file (any sample rate/channels).")
    parser.add_argument("--ats-offline", default="./build/tools/ats_offline",
                        help="Path to ats_offline binary (default: ./build/tools/ats_offline).")
    parser.add_argument("--type", dest="analysis_type", type=int, default=4,
                        choices=[1, 2, 3, 4],
                        help="ATS analysis type (default: 4).")
    parser.add_argument("--order", type=int, default=16,
                        help="RDC cepstrum order (default: 16).")
    parser.add_argument("--reg", type=float, default=1e-3,
                        help="RDC regularization (default: 1e-3).")
    parser.add_argument("--bark", action="store_true",
                        help="Overlay a Bark-warped RDC envelope (legacy, for comparison).")
    parser.add_argument("--new-model", dest="new_model", action="store_true", default=True,
                        help="Overlay new RDC model (Bark+curvature+BIC+gap fill). Default: on.")
    parser.add_argument("--no-new-model", dest="new_model", action="store_false",
                        help="Hide the new RDC model overlay.")
    parser.add_argument("--density", dest="density", action="store_true", default=True,
                        help="Show partial-density heatmap bar below main plot. Default: on.")
    parser.add_argument("--no-density", dest="density", action="store_false",
                        help="Hide the density heatmap bar.")
    parser.add_argument("--gcv", dest="use_gcv", action="store_true", default=False,
                        help="Use GCV lambda selection in the new model (default: off).")
    parser.add_argument("--no-gcv", dest="use_gcv", action="store_false",
                        help="Disable GCV lambda selection.")
    parser.add_argument("--cache", default=None, metavar="PATH",
                        help="Cache frame-dump JSON at PATH to skip re-analysis on re-runs.")
    args = parser.parse_args()

    wav_path = Path(args.wav)
    if not wav_path.is_file():
        print(f"error: WAV not found: {wav_path}", file=sys.stderr)
        return 1

    ats_offline = args.ats_offline
    if not os.path.isfile(ats_offline):
        print(f"error: ats_offline not found: {ats_offline}", file=sys.stderr)
        return 1

    print(f"Analyzing: {wav_path.name}")
    dump = run_frame_dump(wav_path, ats_offline, args.analysis_type, args.cache)
    print(
        f"  {dump['frames']} frames  |  {dump['partials']} partials  |"
        f"  sr={dump['sample_rate']} Hz  |"
        f"  dur={dump['duration_seconds']:.3f}s"
    )

    run_viewer(dump, wav_path.name, args.order, args.reg, args.bark,
               show_new_model=args.new_model, show_density=args.density,
               use_gcv=args.use_gcv)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

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


# ---------------------------------------------------------------------------
# Mel frequency axis (PRD Tier 2 — third axis option alongside Bark/Linear)
# Uses the standard O'Shaughnessy formula: mel = 2595 * log10(1 + f/700).
# Slightly more uniform than Bark in the 1–5 kHz musical range.
# ---------------------------------------------------------------------------

def _mel(f: float) -> float:
    return 2595.0 * math.log10(1.0 + max(0.0, f) / 700.0)


def _hz_from_mel(mel_val: float) -> float:
    return 700.0 * (10.0 ** (mel_val / 2595.0) - 1.0)


def _normalized_position_mel(freq_hz: float,
                               min_hz: float, max_hz: float) -> float:
    m_min = _mel(max(1.0, min_hz))
    m_max = _mel(max_hz)
    m_val = _mel(max(1.0, freq_hz))
    if m_max <= m_min:
        return 0.0
    return max(0.0, min(1.0, (m_val - m_min) / (m_max - m_min)))


def _normalized_position_axis(
    freq_hz: float,
    min_hz: float,
    max_hz: float,
    axis: str,
) -> float:
    if axis == "linear":
        return _normalized_position_linear(freq_hz, min_hz, max_hz)
    if axis == "mel":
        return _normalized_position_mel(freq_hz, min_hz, max_hz)
    return _normalized_position_bark(freq_hz, min_hz, max_hz)


def _frequency_for_position(
    position: float,
    min_hz: float,
    max_hz: float,
    axis: str,
) -> float:
    pos = max(0.0, min(1.0, position))
    if axis == "linear":
        return min_hz + pos * (max_hz - min_hz)
    if axis == "mel":
        m_min = _mel(max(1.0, min_hz))
        m_max = _mel(max_hz)
        return _hz_from_mel(m_min + pos * (m_max - m_min))
    b_min = _bark(max(1.0, min_hz))
    b_max = _bark(max_hz)
    return _hz_from_bark(b_min + pos * (b_max - b_min))


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
    use_mel: bool = False,
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
        if use_mel:
            return _normalized_position_mel(f, min_hz, max_hz)
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
    return _frequency_for_position(position, min_hz, max_hz, "bark")


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
    return _build_curvature_penalty_for_axis(order, min_hz, max_hz, "bark")


def _build_curvature_penalty_for_axis(order: int, min_hz: float, max_hz: float, axis: str):
    """Curvature penalty in the chosen warped frequency space."""
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
        freq_hz  = _frequency_for_position(position, min_hz, max_hz, axis)
        fit_pos  = _normalized_position_axis(freq_hz, min_hz, max_hz, axis)
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
    # Posterior uncertainty (Stage 2 of GCV plan): A^{-1} and residual variance.
    # Populated by the final IRLS solve; None when solve failed or is a fallback.
    hessian_inv: object = None   # np.ndarray (order+1)×(order+1) or None
    noise_variance: float = 0.0  # σ² = weighted residual variance (in dB²)
    axis: str = "bark"
    blend_components: object = None   # list[tuple[float, _EnvelopeModel]] | None
    solve_status: str = "ok"
    release_details: object = None


@dataclass
class TemporalEnvelopeFrame:
    partials: list
    transient_boundary: bool = False
    motion_scale: float = 1.0
    residual_scale: float = 1.0
    label: str = ""


@dataclass
class TemporalEnvelopeSmoothingOptions:
    enabled: bool = False
    bark_grid_size: int = 48
    process_noise_db: float = 0.24
    observation_floor_db: float = 0.18
    transient_process_scale: float = 24.0
    delta_process_scale: float = 4.0
    motion_process_scale: float = 1.2
    residual_process_scale: float = 0.8
    high_delta_db: float = 1.1
    min_gain: float = 0.08
    max_gain: float = 0.96


@dataclass
class SubbandAdaptiveOptions:
    enabled: bool = False
    axis: str = "bark"
    band_count: int = 3
    overlap: float = 0.14
    min_band_points: int = 1
    sample_grid_size: int = 96
    seam_rms_threshold_db: float = 0.75
    seam_max_threshold_db: float = 1.80


@dataclass
class RefinementOptions:
    enabled: bool = False
    bark_grid_size: int = 72
    max_iterations: int = 3
    initial_step_size: float = 0.55
    min_step_size: float = 0.05
    max_delta_db: float = 1.25
    blur_weight: float = 0.35
    anchor_strength: float = 0.20
    uncertainty_gain: float = 0.65
    dense_target_hz: list[float] | None = None
    dense_target_db: list[float] | None = None
    target_label: str = ""


@dataclass
class _TemporalEnvelopeState:
    grid_freqs_hz: list[float]
    values_db: object
    variance_db2: object
    frame_index: int
    label: str = ""


def _evaluate_new_model(model: _EnvelopeModel, frequency_hz: float) -> float:
    import numpy as np
    if model.storage == "blend" and model.blend_components:
        return float(sum(
            weight * _evaluate_new_model(component, frequency_hz)
            for weight, component in model.blend_components
        ))
    if model.storage == "samples":
        if model.values.size == 1:
            return float(model.values[0])
        pos    = _normalized_position_bark(frequency_hz, model.min_hz, model.max_hz)
        scaled = pos * float(model.values.size - 1)
        lo = int(math.floor(scaled))
        hi = min(lo + 1, int(model.values.size) - 1)
        t  = scaled - float(lo)
        return float(model.values[lo] + t * (model.values[hi] - model.values[lo]))
    pos = _normalized_position_axis(frequency_hz, model.min_hz, model.max_hz, model.axis)
    return float(np.dot(model.values, _basis_row_ndarray(pos, model.order)))


def _evaluate_prediction_sigma(model: _EnvelopeModel, frequency_hz: float) -> float:
    """Return posterior predictive standard deviation (dB) at frequency_hz.

    Computed as sqrt(σ² · x(f)ᵀ A⁻¹ x(f)) where σ² = noise_variance,
    A⁻¹ = hessian_inv, and x(f) is the cosine basis vector at f.
    Returns float('nan') when uncertainty information is not available.
    """
    import numpy as np
    if model.storage == "blend" and model.blend_components:
        variance = 0.0
        has_component_sigma = False
        for weight, component in model.blend_components:
            sigma = _evaluate_prediction_sigma(component, frequency_hz)
            if math.isfinite(sigma):
                variance += (weight * sigma) ** 2
                has_component_sigma = True
        return math.sqrt(max(0.0, variance)) if has_component_sigma else float("nan")
    if model.hessian_inv is None or model.noise_variance <= 0.0:
        return float("nan")
    pos = _normalized_position_axis(frequency_hz, model.min_hz, model.max_hz, model.axis)
    x = _basis_row_ndarray(pos, model.order)
    variance = model.noise_variance * max(0.0, float(x @ model.hessian_inv @ x))
    return math.sqrt(variance)


def _solve_new_attempt(
    points: list,
    order: int,
    regularization: float,
    ridge: float,
    min_hz: float,
    max_hz: float,
    irls_max_iterations: int = 4,
    n_real_points: int = 0,
    axis: str = "bark",
    true_envelope_release_mode: str = "heuristic",
):
    """Assemble and solve the weighted regularized normal equations.

    irls_max_iterations controls Huber-loss IRLS robustness (mirrors C++
    irlsMaxIterations=4).  Set to 1 for pure L2 (BIC/GCV search paths).
    After the final IRLS iteration, A⁻¹ and σ² are stored in the returned
    _EnvelopeModel for downstream uncertainty evaluation.

    n_real_points — when > 0, specifies how many leading entries in `points`
    are real partials (vs low-weight virtual gap-fill points appended after).
    IRLS robust-scale (MAD) and the final noise_variance are then computed
    from real partials only, preventing virtual-point residuals (which are
    near zero by construction) from skewing the Huber threshold and
    uncertainty estimate.  Pass 0 (default) for the legacy behaviour where
    all points are treated equally.
    """
    import numpy as np
    n = order + 1
    if not points:
        return None
    n_real = n_real_points if 0 < n_real_points <= len(points) else len(points)
    n_virtual = max(0, len(points) - n_real)
    penalty = _build_curvature_penalty_for_axis(order, min_hz, max_hz, axis)
    B = np.zeros((len(points), n), dtype=np.float64)
    W_base = np.zeros(len(points), dtype=np.float64)
    Y = np.zeros(len(points), dtype=np.float64)
    for i, pt in enumerate(points):
        B[i, :] = _basis_row_ndarray(
            _normalized_position_axis(pt["frequency_hz"], min_hz, max_hz, axis), order)
        W_base[i] = pt["weight"]
        Y[i] = pt["log_amplitude_db"]

    ridge_mat  = max(0.0, ridge) * np.eye(n, dtype=np.float64)
    penalty_P  = regularization * penalty if regularization > 0.0 else np.zeros((n, n), dtype=np.float64)

    # Adaptive Huber sigma floor: sparse real frames need a more conservative
    # floor (fewer points → noisier MAD estimate).  Converges to 0.5 dB for
    # dense frames (n_real ≥ 30) and rises smoothly to ~2 dB for n_real ≤ 4.
    sigma_floor = max(0.5, 2.0 * math.exp(-n_real / 8.0))

    # IRLS: start with Huber weights = 1 (pure L2 first iter), then update.
    irls_w = np.ones(len(points), dtype=np.float64)
    release_w = np.zeros(len(points), dtype=np.float64)
    release_targets = np.zeros(len(points), dtype=np.float64)
    release_bounds = np.zeros(len(points), dtype=np.float64)
    constrained_peaks = 0
    unclamped_outliers = 0
    solve_status = "ok"
    max_iter = max(1, irls_max_iterations + (4 if true_envelope_release_mode == "bounded" and n_virtual > 0 else 0))
    A = ridge_mat  # will be set inside the loop
    coefficients = np.zeros(n, dtype=np.float64)
    final_sigma_hat = sigma_floor
    outlier_cutoff_db = 0.0

    for iter_idx in range(max_iter):
        W_eff = W_base * irls_w
        BtW   = (B * W_eff[:, None]).T
        A     = BtW @ B + ridge_mat + penalty_P
        rhs   = BtW @ Y
        if true_envelope_release_mode == "bounded" and n_virtual > 0:
            for i in range(n_real):
                if release_w[i] <= 0.0:
                    continue
                basis = B[i]
                A += release_w[i] * np.outer(basis, basis)
                rhs += release_w[i] * release_targets[i] * basis
        try:
            coefficients = np.linalg.solve(A, rhs)
        except np.linalg.LinAlgError:
            solve_status = "solve_failed"
            return None

        predicted  = B @ coefficients
        residuals  = np.abs(predicted - Y)
        W_real     = W_eff[:n_real]
        w_sum_real = max(float(W_real.sum()), 1.0e-30)
        res_real   = (predicted - Y)[:n_real]
        w_err_real = math.sqrt(float(np.sum(W_real * res_real ** 2) / w_sum_real))

        if iter_idx == max_iter - 1:
            break

        # Huber IRLS weight update.
        # Robust scale: MAD of real-partial residuals / 0.6745, with adaptive
        # floor.  Using real-only residuals prevents the near-zero residuals of
        # virtual gap-fill points from artificially tightening the Huber
        # threshold and downweighting legitimate real partials.
        # Delta constant 1.345 is the asymptotically optimal Huber value for
        # 95% efficiency under a normal error distribution.
        real_res_sorted = np.sort(residuals[:n_real])
        sigma_hat   = max(float(real_res_sorted[len(real_res_sorted) // 2]) / 0.6745,
                          sigma_floor)
        final_sigma_hat = sigma_hat
        p90_residual = float(real_res_sorted[int(0.90 * (len(real_res_sorted) - 1))])
        max_residual = float(real_res_sorted[-1])

        signed_real = (Y - predicted)[:n_real]
        new_release_w = np.zeros(len(points), dtype=np.float64)
        new_release_targets = np.zeros(len(points), dtype=np.float64)
        new_release_bounds = np.zeros(len(points), dtype=np.float64)
        new_constrained_peaks = 0
        new_unclamped_outliers = 0

        if n_virtual > 0:
            if true_envelope_release_mode == "bounded":
                try:
                    A_inv = np.linalg.inv(A)
                except np.linalg.LinAlgError:
                    A_inv = None
                release_floor_db = 0.18
                release_ceiling_db = 0.55
                outlier_cutoff_db = 4.0
                for i in range(n_real):
                    if A_inv is not None:
                        local_variance = max(0.0, float(B[i] @ A_inv @ B[i]))
                        local_sigma = sigma_hat * math.sqrt(local_variance)
                    else:
                        local_sigma = sigma_hat
                    bound_db = min(
                        release_ceiling_db,
                        max(release_floor_db, 0.12 + 0.06 * sigma_hat + 0.14 * local_sigma),
                    )
                    new_release_bounds[i] = bound_db
                    if signed_real[i] > outlier_cutoff_db:
                        new_unclamped_outliers += 1
                        continue
                    violation_db = signed_real[i] - bound_db
                    if violation_db <= 0.0:
                        continue
                    violation_scale = np.clip(
                        violation_db / max(bound_db, 1.0e-3), 1.0, 4.0)
                    new_release_w[i] = W_base[i] * irls_w[i] * (3.0 + 2.5 * violation_scale)
                    new_release_targets[i] = Y[i] - bound_db
                    new_constrained_peaks += 1
            else:
                # Legacy heuristic mode: gently boost real peaks only when the
                # smooth curve sags just below them. Large residuals are left to
                # Huber as likely tracking glitches rather than treated as
                # envelope constraints.
                release_db = 0.35
                peak_room = np.clip((signed_real - release_db) / max(sigma_hat, 1.0e-9),
                                    0.0, 2.5)
                peak_room = np.where(signed_real <= 4.0, peak_room, 0.0)
                new_release_w[:n_real] = W_base[:n_real] * irls_w[:n_real] * 0.45 * peak_room
                new_release_targets[:n_real] = Y[:n_real]
                new_release_bounds[:n_real] = release_db
                new_constrained_peaks = int(np.count_nonzero(new_release_w[:n_real] > 0.0))
                new_unclamped_outliers = int(np.count_nonzero(signed_real > 4.0))
                outlier_cutoff_db = 4.0

        release_delta = float(np.max(np.abs(new_release_w - release_w)))
        release_w = new_release_w
        release_targets = new_release_targets
        release_bounds = new_release_bounds
        constrained_peaks = new_constrained_peaks
        unclamped_outliers = new_unclamped_outliers

        release_violation = 0.0
        if n_virtual > 0 and np.any(release_bounds[:n_real] > 0.0):
            eligible_mask = signed_real <= outlier_cutoff_db
            release_violation = float(np.max(np.where(
                eligible_mask,
                np.maximum(signed_real - release_bounds[:n_real], 0.0),
                0.0,
            )))

        if (p90_residual <= 1.25 * sigma_hat and max_residual <= 2.75 * sigma_hat
                and release_delta < 0.005
                and (true_envelope_release_mode != "bounded" or release_violation < 0.02)):
            break
        huber_delta = 1.345 * sigma_hat
        new_irls_w = np.where(residuals <= huber_delta, 1.0,
                               huber_delta / np.maximum(residuals, 1.0e-9))

        # Early exit: if Huber weights have converged (max change < 0.5%),
        # another iteration would produce an identical solution.  Stop here to
        # avoid the slight over-penalisation of clean-frame partials that the
        # final iteration causes when the delta is already tight.
        if float(np.max(np.abs(new_irls_w - irls_w))) < 0.005:
            irls_w = new_irls_w
            break
        irls_w = new_irls_w

    # Condition estimate.
    try:
        eigvals = np.linalg.eigvalsh(A)
        cond = float(eigvals[-1] / max(eigvals[0], 1.0e-30))
    except np.linalg.LinAlgError:
        cond = float("inf")

    # Posterior uncertainty: A⁻¹ so callers can compute σ²·x(f)ᵀA⁻¹x(f).
    # noise_variance is computed from real-partial residuals only so virtual
    # points (near-zero residual by construction) do not deflate uncertainty.
    try:
        hessian_inv    = np.linalg.inv(A)
        noise_variance = w_err_real ** 2
    except np.linalg.LinAlgError:
        hessian_inv    = None
        noise_variance = 0.0

    final_signed_real = (Y - (B @ coefficients))[:n_real]
    final_protected_undershoot = [
        max(0.0, final_signed_real[i] - release_bounds[i])
        for i in range(n_real)
        if release_bounds[i] > 0.0 and final_signed_real[i] <= max(4.0, 2.75 * final_sigma_hat + 0.5)
    ]
    release_details = {
        "mode": true_envelope_release_mode,
        "release_bound_db": float(np.max(release_bounds[:n_real])) if n_real > 0 else 0.0,
        "release_bound_mean_db": float(np.mean(release_bounds[:n_real])) if n_real > 0 else 0.0,
        "constrained_peak_count": constrained_peaks,
        "unclamped_outlier_count": unclamped_outliers,
        "max_release_violation_db": max(final_protected_undershoot) if final_protected_undershoot else 0.0,
        "p95_release_violation_db": float(np.percentile(final_protected_undershoot, 95))
        if final_protected_undershoot else 0.0,
        "sigma_hat_db": final_sigma_hat,
        "outlier_cutoff_db": outlier_cutoff_db,
    }

    return _EnvelopeModel(
        storage="coefficients", values=coefficients, order=order,
        min_hz=min_hz, max_hz=max_hz, regularization=regularization,
        weighted_error_db=w_err_real, condition_estimate=cond,
        fit_points=len(points), fallback=None,
        hessian_inv=hessian_inv, noise_variance=noise_variance,
        axis=axis,
        solve_status=solve_status,
        release_details=release_details,
    )


def _append_virtual_gap(
    points: list,
    prev_model: _EnvelopeModel,
    weight_scale: float,
    min_gap: float,
    max_count: int,
    axis: str = "bark",
) -> list:
    """Cross-frame gap fill with low-weight virtual points (Tier 3.8).

    weight_scale=0.0 suppresses injection entirely (used by the p75 delta gate
    on onset/transient frames where the previous-frame envelope is stale).

    Improvement over the original uniform-grid approach: virtual points are now
    placed at the centres of the *actual* Bark-space gaps (including the leading
    and trailing boundary regions), sorted by gap size so the most under-supported
    regions are filled first.  Each virtual point's weight is scaled proportionally
    to its gap size, so large gaps (where the previous frame provides the only
    constraint) carry slightly more influence than small gaps.
    """
    if prev_model is None or max_count <= 0 or not points or weight_scale <= 0.0:
        return points

    current_positions = sorted(
        _normalized_position_axis(p["frequency_hz"], prev_model.min_hz, prev_model.max_hz, axis)
        for p in points
    )

    mean_weight = sum(p["weight"] for p in points) / len(points)

    # Identify all Bark-space gaps, including leading [0, first] and
    # trailing [last, 1] boundary gaps.
    boundaries = [0.0] + list(current_positions) + [1.0]
    gaps: list[tuple[float, float, float]] = []   # (size, lo_pos, hi_pos)
    for i in range(len(boundaries) - 1):
        lo_pos, hi_pos = boundaries[i], boundaries[i + 1]
        gap_size = hi_pos - lo_pos
        if gap_size >= min_gap:
            gaps.append((gap_size, lo_pos, hi_pos))

    if not gaps:
        return points

    # Sort largest-gap-first and allocate virtual slots proportionally.
    gaps.sort(reverse=True)
    total_gap = sum(g[0] for g in gaps)

    # Build candidates: one (or more) points per gap, centred within the gap.
    # For each gap, n_pts ∝ gap_size; at minimum 1 per eligible gap.
    candidates: list[tuple[float, float, dict]] = []   # (gap_size, pos, point_dict)
    remaining = max_count
    for gap_size, lo_pos, hi_pos in gaps:
        if remaining <= 0:
            break
        # Proportional allocation (at least 1 per gap).
        n_pts = max(1, round(gap_size / total_gap * max_count))
        n_pts = min(n_pts, remaining)
        if n_pts == 1:
            positions_in_gap = [(lo_pos + hi_pos) * 0.5]
        else:
            step = (hi_pos - lo_pos) / (n_pts + 1)
            positions_in_gap = [lo_pos + (j + 1) * step for j in range(n_pts)]
        # All virtual points share the same base weight so the overall virtual
        # influence on the solve stays comparable to the original implementation.
        # (Scaling by gap size shrinks weights in large gaps where they are
        # needed most — counterproductive.)
        # Boundary gaps: place the exact boundary endpoint first so the cosine
        # basis is pinned at x=0 or x=1, giving the strongest extrapolation
        # constraint.  Extra points (when n_pts > 1) are placed interior.
        is_leading  = (lo_pos == 0.0)
        is_trailing = (hi_pos == 1.0)
        if n_pts == 1:
            if is_leading:
                positions_in_gap = [0.0]
            elif is_trailing:
                positions_in_gap = [1.0]
            else:
                positions_in_gap = [(lo_pos + hi_pos) * 0.5]
        else:
            step = (hi_pos - lo_pos) / (n_pts + 1)
            interior = [lo_pos + (j + 1) * step for j in range(n_pts - 1)]
            if is_leading:
                positions_in_gap = [0.0] + interior
            elif is_trailing:
                positions_in_gap = interior + [1.0]
            else:
                positions_in_gap = interior + [(lo_pos + hi_pos) * 0.5]

        gap_weight = max(1.0e-6, mean_weight * weight_scale)
        for pos in positions_in_gap:
            fhz = _frequency_for_position(pos, prev_model.min_hz, prev_model.max_hz, axis)
            db  = _evaluate_new_model(prev_model, fhz)
            candidates.append((gap_size, pos, {
                "frequency_hz":     fhz,
                "log_amplitude_db": db,
                "weight":           gap_weight,
            }))
        remaining -= n_pts

    # Insert candidates (largest gap first), respecting the minimum-spacing guard.
    augmented = list(points)
    taken     = list(current_positions)
    for _, pos, sp in sorted(candidates, key=lambda x: -x[0]):
        if len(augmented) >= len(points) + max_count:
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
    axis: str = "bark",
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
    penalty = _build_curvature_penalty_for_axis(order, min_hz, max_hz, axis)

    B = np.zeros((len(points), n), dtype=np.float64)
    W = np.zeros(len(points),     dtype=np.float64)
    Y = np.zeros(len(points),     dtype=np.float64)
    for i, pt in enumerate(points):
        B[i, :] = _basis_row_ndarray(
            _normalized_position_axis(pt["frequency_hz"], min_hz, max_hz, axis), order)
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
    coarse = np.logspace(math.log10(max(lambda_min, 1.0e-12)),
                         math.log10(max(lambda_max, lambda_min * 10.0)),
                         max(2, n_candidates))
    best_index = min(range(len(coarse)), key=lambda i: abs(float(coarse[i]) - best_lam))
    left_index = max(0, best_index - 1)
    right_index = min(len(coarse) - 1, best_index + 1)
    refine_lo = float(coarse[left_index])
    refine_hi = float(coarse[right_index])
    if refine_hi > refine_lo:
        refine_lam, refine_gcv = _search_range(refine_lo, refine_hi)
        if refine_gcv < best_gcv:
            best_lam = refine_lam
            best_gcv = refine_gcv
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


def _axis_coverage(points: list, min_hz: float, max_hz: float, axis: str) -> float:
    if not points:
        return 0.0
    positions = [
        _normalized_position_axis(p["frequency_hz"], min_hz, max_hz, axis)
        for p in points
    ]
    return max(positions) - min(positions) if positions else 0.0


def _log_gap_cv(points: list) -> float:
    freqs = sorted(p["frequency_hz"] for p in points)
    if len(freqs) < 4:
        return 0.0
    gaps = [math.log(freqs[i + 1]) - math.log(freqs[i]) for i in range(len(freqs) - 1)]
    mean_gap = sum(gaps) / len(gaps)
    if mean_gap <= 1.0e-9:
        return 0.0
    variance = sum((gap - mean_gap) ** 2 for gap in gaps) / len(gaps)
    return math.sqrt(max(0.0, variance)) / mean_gap


def _adaptive_regularization(
    base_reg: float,
    coverage: float,
    n_real: int,
    n_virtual: int,
    axis: str,
) -> float:
    reg = max(base_reg, 1.0e-5)

    if n_real <= 8:
        sparse_scale = 0.20 + 0.80 * min(max(coverage, 0.0) / 0.60, 1.0)
        reg *= sparse_scale

    if n_virtual > 0:
        reg *= 0.30 if coverage < 0.45 else 0.55

    if n_real >= 16 and coverage > 0.65:
        dense_scale = 1.0 + 14.0 * ((coverage - 0.65) / 0.35) ** 2 * min(n_real / 32.0, 1.0)
        reg *= dense_scale

    if axis == "linear" and n_real <= 10 and n_virtual == 0:
        reg *= 0.85
        if coverage > 0.70:
            reg *= 0.05
    elif axis == "mel" and coverage > 0.55:
        reg *= 0.90
    elif axis == "bark" and n_virtual > 0:
        reg *= 0.85

    return min(max(reg, 1.0e-5), 1.0e-1)


def _fit_axis_model(
    usable: list,
    min_hz: float,
    max_hz: float,
    order: int,
    regularization: float,
    previous_model,
    grid_size: int,
    use_gcv: bool,
    axis: str,
    true_envelope_release_mode: str,
) -> tuple[list[float], list[float], dict, _EnvelopeModel]:
    import numpy as np

    eff_order = order
    fallback = None
    if len(usable) <= eff_order:
        eff_order = max(0, len(usable) - 1)
        fallback = "order_reduced_sparse"

    virtual_weight_scale = 0.05
    p75_delta_db = 0.0
    if previous_model is not None and usable:
        deltas = sorted(
            abs(p["log_amplitude_db"] - _evaluate_new_model(previous_model, p["frequency_hz"]))
            for p in usable
        )
        p75_delta_db = deltas[int(0.75 * (len(deltas) - 1))]
        if p75_delta_db > 12.0:
            virtual_weight_scale = 0.0

    augmented = _append_virtual_gap(usable, previous_model, virtual_weight_scale, 0.1, 6, axis=axis)
    n_real = len(usable)
    n_virtual = max(0, len(augmented) - n_real)
    coverage = _axis_coverage(usable, min_hz, max_hz, axis)
    if axis in ("linear", "mel") and n_virtual == 0 and n_real <= 8 and coverage > 0.70:
        eff_order = min(eff_order, max(1, n_real - 2))
    eff_reg = _adaptive_regularization(regularization, coverage, n_real, n_virtual, axis)

    if eff_order > 1:
        density_floor = max(1, math.ceil(n_real * 0.15))
        coverage_floor = 3 if coverage < 0.40 else density_floor
        min_bic_order = max(density_floor, coverage_floor)
        best_order, best_bic = eff_order, float("inf")
        for trial_order in range(min_bic_order, eff_order + 1):
            t = _solve_new_attempt(
                augmented, trial_order, eff_reg, 1.0e-10, min_hz, max_hz,
                irls_max_iterations=1, n_real_points=n_real, axis=axis,
                true_envelope_release_mode=true_envelope_release_mode)
            if t is None or t.condition_estimate > 1.0e7:
                continue
            n_bic = max(n_real, 1)
            bic = n_bic * math.log(max(t.weighted_error_db ** 2, 1.0e-30))
            bic += (trial_order + 1) * math.log(n_bic)
            if bic < best_bic:
                best_bic = bic
                best_order = trial_order
        eff_order = best_order

    gcv_lambda = None
    if use_gcv:
        gcv_lambda = _gcv_lambda_search(
            augmented, eff_order, min_hz, max_hz,
            lambda_min=1.0e-5, lambda_max=1.0e+2,
            n_candidates=10, ridge=1.0e-10, axis=axis)
        eff_reg = gcv_lambda

    result = _solve_new_attempt(
        augmented, eff_order, eff_reg, 1.0e-10, min_hz, max_hz,
        irls_max_iterations=4, n_real_points=n_real, axis=axis,
        true_envelope_release_mode=true_envelope_release_mode)
    if result is None or result.condition_estimate > 1.0e7:
        retry_reg = min(max(eff_reg * 10.0, 1.0e-4), 1.0)
        retry = _solve_new_attempt(
            augmented, eff_order, retry_reg, 1.0e-10, min_hz, max_hz,
            irls_max_iterations=4, n_real_points=n_real, axis=axis,
            true_envelope_release_mode=true_envelope_release_mode)
        if retry is not None:
            retry.fallback = "ill_conditioned_retry"
            result = retry
        else:
            w_sum = max(sum(p["weight"] for p in augmented), 1.0e-30)
            mean_db = sum(p["weight"] * p["log_amplitude_db"] for p in augmented) / w_sum
            result = _EnvelopeModel(
                storage="samples", values=np.array([mean_db]), order=0,
                min_hz=min_hz, max_hz=max_hz, regularization=0.0,
                weighted_error_db=0.0, condition_estimate=float("inf"),
                fit_points=len(augmented), fallback="constant_fallback",
                axis=axis,
                solve_status="constant_fallback",
                release_details={
                    "mode": true_envelope_release_mode,
                    "release_bound_db": 0.0,
                    "release_bound_mean_db": 0.0,
                    "constrained_peak_count": 0,
                    "unclamped_outlier_count": 0,
                    "max_release_violation_db": 0.0,
                    "p95_release_violation_db": 0.0,
                    "sigma_hat_db": 0.0,
                    "outlier_cutoff_db": 0.0,
                },
            )

    if fallback and result.fallback is None:
        result.fallback = fallback

    grid_freqs = np.exp(np.linspace(math.log(min_hz), math.log(max_hz), grid_size)).tolist()
    grid_db = [_evaluate_new_model(result, f) for f in grid_freqs]
    sigma_grid = [_evaluate_prediction_sigma(result, f) for f in grid_freqs]
    diag = {
        "axis": axis,
        "fit_points": len(augmented),
        "real_fit_points": n_real,
        "effective_order": result.order,
        "weighted_error_db": result.weighted_error_db,
        "condition_estimate": result.condition_estimate,
        "fallback": result.fallback,
        "virtual_points": n_virtual,
        "gcv_lambda": gcv_lambda,
        "gcv_was_used": use_gcv,
        "p75_delta_db": p75_delta_db,
        "virtual_suppressed": virtual_weight_scale == 0.0,
        "noise_variance_db2": result.noise_variance,
        "has_uncertainty": result.hessian_inv is not None,
        "predict_sigma_grid": sigma_grid,
        "coverage": coverage,
        "adaptive_regularization": eff_reg,
        "solve_status": result.solve_status,
        "true_envelope_release_mode": true_envelope_release_mode,
    }
    if result.release_details:
        diag.update(result.release_details)
    return grid_freqs, grid_db, diag, result


def _select_or_blend_axis_models(
    axis_results: dict[str, tuple[list[float], list[float], dict, _EnvelopeModel]],
    usable: list,
    min_hz: float,
    max_hz: float,
) -> tuple[list[float], list[float], dict, _EnvelopeModel]:
    import numpy as np

    bark_coverage = _axis_coverage(usable, min_hz, max_hz, "bark")
    span_octaves = math.log(max(usable[-1]["frequency_hz"], usable[0]["frequency_hz"]) /
                            min(usable[-1]["frequency_hz"], usable[0]["frequency_hz"]), 2.0) if len(usable) > 1 else 0.0
    gap_cv = _log_gap_cv(usable)
    n_real = len(usable)
    virtual_points = axis_results["bark"][2]["virtual_points"] if "bark" in axis_results else 0

    if n_real >= 24:
        priors = {"linear": 0.70, "mel": 0.25, "bark": 0.05}
    elif virtual_points > 0 or span_octaves < 0.8:
        priors = {"linear": 0.55, "mel": 0.35, "bark": 0.10}
    elif n_real <= 8 and span_octaves > 3.2:
        priors = {"linear": 0.60, "mel": 0.35, "bark": 0.05}
    elif n_real <= 8:
        priors = {"linear": 0.25, "mel": 0.65, "bark": 0.10}
    else:
        priors = {"linear": 0.50, "mel": 0.40, "bark": 0.10}

    if gap_cv > 0.45 and bark_coverage > 0.50:
        priors["linear"] += 0.10
        priors["mel"] += 0.05
    elif gap_cv < 0.15 and bark_coverage < 0.40:
        priors["bark"] += 0.05

    scores: dict[str, float] = {}
    for axis, (_, _, diag, model) in axis_results.items():
        err = max(diag["weighted_error_db"], 1.0e-3)
        complexity = 1.0 + 0.15 * max(model.order, 0)
        condition_penalty = 1.0 + 0.02 * math.log10(max(diag["condition_estimate"], 1.0))
        scores[axis] = priors[axis] / (err * complexity * condition_penalty)

    ranked = sorted(scores.items(), key=lambda item: item[1], reverse=True)
    top_axis, top_score = ranked[0]
    second_axis, second_score = ranked[1]
    top_result = axis_results[top_axis]

    score_sum = sum(scores.values())
    normalized = {axis: score / max(score_sum, 1.0e-9) for axis, score in scores.items()}

    if top_score >= second_score * 1.60 and normalized[top_axis] >= 0.60:
        grid_hz, grid_db, diag, model = top_result
        diag = dict(diag)
        diag["selected_axis"] = top_axis
        diag["selection_mode"] = "axis_select"
        diag["axis_scores"] = scores
        diag["axis_weights"] = normalized
        return grid_hz, grid_db, diag, model

    components = [
        (weight, axis_results[axis][3])
        for axis, weight in sorted(normalized.items(), key=lambda item: item[1], reverse=True)
        if weight >= 0.12
    ]
    if len(components) < 2:
        components = [
            (normalized[top_axis], top_result[3]),
            (normalized[second_axis], axis_results[second_axis][3]),
        ]
    weight_sum = sum(weight for weight, _ in components)
    components = [(weight / max(weight_sum, 1.0e-9), model) for weight, model in components]
    blend_model = _EnvelopeModel(
        storage="blend", values=np.array([], dtype=np.float64), order=max(
            component.order for _, component in components),
        min_hz=min_hz, max_hz=max_hz, regularization=0.0,
        weighted_error_db=sum(
            weight * component.weighted_error_db for weight, component in components),
        condition_estimate=min(component.condition_estimate for _, component in components),
        fit_points=top_result[3].fit_points,
        fallback=top_result[3].fallback,
        noise_variance=sum(weight * component.noise_variance for weight, component in components),
        axis="blend",
        blend_components=components,
    )
    grid_hz = top_result[0]
    grid_db = [
        sum(weight * _evaluate_new_model(component, grid_hz[i]) for weight, component in components)
        for i in range(len(grid_hz))
    ]
    sigma_grid = [_evaluate_prediction_sigma(blend_model, f) for f in grid_hz]
    diag = dict(top_result[2])
    diag["selected_axis"] = top_axis
    diag["selection_mode"] = "axis_blend"
    diag["blend_axes"] = [(component.axis, weight) for weight, component in components]
    diag["axis_scores"] = scores
    diag["axis_weights"] = normalized
    diag["weighted_error_db"] = blend_model.weighted_error_db
    diag["has_uncertainty"] = any(math.isfinite(s) for s in sigma_grid)
    diag["predict_sigma_grid"] = sigma_grid
    return grid_hz, grid_db, diag, blend_model


def _fit_global_model(
    frame_partials: list,
    sample_rate: float,
    lowest_freq_hz: float,
    highest_freq_hz: float,
    order: int,
    regularization: float,
    previous_model,
    grid_size: int,
    use_gcv: bool,
    true_envelope_release_mode: str,
) -> tuple:
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

    usable = sorted(usable, key=lambda p: p["frequency_hz"])
    axis_results = {
        axis: _fit_axis_model(
            usable, min_hz, max_hz, order, regularization,
            previous_model, grid_size, use_gcv, axis, true_envelope_release_mode)
        for axis in ("linear", "mel", "bark")
    }
    return _select_or_blend_axis_models(axis_results, usable, min_hz, max_hz)


def _make_subband_layout(count: int, overlap: float) -> list[dict]:
    count = max(2, count)
    overlap = max(0.0, min(0.45, overlap))
    half_overlap = overlap * 0.5
    labels = ["low", "mid", "high"] if count == 3 else [f"band_{idx + 1}" for idx in range(count)]
    layout = []
    for idx in range(count):
        core_lo = idx / float(count)
        core_hi = (idx + 1) / float(count)
        layout.append({
            "label": labels[idx],
            "lo": max(0.0, core_lo - half_overlap),
            "core_lo": core_lo,
            "core_hi": core_hi,
            "hi": min(1.0, core_hi + half_overlap),
        })
    return layout


def _subband_window(position: float, band: dict) -> float:
    lo = band["lo"]
    core_lo = band["core_lo"]
    core_hi = band["core_hi"]
    hi = band["hi"]
    if position < lo or position > hi:
        return 0.0
    if position <= core_lo:
        denom = max(core_lo - lo, 1.0e-9)
        t = max(0.0, min(1.0, (position - lo) / denom))
        return 0.5 - 0.5 * math.cos(math.pi * t)
    if position >= core_hi:
        denom = max(hi - core_hi, 1.0e-9)
        t = max(0.0, min(1.0, (hi - position) / denom))
        return 0.5 - 0.5 * math.cos(math.pi * t)
    return 1.0


def _build_subband_points(
    usable: list,
    band: dict,
    min_hz: float,
    max_hz: float,
    axis: str,
) -> tuple[list, int]:
    band_points = []
    support_count = 0
    for point in usable:
        position = _normalized_position_axis(point["frequency_hz"], min_hz, max_hz, axis)
        window = _subband_window(position, band)
        if window <= 0.0:
            continue
        if window >= 0.20:
            support_count += 1
        weighted_point = dict(point)
        weighted_point["weight"] = max(1.0e-4, point["weight"] * window)
        band_points.append(weighted_point)
    return band_points, support_count


def _subband_mean_sigma_db(global_model: _EnvelopeModel, band_points: list) -> float:
    weighted = []
    for point in band_points:
        sigma_db = _evaluate_prediction_sigma(global_model, point["frequency_hz"])
        if math.isfinite(sigma_db) and sigma_db > 0.0:
            weighted.append((point["weight"], sigma_db))
    if weighted:
        w_sum = max(sum(weight for weight, _ in weighted), 1.0e-9)
        return sum(weight * sigma for weight, sigma in weighted) / w_sum
    return max(math.sqrt(max(global_model.noise_variance, 0.0)), 0.35)


def _select_subband_order(
    base_order: int,
    support_count: int,
    density_ratio: float,
    mean_sigma_db: float,
) -> int:
    if support_count <= 1:
        return 0
    max_supported = max(1, support_count - 1)
    density_ratio = max(0.0, min(1.0, density_ratio))
    sigma_penalty = min(0.55, mean_sigma_db / 3.0)
    target_order = 2 + round((max(base_order, 2) - 2) * max(0.15, density_ratio) * (1.0 - sigma_penalty))
    return max(1, min(base_order, max_supported, target_order))


def _subband_regularization(
    base_reg: float,
    density_ratio: float,
    mean_sigma_db: float,
    support_count: int,
) -> float:
    density_ratio = max(0.0, min(1.0, density_ratio))
    sigma_ratio = min(1.5, mean_sigma_db / 0.8)
    sparsity = 1.0 - density_ratio
    reg = max(base_reg, 1.0e-5) * (0.55 + 1.35 * sparsity + 0.85 * sigma_ratio)
    if support_count <= 3:
        reg *= 1.15
    return min(max(reg, 1.0e-5), 5.0e-2)


def _blend_subband_value(frequency_hz: float, bands: list, axis: str, min_hz: float, max_hz: float) -> float:
    position = _normalized_position_axis(frequency_hz, min_hz, max_hz, axis)
    weighted_sum = 0.0
    weight_total = 0.0
    for band_entry in bands:
        window = _subband_window(position, band_entry["layout"])
        if window <= 0.0:
            continue
        weighted_sum += window * _evaluate_new_model(band_entry["model"], frequency_hz)
        weight_total += window
    if weight_total <= 1.0e-9:
        return _evaluate_new_model(bands[0]["model"], frequency_hz)
    return weighted_sum / weight_total


def _fit_subband_model(
    usable: list,
    min_hz: float,
    max_hz: float,
    order: int,
    regularization: float,
    grid_size: int,
    use_gcv: bool,
    true_envelope_release_mode: str,
    global_diag: dict,
    global_model: _EnvelopeModel,
    options: SubbandAdaptiveOptions,
) -> tuple[list[float], list[float], dict, _EnvelopeModel] | None:
    import numpy as np

    axis = options.axis if options.axis in ("linear", "mel", "bark") else "bark"
    layout = _make_subband_layout(options.band_count, options.overlap)
    sample_grid_size = max(grid_size, max(32, options.sample_grid_size))
    sample_grid_hz = _make_bark_grid(min_hz, max_hz, sample_grid_size)

    band_entries = []
    band_diags = []
    for band in layout:
        band_points, support_count = _build_subband_points(usable, band, min_hz, max_hz, axis)
        if support_count < max(1, options.min_band_points):
            return None

        band_width = max(band["core_hi"] - band["core_lo"], 1.0e-6)
        band_min_hz = _frequency_for_position(band["lo"], min_hz, max_hz, axis)
        band_max_hz = _frequency_for_position(band["hi"], min_hz, max_hz, axis)
        density_ratio = min(1.0, support_count / max(5.0, 15.0 * band_width))
        mean_sigma_db = _subband_mean_sigma_db(global_model, band_points)
        local_order = _select_subband_order(order, support_count, density_ratio, mean_sigma_db)
        local_reg = _subband_regularization(regularization, density_ratio, mean_sigma_db, support_count)

        _, _, band_diag, band_model = _fit_axis_model(
            band_points,
            band_min_hz,
            band_max_hz,
            local_order,
            local_reg,
            None,
            sample_grid_size,
            use_gcv,
            axis,
            true_envelope_release_mode,
        )

        band_sigma_grid = []
        for freq_hz in sample_grid_hz:
            sigma_db = _evaluate_prediction_sigma(band_model, freq_hz)
            if not math.isfinite(sigma_db) or sigma_db <= 0.0:
                sigma_db = max(math.sqrt(max(band_model.noise_variance, 0.0)), 0.25)
            band_sigma_grid.append(sigma_db)

        confidence = 1.0 / (1.0 + mean_sigma_db + band_diag["weighted_error_db"])
        band_entries.append({
            "layout": band,
            "model": band_model,
            "sigma_grid": band_sigma_grid,
        })
        band_diags.append({
            "label": band["label"],
            "point_count": support_count,
            "effective_order": band_model.order,
            "regularization": local_reg,
            "uncertainty_db": mean_sigma_db,
            "confidence": max(0.0, min(1.0, confidence)),
            "weighted_error_db": band_diag["weighted_error_db"],
            "condition_estimate": band_diag["condition_estimate"],
            "solve_status": band_diag["solve_status"],
            "coverage": band_diag["coverage"],
        })

    blended_values = []
    blended_sigma = []
    for sample_index, freq_hz in enumerate(sample_grid_hz):
        position = _normalized_position_axis(freq_hz, min_hz, max_hz, axis)
        contributions = []
        for band_entry in band_entries:
            window = _subband_window(position, band_entry["layout"])
            if window <= 0.0:
                continue
            contributions.append((window, band_entry))
        if not contributions:
            blended_values.append(_evaluate_new_model(global_model, freq_hz))
            blended_sigma.append(max(math.sqrt(max(global_model.noise_variance, 0.0)), 0.35))
            continue
        weight_total = sum(weight for weight, _ in contributions)
        normalized = [(weight / max(weight_total, 1.0e-9), band_entry) for weight, band_entry in contributions]
        blended_values.append(sum(
            weight * _evaluate_new_model(band_entry["model"], freq_hz)
            for weight, band_entry in normalized
        ))
        blended_sigma.append(math.sqrt(sum(
            (weight * band_entry["sigma_grid"][sample_index]) ** 2
            for weight, band_entry in normalized
        )))

    seam_pairs = []
    for band_index in range(len(band_entries) - 1):
        left = band_entries[band_index]
        right = band_entries[band_index + 1]
        overlap_lo = max(left["layout"]["lo"], right["layout"]["lo"])
        overlap_hi = min(left["layout"]["hi"], right["layout"]["hi"])
        overlap_freqs = [
            freq_hz
            for freq_hz in sample_grid_hz
            if overlap_lo <= _normalized_position_axis(freq_hz, min_hz, max_hz, axis) <= overlap_hi
        ]
        if not overlap_freqs:
            seam_pairs.append({"pair": f"{left['layout']['label']}->{right['layout']['label']}", "rms_db": 0.0, "max_db": 0.0})
            continue
        diffs = [
            _evaluate_new_model(left["model"], freq_hz) - _evaluate_new_model(right["model"], freq_hz)
            for freq_hz in overlap_freqs
        ]
        seam_pairs.append({
            "pair": f"{left['layout']['label']}->{right['layout']['label']}",
            "rms_db": math.sqrt(sum(diff * diff for diff in diffs) / max(len(diffs), 1)),
            "max_db": max(abs(diff) for diff in diffs),
        })

    seam_rms = max((pair["rms_db"] for pair in seam_pairs), default=0.0)
    seam_max = max((pair["max_db"] for pair in seam_pairs), default=0.0)
    mean_confidence = sum(band["confidence"] for band in band_diags) / max(len(band_diags), 1)
    global_anchor_weight = min(
        0.70,
        max(
            0.05,
            0.08
            + 0.28 * (1.0 - mean_confidence)
            + 0.18 * max(0.0, seam_rms / max(options.seam_rms_threshold_db, 1.0e-6) - 1.0)
            + 0.12 * max(0.0, seam_max / max(options.seam_max_threshold_db, 1.0e-6) - 1.0),
        ),
    )
    for sample_index, freq_hz in enumerate(sample_grid_hz):
        global_value = _evaluate_new_model(global_model, freq_hz)
        global_sigma = max(math.sqrt(max(global_model.noise_variance, 0.0)), 0.35)
        blended_values[sample_index] = (
            (1.0 - global_anchor_weight) * blended_values[sample_index]
            + global_anchor_weight * global_value
        )
        blended_sigma[sample_index] = math.sqrt(
            ((1.0 - global_anchor_weight) * blended_sigma[sample_index]) ** 2
            + (global_anchor_weight * global_sigma) ** 2
        )

    blended_variance = [sigma_db * sigma_db for sigma_db in blended_sigma]
    blended_model = _build_sampled_model(
        blended_values,
        blended_variance,
        min_hz,
        max_hz,
        global_model,
        len(usable),
        0.0,
        "subband_adaptive",
    )
    blended_weighted_error = math.sqrt(sum(
        point["weight"] * (point["log_amplitude_db"] - _evaluate_new_model(blended_model, point["frequency_hz"])) ** 2
        for point in usable
    ) / max(sum(point["weight"] for point in usable), 1.0e-9))
    blended_model.weighted_error_db = blended_weighted_error

    output_grid_hz = list(sample_grid_hz)
    output_grid_db = [_evaluate_new_model(blended_model, freq_hz) for freq_hz in output_grid_hz]
    diag = dict(global_diag)
    diag.update({
        "axis": axis,
        "selected_axis": f"subband_{axis}",
        "selection_mode": "subband_overlap_add",
        "fit_points": len(usable),
        "real_fit_points": len(usable),
        "weighted_error_db": blended_weighted_error,
        "condition_estimate": max(band["condition_estimate"] for band in band_diags),
        "fallback": None,
        "virtual_points": 0,
        "gcv_lambda": None,
        "gcv_was_used": use_gcv,
        "noise_variance_db2": float(sum(blended_variance) / max(len(blended_variance), 1)),
        "has_uncertainty": True,
        "predict_sigma_grid": list(blended_sigma),
        "subband_enabled": True,
        "subband_applied": True,
        "subband_band_count": len(layout),
        "subband_axis": axis,
        "subband_global_weighted_error_db": global_diag["weighted_error_db"],
        "subband_global_anchor_weight": global_anchor_weight,
        "band_seam_rms_db": seam_rms,
        "band_seam_max_db": seam_max,
        "band_seam_pairs": seam_pairs,
        "band_seam_rms_threshold_db": options.seam_rms_threshold_db,
        "band_seam_max_threshold_db": options.seam_max_threshold_db,
        "subbands": band_diags,
        "solve_status": "subband_adaptive",
    })
    return output_grid_hz, output_grid_db, diag, blended_model


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
    true_envelope_release_mode: str = "heuristic",
    subband_options: SubbandAdaptiveOptions | None = None,
    refinement_options: RefinementOptions | None = None,
) -> tuple:
    """Fit the new estimator with adaptive regularisation and optional sub-bands.

    Returns (grid_freqs_hz, grid_db, diagnostics_dict, _EnvelopeModel).
    """
    if subband_options is None:
        subband_options = SubbandAdaptiveOptions()
    if refinement_options is None:
        refinement_options = RefinementOptions()

    global_grid_hz, global_grid_db, global_diag, global_model = _fit_global_model(
        frame_partials,
        sample_rate,
        lowest_freq_hz,
        highest_freq_hz,
        order,
        regularization,
        previous_model,
        grid_size,
        use_gcv,
        true_envelope_release_mode,
    )
    selected_grid_hz = global_grid_hz
    selected_grid_db = global_grid_db
    selected_diag = global_diag
    selected_model = global_model

    min_hz = max(lowest_freq_hz, 1.0)
    max_hz = min(highest_freq_hz, sample_rate * 0.5)
    if not subband_options.enabled or not global_grid_hz:
        if refinement_options.enabled and selected_grid_hz:
            return _apply_optional_refinement(
                selected_grid_hz,
                selected_diag,
                selected_model,
                min_hz,
                max_hz,
                refinement_options,
            )
        return selected_grid_hz, selected_grid_db, selected_diag, selected_model

    usable = sorted(
        [
            point for point in _build_fit_points_new(frame_partials, min_hz, max_hz)
            if min_hz <= point["frequency_hz"] <= max_hz and point["weight"] > 0.0
        ],
        key=lambda point: point["frequency_hz"],
    )
    if not usable:
        if refinement_options.enabled and selected_grid_hz:
            return _apply_optional_refinement(
                selected_grid_hz,
                selected_diag,
                selected_model,
                min_hz,
                max_hz,
                refinement_options,
            )
        return selected_grid_hz, selected_grid_db, selected_diag, selected_model

    if len(usable) >= 24:
        fallback_diag = dict(global_diag)
        fallback_diag.update({
            "subband_enabled": True,
            "subband_applied": False,
            "subband_reason": "dense_global_noop",
        })
        selected_diag = fallback_diag
    else:
        subband_result = _fit_subband_model(
            usable,
            min_hz,
            max_hz,
            order,
            regularization,
            grid_size,
            use_gcv,
            true_envelope_release_mode,
            global_diag,
            global_model,
            subband_options,
        )
        if subband_result is None:
            fallback_diag = dict(global_diag)
            fallback_diag.update({
                "subband_enabled": True,
                "subband_applied": False,
                "subband_reason": "insufficient_band_support",
            })
            selected_diag = fallback_diag
        else:
            selected_grid_hz, selected_grid_db, selected_diag, selected_model = subband_result

    if refinement_options.enabled and selected_grid_hz:
        return _apply_optional_refinement(
            selected_grid_hz,
            selected_diag,
            selected_model,
            min_hz,
            max_hz,
            refinement_options,
        )
    return selected_grid_hz, selected_grid_db, selected_diag, selected_model


def _make_bark_grid(min_hz: float, max_hz: float, grid_size: int) -> list[float]:
    if grid_size <= 1:
        return [min_hz]
    return [
        _frequency_for_bark_position(i / float(grid_size - 1), min_hz, max_hz)
        for i in range(grid_size)
    ]


def _interpolate_curve(source_hz: list[float], source_db: list[float], query_hz: list[float]) -> list[float]:
    if not source_hz or not source_db or len(source_hz) != len(source_db):
        return []
    if len(source_hz) == 1:
        return [float(source_db[0]) for _ in query_hz]

    result = []
    for freq_hz in query_hz:
        if freq_hz <= source_hz[0]:
            result.append(float(source_db[0]))
            continue
        if freq_hz >= source_hz[-1]:
            result.append(float(source_db[-1]))
            continue
        hi = bisect.bisect_right(source_hz, freq_hz)
        lo = max(0, hi - 1)
        hi = min(hi, len(source_hz) - 1)
        span = max(source_hz[hi] - source_hz[lo], 1.0e-12)
        blend = (freq_hz - source_hz[lo]) / span
        result.append(float(source_db[lo] + blend * (source_db[hi] - source_db[lo])))
    return result


def _sample_model_on_grid(model: _EnvelopeModel, grid_freqs_hz: list[float]) -> tuple[list[float], list[float]]:
    values = [_evaluate_new_model(model, freq_hz) for freq_hz in grid_freqs_hz]
    sigma = []
    fallback_sigma = max(math.sqrt(max(model.noise_variance, 0.0)), 0.18)
    for freq_hz in grid_freqs_hz:
        pred_sigma = _evaluate_prediction_sigma(model, freq_hz)
        sigma.append(pred_sigma if math.isfinite(pred_sigma) and pred_sigma > 0.0 else fallback_sigma)
    return values, sigma


def _build_sampled_model(
    values_db: list[float],
    variance_db2: list[float],
    min_hz: float,
    max_hz: float,
    base_model: _EnvelopeModel,
    fit_points: int,
    weighted_error_db: float,
    solve_status: str,
) -> _EnvelopeModel:
    import numpy as np
    sampled_model = _EnvelopeModel(
        storage="samples",
        values=np.array(values_db, dtype=np.float64),
        order=0,
        min_hz=min_hz,
        max_hz=max_hz,
        regularization=base_model.regularization,
        weighted_error_db=weighted_error_db,
        condition_estimate=base_model.condition_estimate,
        fit_points=fit_points,
        fallback=base_model.fallback,
        hessian_inv=None,
        noise_variance=float(sum(variance_db2) / max(len(variance_db2), 1)),
        axis="bark",
        solve_status=solve_status,
        release_details=base_model.release_details,
    )
    return sampled_model


def _apply_optional_refinement(
    base_grid_hz: list[float],
    base_diag: dict,
    base_model: _EnvelopeModel,
    min_hz: float,
    max_hz: float,
    options: RefinementOptions,
) -> tuple[list[float], list[float], dict, _EnvelopeModel]:
    import time
    import numpy as np

    base_grid_db = [_evaluate_new_model(base_model, freq_hz) for freq_hz in base_grid_hz]
    diag = dict(base_diag)
    diag.update({
        "refinement_enabled": bool(options.enabled),
        "refinement_applied": False,
    })
    if not options.enabled:
        diag["refinement_reason"] = "disabled"
        return base_grid_hz, base_grid_db, diag, base_model
    if not options.dense_target_hz or not options.dense_target_db:
        diag["refinement_reason"] = "missing_dense_target"
        return base_grid_hz, base_grid_db, diag, base_model
    if len(options.dense_target_hz) != len(options.dense_target_db):
        diag["refinement_reason"] = "invalid_dense_target"
        return base_grid_hz, base_grid_db, diag, base_model

    bark_grid_hz = _make_bark_grid(min_hz, max_hz, max(8, options.bark_grid_size))
    base_values_db, base_sigma_db = _sample_model_on_grid(base_model, bark_grid_hz)
    target_values_db = _interpolate_curve(options.dense_target_hz, options.dense_target_db, bark_grid_hz)
    if not target_values_db:
        diag["refinement_reason"] = "target_interpolation_failed"
        return base_grid_hz, base_grid_db, diag, base_model

    base_values = np.array(base_values_db, dtype=np.float64)
    target_values = np.array(target_values_db, dtype=np.float64)
    sigma_values = np.array([max(0.18, sigma_db) for sigma_db in base_sigma_db], dtype=np.float64)
    sigma_ref = max(float(np.median(sigma_values)), 0.18)
    target_weights = 1.0 + options.uncertainty_gain * np.clip((sigma_values / sigma_ref) - 1.0, 0.0, 3.0)
    anchor_weights = options.anchor_strength / np.maximum(target_weights, 1.0e-6)

    def blur(values: np.ndarray) -> np.ndarray:
        if values.size <= 2:
            return values.copy()
        padded = np.pad(values, (1, 1), mode="edge")
        return 0.25 * padded[:-2] + 0.50 * padded[1:-1] + 0.25 * padded[2:]

    blurred_target = blur(target_values)

    def loss_and_gradient(values: np.ndarray) -> tuple[float, np.ndarray]:
        diff = values - target_values
        blurred_diff = blur(values) - blurred_target
        anchor_diff = values - base_values

        direct_loss = float(np.mean(target_weights * diff * diff))
        blurred_loss = float(np.mean(blurred_diff * blurred_diff))
        anchor_loss = float(np.mean(anchor_weights * anchor_diff * anchor_diff))
        total_loss = direct_loss + options.blur_weight * blurred_loss + anchor_loss

        norm = max(values.size, 1)
        grad_direct = (2.0 / norm) * target_weights * diff
        grad_blur = (2.0 * options.blur_weight / norm) * blur(blurred_diff)
        grad_anchor = (2.0 / norm) * anchor_weights * anchor_diff
        return total_loss, grad_direct + grad_blur + grad_anchor

    start_time = time.perf_counter()
    current_values = base_values.copy()
    current_loss, gradient = loss_and_gradient(current_values)
    loss_history = [current_loss]
    step_history: list[float] = []

    for _ in range(max(0, options.max_iterations)):
        step_size = max(options.initial_step_size, options.min_step_size)
        accepted = False
        while step_size >= options.min_step_size:
            delta = np.clip(step_size * gradient, -options.max_delta_db, options.max_delta_db)
            candidate_values = current_values - delta
            candidate_loss, candidate_grad = loss_and_gradient(candidate_values)
            if candidate_loss <= current_loss - 1.0e-9:
                current_values = candidate_values
                current_loss = candidate_loss
                gradient = candidate_grad
                loss_history.append(candidate_loss)
                step_history.append(step_size)
                accepted = True
                break
            step_size *= 0.5
        if not accepted:
            break

    runtime_ms = (time.perf_counter() - start_time) * 1000.0
    refined_values_db = [float(value) for value in current_values.tolist()]
    variance_db2 = [float(sigma_db * sigma_db) for sigma_db in sigma_values.tolist()]
    refined_model = _build_sampled_model(
        refined_values_db,
        variance_db2,
        min_hz,
        max_hz,
        base_model,
        base_model.fit_points,
        math.sqrt(max(current_loss, 0.0)),
        "analysis_refined" if len(loss_history) > 1 else base_model.solve_status,
    )
    output_grid_db = [_evaluate_new_model(refined_model, freq_hz) for freq_hz in base_grid_hz]
    diag.update({
        "refinement_enabled": True,
        "refinement_applied": len(loss_history) > 1,
        "refinement_reason": "dense_target_refinement" if len(loss_history) > 1 else "no_descent_step",
        "refinement_target_label": options.target_label,
        "refinement_target_points": len(options.dense_target_hz),
        "refinement_grid_size": len(bark_grid_hz),
        "refinement_iteration_count": max(0, len(loss_history) - 1),
        "refinement_loss_initial": loss_history[0],
        "refinement_loss_final": loss_history[-1],
        "refinement_loss_history": loss_history,
        "refinement_step_history": step_history,
        "refinement_monotonic": all(
            loss_history[i + 1] <= loss_history[i] + 1.0e-9
            for i in range(len(loss_history) - 1)
        ),
        "refinement_runtime_ms": runtime_ms,
        "refinement_uncertainty_median_db": sigma_ref,
    })
    return base_grid_hz, output_grid_db, diag, refined_model


def _smooth_temporal_envelope_frame(
    previous_state: _TemporalEnvelopeState | None,
    observation_values_db: list[float],
    observation_sigma_db: list[float],
    options: TemporalEnvelopeSmoothingOptions,
    transient_boundary: bool,
    motion_scale: float,
    residual_scale: float,
    frame_index: int,
    label: str,
) -> tuple[_TemporalEnvelopeState, dict]:
    import numpy as np

    obs_values = np.array(observation_values_db, dtype=np.float64)
    obs_sigma = np.array([
        max(options.observation_floor_db, sigma_db)
        for sigma_db in observation_sigma_db
    ], dtype=np.float64)
    obs_var = obs_sigma * obs_sigma

    if previous_state is None or not options.enabled:
        state = _TemporalEnvelopeState(
            grid_freqs_hz=list(previous_state.grid_freqs_hz) if previous_state else [],
            values_db=obs_values,
            variance_db2=obs_var,
            frame_index=frame_index,
            label=label,
        )
        diag = {
            "temporal_enabled": bool(options.enabled),
            "temporal_applied": False,
            "temporal_gain_mean": 1.0,
            "temporal_gain_min": 1.0,
            "temporal_gain_max": 1.0,
            "temporal_prior_observation_ratio": 0.0,
            "temporal_process_noise_db": options.process_noise_db,
            "temporal_process_scale": 1.0,
            "temporal_delta_rms_db": 0.0,
            "temporal_gate_reason": "initial_frame",
        }
        return state, diag

    if transient_boundary:
        state = _TemporalEnvelopeState(
            grid_freqs_hz=previous_state.grid_freqs_hz,
            values_db=obs_values,
            variance_db2=obs_var,
            frame_index=frame_index,
            label=label,
        )
        diag = {
            "temporal_enabled": True,
            "temporal_applied": True,
            "temporal_gain_mean": 1.0,
            "temporal_gain_min": 1.0,
            "temporal_gain_max": 1.0,
            "temporal_prior_observation_ratio": 0.0,
            "temporal_process_noise_db": options.process_noise_db * options.transient_process_scale,
            "temporal_process_scale": options.transient_process_scale,
            "temporal_delta_rms_db": float(np.sqrt(np.mean((obs_values - previous_state.values_db) ** 2))),
            "temporal_gate_reason": "transient_boundary_bypass",
        }
        return state, diag

    delta_rms = float(np.sqrt(np.mean((obs_values - previous_state.values_db) ** 2)))
    process_scale = 1.0
    gate_reasons: list[str] = []

    if transient_boundary:
        process_scale *= options.transient_process_scale
        gate_reasons.append("transient_boundary")

    if delta_rms > options.high_delta_db:
        excess = min((delta_rms - options.high_delta_db) / max(options.high_delta_db, 1.0e-6), 2.0)
        process_scale *= 1.0 + options.delta_process_scale * excess
        gate_reasons.append("high_delta")

    if motion_scale > 1.0:
        process_scale *= 1.0 + options.motion_process_scale * (motion_scale - 1.0)
        gate_reasons.append("motion_scale")

    if residual_scale > 1.0:
        process_scale *= 1.0 + options.residual_process_scale * (residual_scale - 1.0)
        gate_reasons.append("residual_scale")

    process_noise_db = options.process_noise_db * process_scale
    process_var = process_noise_db * process_noise_db
    prior_var = np.maximum(previous_state.variance_db2 + process_var, 1.0e-9)
    gain = np.clip(prior_var / (prior_var + obs_var), options.min_gain, options.max_gain)
    posterior_values = previous_state.values_db + gain * (obs_values - previous_state.values_db)
    posterior_var = np.maximum((1.0 - gain) * prior_var, 1.0e-9)

    state = _TemporalEnvelopeState(
        grid_freqs_hz=previous_state.grid_freqs_hz,
        values_db=posterior_values,
        variance_db2=posterior_var,
        frame_index=frame_index,
        label=label,
    )
    diag = {
        "temporal_enabled": True,
        "temporal_applied": True,
        "temporal_gain_mean": float(np.mean(gain)),
        "temporal_gain_min": float(np.min(gain)),
        "temporal_gain_max": float(np.max(gain)),
        "temporal_prior_observation_ratio": float(np.mean(prior_var / np.maximum(obs_var, 1.0e-9))),
        "temporal_process_noise_db": process_noise_db,
        "temporal_process_scale": process_scale,
        "temporal_delta_rms_db": delta_rms,
        "temporal_gate_reason": "+".join(gate_reasons) if gate_reasons else "steady",
    }
    return state, diag


def fit_new_model_sequence(
    frames: list[TemporalEnvelopeFrame],
    sample_rate: float,
    lowest_freq_hz: float,
    highest_freq_hz: float,
    order: int = 16,
    regularization: float = 1.0e-3,
    temporal_options: TemporalEnvelopeSmoothingOptions | None = None,
    use_gcv: bool = False,
    true_envelope_release_mode: str = "heuristic",
) -> list[tuple[list[float], list[float], dict, _EnvelopeModel]]:
    if temporal_options is None:
        temporal_options = TemporalEnvelopeSmoothingOptions()

    min_hz = max(lowest_freq_hz, 1.0)
    max_hz = min(highest_freq_hz, sample_rate * 0.5)
    bark_grid_hz = _make_bark_grid(min_hz, max_hz, max(2, temporal_options.bark_grid_size))

    results = []
    previous_fit_model = None
    previous_state = None

    for frame_index, frame in enumerate(frames):
        raw_grid_hz, raw_grid_db, raw_diag, raw_model = fit_new_model(
            frame.partials,
            sample_rate,
            lowest_freq_hz,
            highest_freq_hz,
            order=order,
            regularization=regularization,
            previous_model=previous_fit_model,
            grid_size=max(512, temporal_options.bark_grid_size),
            use_gcv=use_gcv,
            true_envelope_release_mode=true_envelope_release_mode,
        )
        previous_fit_model = raw_model

        if not raw_grid_hz:
            diag = dict(raw_diag)
            diag.update({
                "temporal_enabled": bool(temporal_options.enabled),
                "temporal_applied": False,
                "temporal_gate_reason": "no_observation",
            })
            results.append((raw_grid_hz, raw_grid_db, diag, raw_model))
            previous_state = None
            continue

        observation_values_db, observation_sigma_db = _sample_model_on_grid(raw_model, bark_grid_hz)
        state, temporal_diag = _smooth_temporal_envelope_frame(
            previous_state,
            observation_values_db,
            observation_sigma_db,
            temporal_options,
            transient_boundary=frame.transient_boundary,
            motion_scale=max(0.25, frame.motion_scale),
            residual_scale=max(0.25, frame.residual_scale),
            frame_index=frame_index,
            label=frame.label,
        )
        if not state.grid_freqs_hz:
            state.grid_freqs_hz = bark_grid_hz
        previous_state = state

        smoothed_grid_db = [float(value) for value in state.values_db.tolist()]
        smoothed_variance_db2 = [float(value) for value in state.variance_db2.tolist()]
        smoothed_weighted_error = math.sqrt(sum(
            (observation_values_db[i] - smoothed_grid_db[i]) ** 2 for i in range(len(smoothed_grid_db))
        ) / max(len(smoothed_grid_db), 1))
        smoothed_model = _build_sampled_model(
            smoothed_grid_db,
            smoothed_variance_db2,
            min_hz,
            max_hz,
            raw_model,
            raw_model.fit_points,
            smoothed_weighted_error,
            "temporal_smoothed" if temporal_diag["temporal_applied"] else raw_model.solve_status,
        )

        diag = dict(raw_diag)
        diag.update(temporal_diag)
        diag["temporal_raw_weighted_error_db"] = raw_model.weighted_error_db
        diag["temporal_smoothed_weighted_error_db"] = smoothed_model.weighted_error_db
        diag["temporal_variance_mean_db2"] = float(sum(smoothed_variance_db2) / max(len(smoothed_variance_db2), 1))
        diag["temporal_grid_size"] = len(smoothed_grid_db)
        results.append((bark_grid_hz, smoothed_grid_db, diag, smoothed_model))

    return results


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

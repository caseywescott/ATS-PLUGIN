#!/usr/bin/env python3
"""Shared fixtures and metrics for Phase 4 Task 4.3 sub-band research."""

from __future__ import annotations

import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from visualize_envelopes import (
    SubbandAdaptiveOptions,
    _evaluate_new_model,
    _normalized_position_bark,
    fit_new_model,
)

SR = 48000.0
LO = 80.0
HI = 8000.0
ORD = 16
REG = 1.0e-3
SUBBAND_OPTIONS = SubbandAdaptiveOptions(enabled=True)
EVAL_FREQS = [LO * (HI / LO) ** (i / 79.0) for i in range(80)]


def canonical_true_db(freq_hz: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    return (
        -14.0
        + 8.0 * math.cos(math.pi * x)
        - 5.5 * math.cos(2.0 * math.pi * x)
        + 3.0 * math.cos(3.0 * math.pi * x)
        - 1.5 * math.cos(4.0 * math.pi * x)
        + 0.6 * math.cos(5.0 * math.pi * x)
    )


def make_partials(
    freqs_hz: list[float],
    truth_db_fn,
    noise_db: float = 0.0,
    seed: int = 0,
    smr: float = 10.0,
) -> list:
    rng = random.Random(seed)
    partials = []
    for freq_hz in freqs_hz:
        db = truth_db_fn(freq_hz)
        if noise_db:
            db += rng.gauss(0.0, noise_db)
        partials.append([freq_hz, 10.0 ** (db / 20.0), smr])
    return partials


def rms_error(model, truth_db_fn, eval_freqs: list[float] | None = None) -> float:
    eval_freqs = eval_freqs or EVAL_FREQS
    return math.sqrt(sum(
        (_evaluate_new_model(model, freq_hz) - truth_db_fn(freq_hz)) ** 2
        for freq_hz in eval_freqs
    ) / max(len(eval_freqs), 1))


def run_fixture(partials: list, truth_db_fn):
    _, _, global_diag, global_model = fit_new_model(partials, SR, LO, HI, ORD, REG)
    _, _, subband_diag, subband_model = fit_new_model(
        partials,
        SR,
        LO,
        HI,
        ORD,
        REG,
        subband_options=SUBBAND_OPTIONS,
    )
    return {
        "global_diag": global_diag,
        "global_model": global_model,
        "global_rms_db": rms_error(global_model, truth_db_fn),
        "subband_diag": subband_diag,
        "subband_model": subband_model,
        "subband_rms_db": rms_error(subband_model, truth_db_fn),
    }


def fixture_s1_dense_harmonic():
    fund = 200.0
    freqs = [fund * k for k in range(1, 41) if fund * k <= HI]
    return make_partials(freqs, canonical_true_db, noise_db=1.5, seed=1), canonical_true_db


def fixture_s2_sparse_guitar():
    fund = 200.0
    freqs = [fund * k for k in range(1, 8)]
    return make_partials(freqs, canonical_true_db, noise_db=1.0, seed=2), canonical_true_db


def fixture_s5_inharmonic():
    fund = 420.0
    ratios = [1.0, 2.76, 5.41, 8.93, 11.9, 15.4]
    freqs = [fund * ratio for ratio in ratios if fund * ratio <= HI]
    return make_partials(freqs, canonical_true_db, noise_db=0.3, seed=6), canonical_true_db


def b1_truth_db(freq_hz: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    low_bump = 4.6 * math.exp(-0.5 * ((x - 0.18) / 0.085) ** 2)
    high_cluster = 8.5 * math.exp(-0.5 * ((x - 0.86) / 0.035) ** 2)
    low_ripple = 1.7 * math.cos(5.0 * math.pi * x) if x < 0.42 else 0.0
    return (
        -18.0
        + 6.8 * math.cos(math.pi * x)
        - 4.2 * math.cos(2.0 * math.pi * x)
        + 1.2 * math.cos(3.0 * math.pi * x)
        + low_bump
        + high_cluster
        + low_ripple
    )


def fixture_b1_low_dense_high_sparse():
    freqs = [180.0, 240.0, 320.0, 420.0, 540.0, 690.0, 880.0, 1120.0, 1430.0, 1820.0,
             3200.0, 4300.0, 6100.0, 7450.0]
    return make_partials(freqs, b1_truth_db), b1_truth_db


def b2_truth_db(freq_hz: float, center: float = 0.72) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    bright_cluster = 7.2 * math.exp(-0.5 * ((x - center) / 0.045) ** 2)
    low_shelf = 1.8 * math.exp(-0.5 * ((x - 0.28) / 0.15) ** 2)
    return (
        -18.0
        + 6.5 * math.cos(math.pi * x)
        - 3.7 * math.cos(2.0 * math.pi * x)
        + 1.4 * math.cos(3.0 * math.pi * x)
        + low_shelf
        + bright_cluster
    )


def fixture_b2_bright_high_cluster():
    freqs = [160.0, 250.0, 360.0, 520.0, 760.0, 1100.0, 1600.0, 2500.0,
             4100.0, 5050.0, 5520.0, 6030.0, 7350.0]
    return make_partials(freqs, b2_truth_db), b2_truth_db


def boundary_sweep_metrics():
    freqs = [160.0, 250.0, 360.0, 520.0, 760.0, 1100.0, 1600.0, 2500.0,
             4100.0, 5050.0, 5520.0, 6030.0, 7350.0]
    centers = [0.60, 0.66, 0.72, 0.78]
    probe_freqs = [3300.0, 3900.0, 4600.0, 5200.0, 5900.0, 6600.0]
    fitted = []
    truths = []
    frame_diags = []

    for center in centers:
        truth_fn = lambda freq_hz, c=center: b2_truth_db(freq_hz, center=c)
        truths.append(truth_fn)
        partials = make_partials(freqs, truth_fn)
        _, _, subband_diag, subband_model = fit_new_model(
            partials,
            SR,
            LO,
            HI,
            ORD,
            REG,
            subband_options=SUBBAND_OPTIONS,
        )
        fitted.append(subband_model)
        frame_diags.append(subband_diag)

    zipper_residuals = []
    for frame_index in range(1, len(fitted)):
        for probe_hz in probe_freqs:
            estimated_delta = _evaluate_new_model(fitted[frame_index], probe_hz)
            estimated_delta -= _evaluate_new_model(fitted[frame_index - 1], probe_hz)
            truth_delta = truths[frame_index](probe_hz) - truths[frame_index - 1](probe_hz)
            zipper_residuals.append(estimated_delta - truth_delta)

    zipper_rms_db = math.sqrt(sum(value * value for value in zipper_residuals) / max(len(zipper_residuals), 1))
    zipper_max_db = max(abs(value) for value in zipper_residuals)
    return {
        "frame_diags": frame_diags,
        "zipper_rms_db": zipper_rms_db,
        "zipper_max_db": zipper_max_db,
    }

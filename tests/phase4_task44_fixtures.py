#!/usr/bin/env python3
"""Shared fixtures and metrics for Phase 4 Task 4.4 refinement research."""

from __future__ import annotations

import math
import time
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from visualize_envelopes import (
    RefinementOptions,
    _evaluate_new_model,
    _frequency_for_bark_position,
    _normalized_position_bark,
    fit_new_model,
)

SR = 48000.0
LO = 80.0
HI = 8000.0
ORD = 16
REG = 1.0e-3
TARGET_GRID_SIZE = 72
EVAL_FREQS = [LO * (HI / LO) ** (i / 95.0) for i in range(96)]


def dense_target_grid() -> list[float]:
    return [
        _frequency_for_bark_position(i / float(TARGET_GRID_SIZE - 1), LO, HI)
        for i in range(TARGET_GRID_SIZE)
    ]


TARGET_GRID_HZ = dense_target_grid()


def sample_truth(truth_db_fn, freqs_hz: list[float]) -> list[float]:
    return [truth_db_fn(freq_hz) for freq_hz in freqs_hz]


def make_partials(freqs_hz: list[float], truth_db_fn, smr: float = 10.0) -> list:
    return [[freq_hz, 10.0 ** (truth_db_fn(freq_hz) / 20.0), smr] for freq_hz in freqs_hz]


def rms_error(model, truth_db_fn, eval_freqs: list[float] | None = None) -> float:
    eval_freqs = eval_freqs or EVAL_FREQS
    return math.sqrt(sum(
        (_evaluate_new_model(model, freq_hz) - truth_db_fn(freq_hz)) ** 2
        for freq_hz in eval_freqs
    ) / max(len(eval_freqs), 1))


def canonical_truth_db(freq_hz: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    return (
        -15.0
        + 7.5 * math.cos(math.pi * x)
        - 4.9 * math.cos(2.0 * math.pi * x)
        + 2.4 * math.cos(3.0 * math.pi * x)
        - 1.2 * math.cos(4.0 * math.pi * x)
    )


def r1_truth_db(freq_hz: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    bump = 4.2 * math.exp(-0.5 * ((x - 0.59) / 0.055) ** 2)
    shoulder = 1.6 * math.exp(-0.5 * ((x - 0.39) / 0.08) ** 2)
    return canonical_truth_db(freq_hz) + bump + shoulder


def r2_truth_db(freq_hz: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    residual_bump = 5.1 * math.exp(-0.5 * ((x - 0.74) / 0.07) ** 2)
    low_shelf = 1.1 * math.exp(-0.5 * ((x - 0.18) / 0.10) ** 2)
    return canonical_truth_db(freq_hz) + residual_bump + low_shelf


def s3_truth_db(freq_hz: float) -> float:
    return canonical_truth_db(freq_hz)


def fixture_r1_mild_tracking_error():
    freqs = [196.0, 404.0, 618.0, 838.0, 1098.0, 1388.0, 1765.0, 2295.0, 2910.0, 3645.0, 4630.0]
    return {
        "label": "R1_mild_tracking_error",
        "partials": make_partials(freqs, r1_truth_db),
        "truth_db_fn": r1_truth_db,
        "target_hz": TARGET_GRID_HZ,
        "target_db": sample_truth(r1_truth_db, TARGET_GRID_HZ),
    }


def fixture_r2_sparse_plus_residual_bump():
    freqs = [180.0, 260.0, 390.0, 620.0, 930.0, 1380.0, 2060.0, 2980.0, 4340.0]
    partial_truth = lambda freq_hz: canonical_truth_db(freq_hz) + 0.8 * math.exp(
        -0.5 * ((_normalized_position_bark(freq_hz, LO, HI) - 0.18) / 0.10) ** 2
    )
    return {
        "label": "R2_sparse_plus_residual_bump",
        "partials": make_partials(freqs, partial_truth),
        "truth_db_fn": r2_truth_db,
        "target_hz": TARGET_GRID_HZ,
        "target_db": sample_truth(r2_truth_db, TARGET_GRID_HZ),
    }


def fixture_s1_dense_clean():
    freqs = [200.0 * k for k in range(1, 41) if 200.0 * k <= HI]
    return {
        "label": "S1_dense_clean",
        "partials": make_partials(freqs, canonical_truth_db),
        "truth_db_fn": canonical_truth_db,
        "target_hz": TARGET_GRID_HZ,
        "target_db": sample_truth(canonical_truth_db, TARGET_GRID_HZ),
    }


def fixture_s3_outlier_partials():
    freqs = [210.0 * k for k in range(1, 20) if 210.0 * k <= HI]
    outlier_freqs = {round(freqs[4], 1), round(freqs[11], 1)}

    def outlier_truth(freq_hz: float) -> float:
        base = s3_truth_db(freq_hz)
        if round(freq_hz, 1) in outlier_freqs:
            return base + 13.5
        return base

    return {
        "label": "S3_outlier_partials",
        "partials": make_partials(freqs, outlier_truth),
        "truth_db_fn": s3_truth_db,
        "target_hz": TARGET_GRID_HZ,
        "target_db": sample_truth(s3_truth_db, TARGET_GRID_HZ),
    }


def refinement_options_for_fixture(fixture: dict) -> RefinementOptions:
    return RefinementOptions(
        enabled=True,
        bark_grid_size=TARGET_GRID_SIZE,
        max_iterations=3,
        initial_step_size=0.55,
        min_step_size=0.05,
        max_delta_db=1.25,
        blur_weight=0.35,
        anchor_strength=0.20,
        uncertainty_gain=0.65,
        dense_target_hz=list(fixture["target_hz"]),
        dense_target_db=list(fixture["target_db"]),
        target_label=fixture["label"],
    )


def run_fixture(fixture: dict) -> dict:
    partials = fixture["partials"]
    truth_db_fn = fixture["truth_db_fn"]

    _, _, raw_diag, raw_model = fit_new_model(partials, SR, LO, HI, ORD, REG)
    _, _, refined_diag, refined_model = fit_new_model(
        partials,
        SR,
        LO,
        HI,
        ORD,
        REG,
        refinement_options=refinement_options_for_fixture(fixture),
    )

    return {
        "label": fixture["label"],
        "raw_diag": raw_diag,
        "raw_model": raw_model,
        "raw_rms_db": rms_error(raw_model, truth_db_fn),
        "refined_diag": refined_diag,
        "refined_model": refined_model,
        "refined_rms_db": rms_error(refined_model, truth_db_fn),
    }


def benchmark_runtime_ratio(fixture: dict, repeats: int = 24) -> dict:
    partials = fixture["partials"]
    options = refinement_options_for_fixture(fixture)

    start = time.perf_counter()
    for _ in range(repeats):
        fit_new_model(partials, SR, LO, HI, ORD, REG)
    raw_ms = (time.perf_counter() - start) * 1000.0 / repeats

    start = time.perf_counter()
    for _ in range(repeats):
        fit_new_model(partials, SR, LO, HI, ORD, REG, refinement_options=options)
    refined_ms = (time.perf_counter() - start) * 1000.0 / repeats

    return {
        "raw_ms": raw_ms,
        "refined_ms": refined_ms,
        "ratio": refined_ms / max(raw_ms, 1.0e-9),
    }

#!/usr/bin/env python3
"""Task 4.1 benchmark: bounded true-envelope release vs heuristic release."""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from visualize_envelopes import fit_new_model, _evaluate_new_model, _normalized_position_bark

SR = 48000.0
LO = 80.0
HI = 8000.0
ORD = 16
REG = 1.0e-3
EVAL_FREQS = [LO * (HI / LO) ** (i / 39.0) for i in range(40)]


def true_db(f: float) -> float:
    x = _normalized_position_bark(f, LO, HI)
    return (-14.0
            + 8.0 * math.cos(1.0 * math.pi * x)
            - 5.5 * math.cos(2.0 * math.pi * x)
            + 3.0 * math.cos(3.0 * math.pi * x)
            - 1.5 * math.cos(4.0 * math.pi * x)
            + 0.6 * math.cos(5.0 * math.pi * x))


def make_partials(freqs: list[float], dbs: list[float], smr: float = 10.0,
                  noise_db: float = 0.0, seed: int = 0) -> list:
    import random
    rng = random.Random(seed)
    return [
        [f, 10.0 ** ((db + (rng.gauss(0.0, noise_db) if noise_db else 0.0)) / 20.0), smr]
        for f, db in zip(freqs, dbs)
    ]


def interp_grid(grid_hz: list[float], grid_db: list[float], f: float) -> float:
    if f <= grid_hz[0]:
        return grid_db[0]
    if f >= grid_hz[-1]:
        return grid_db[-1]
    lo, hi = 0, len(grid_hz) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if grid_hz[mid] <= f:
            lo = mid
        else:
            hi = mid
    t = (f - grid_hz[lo]) / max(grid_hz[hi] - grid_hz[lo], 1.0e-12)
    return grid_db[lo] + t * (grid_db[hi] - grid_db[lo])


def rms_at_freqs(grid_hz: list[float], grid_db: list[float], eval_freqs: list[float]) -> float:
    errs = [(interp_grid(grid_hz, grid_db, f) - true_db(f)) ** 2 for f in eval_freqs]
    return math.sqrt(sum(errs) / max(len(errs), 1))


def fit_mode(parts: list, mode: str, previous_model=None):
    return fit_new_model(parts, SR, LO, HI, ORD, REG,
                         previous_model=previous_model,
                         true_envelope_release_mode=mode)


def scenario_s1(mode: str) -> float:
    fund = 200.0
    freqs = [fund * k for k in range(1, 41) if fund * k <= HI]
    parts = make_partials(freqs, [true_db(f) for f in freqs], noise_db=1.5, seed=1)
    grid_hz, grid_db, _, _ = fit_mode(parts, mode)
    return rms_at_freqs(grid_hz, grid_db, EVAL_FREQS)


def scenario_s3(mode: str) -> float:
    fund = 200.0
    freqs = [fund * k for k in range(1, 20) if fund * k <= HI]
    dbs = [true_db(f) for f in freqs]
    dbs[4] += 15.0
    dbs[11] += 15.0
    parts = make_partials(freqs, dbs, noise_db=0.5, seed=3)
    grid_hz, grid_db, _, _ = fit_mode(parts, mode)
    return rms_at_freqs(grid_hz, grid_db, EVAL_FREQS)


def scenario_s4(mode: str) -> float:
    prev_freqs = [100.0, 270.0, 600.0, 1400.0, 3200.0, 5600.0, 7800.0]
    prev_parts = make_partials(prev_freqs, [true_db(f) for f in prev_freqs], noise_db=1.0, seed=4)
    _, _, _, prev_model = fit_mode(prev_parts, "heuristic")

    curr_freqs = [910.0, 970.0, 1030.0, 1090.0]
    curr_parts = make_partials(curr_freqs, [true_db(f) for f in curr_freqs], noise_db=0.5, seed=5)
    grid_hz, grid_db, _, _ = fit_mode(curr_parts, mode, previous_model=prev_model)
    return rms_at_freqs(grid_hz, grid_db, EVAL_FREQS)


def scenario_s5(mode: str) -> float:
    fund = 420.0
    ratios = [1.0, 2.76, 5.41, 8.93, 11.9, 15.4]
    freqs = [fund * r for r in ratios if fund * r <= HI]
    parts = make_partials(freqs, [true_db(f) for f in freqs], noise_db=0.3, seed=6)
    grid_hz, grid_db, _, _ = fit_mode(parts, mode)
    return rms_at_freqs(grid_hz, grid_db, EVAL_FREQS)


def scenario_s4b(mode: str) -> dict[str, float]:
    prev_freqs = [100.0, 280.0, 620.0, 1400.0, 3200.0, 5600.0, 7800.0]
    prev_parts = make_partials(prev_freqs, [true_db(f) for f in prev_freqs], noise_db=0.8, seed=11)
    _, _, _, prev_model = fit_mode(prev_parts, "heuristic")

    curr_freqs = [920.0, 1000.0, 1100.0, 1180.0]
    curr_dbs = [-1.0, 0.0, 0.0, -1.0]
    curr_parts = make_partials(curr_freqs, curr_dbs)
    _, _, diag, model = fit_mode(curr_parts, mode, previous_model=prev_model)
    protected_freqs = [1000.0, 1100.0]
    undershoots = [max(0.0, -_evaluate_new_model(model, f)) for f in protected_freqs]
    return {
        "max_undershoot_db": max(undershoots),
        "p95_undershoot_db": max(undershoots),
        "release_bound_db": float(diag.get("release_bound_db", 0.0)),
        "max_release_violation_db": float(diag.get("max_release_violation_db", 0.0)),
        "constrained_peak_count": float(diag.get("constrained_peak_count", 0)),
    }


def scenario_s4c(mode: str) -> dict[str, float]:
    prev_freqs = [100.0, 280.0, 620.0, 1400.0, 3200.0, 5600.0, 7800.0]
    prev_parts = make_partials(prev_freqs, [true_db(f) for f in prev_freqs], noise_db=0.8, seed=11)
    _, _, _, prev_model = fit_mode(prev_parts, "heuristic")

    curr_freqs = [920.0, 1000.0, 1040.0, 1120.0, 1180.0]
    curr_dbs = [-1.0, 0.0, 7.0, 0.0, -1.0]
    curr_parts = make_partials(curr_freqs, curr_dbs)
    _, _, diag, model = fit_mode(curr_parts, mode, previous_model=prev_model)
    false_gap = 7.0 - _evaluate_new_model(model, 1040.0)
    return {
        "false_peak_gap_db": false_gap,
        "unclamped_outlier_count": float(diag.get("unclamped_outlier_count", 0)),
        "release_bound_db": float(diag.get("release_bound_db", 0.0)),
    }


def main() -> int:
    heuristic = {
        "S1": scenario_s1("heuristic"),
        "S3": scenario_s3("heuristic"),
        "S4": scenario_s4("heuristic"),
        "S5": scenario_s5("heuristic"),
        "S4b": scenario_s4b("heuristic"),
        "S4c": scenario_s4c("heuristic"),
    }
    bounded = {
        "S1": scenario_s1("bounded"),
        "S3": scenario_s3("bounded"),
        "S4": scenario_s4("bounded"),
        "S5": scenario_s5("bounded"),
        "S4b": scenario_s4b("bounded"),
        "S4c": scenario_s4c("bounded"),
    }

    s4_improvement = ((heuristic["S4"] - bounded["S4"]) / heuristic["S4"]) * 100.0
    s4b_reduction = ((heuristic["S4b"]["max_undershoot_db"] - bounded["S4b"]["max_undershoot_db"])
                     / heuristic["S4b"]["max_undershoot_db"]) * 100.0

    print("Task 4.1 bounded true-envelope release benchmark")
    print("")
    print("RMS envelope error (dB)")
    for key in ("S1", "S3", "S4", "S5"):
        print(f"  {key}: heuristic={heuristic[key]:.6f} bounded={bounded[key]:.6f} "
              f"delta={bounded[key] - heuristic[key]:+.6f}")
    print("")
    print(f"S4 improvement: {s4_improvement:.2f}%")
    print("")
    print("S4b protected clustered")
    print(f"  heuristic max undershoot={heuristic['S4b']['max_undershoot_db']:.6f} dB")
    print(f"  bounded   max undershoot={bounded['S4b']['max_undershoot_db']:.6f} dB")
    print(f"  bounded   release bound={bounded['S4b']['release_bound_db']:.6f} dB")
    print(f"  bounded   max violation={bounded['S4b']['max_release_violation_db']:.6f} dB")
    print(f"  reduction={s4b_reduction:.2f}%")
    print("")
    print("S4c false transient")
    print(f"  heuristic false-peak gap={heuristic['S4c']['false_peak_gap_db']:.6f} dB")
    print(f"  bounded   false-peak gap={bounded['S4c']['false_peak_gap_db']:.6f} dB")
    print(f"  bounded   unclamped outliers={bounded['S4c']['unclamped_outlier_count']:.0f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

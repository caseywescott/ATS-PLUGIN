#!/usr/bin/env python3
"""Three-model benchmark: RMS error vs true envelope across five representative scenarios.

Reproduces (and extends) the table from the Research Log:
  S1: Dense harmonic (40 pts)          — 40 harmonics of 200 Hz, clean
  S2: Sparse guitar  (7 pts)           — 7 harmonics of 200 Hz, clean
  S3: Outlier partials (19 pts, 2 out) — 17 clean + 2 at +15 dB
  S4: Clustered + virtual (4+virt)     — 4 clustered at ~1 kHz, prev model provides virtual
  S5: Crotale (6 pts, inharmonic)      — 6 inharmonic partials (bell-like ratios)

Error metric: RMS dB error vs true analytical envelope, evaluated at partial positions.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from visualize_envelopes import (
    _normalized_position_bark,
    fit_new_model,
    rdc_fit_and_eval,
    _evaluate_new_model,
)

SR  = 48000.0
LO  = 80.0
HI  = 8000.0
ORD = 16
REG = 1.0e-3

# 40 log-spaced reference frequencies used as the evaluation grid for all scenarios.
# Evaluating on a dense grid (rather than only at partial positions) is what
# exposes the extrapolation collapse of Linear/Bark on clustered/sparse frames
# (e.g. S4 Linear≈16 dB because 4 pts near 1 kHz leave the rest completely unconstrained).
N_EVAL = 40
EVAL_FREQS: list[float] = [
    LO * (HI / LO) ** (i / (N_EVAL - 1)) for i in range(N_EVAL)
]


# ---------------------------------------------------------------------------
# Ground-truth envelope (smooth, Bark-normalized 3-term cosine)
# ---------------------------------------------------------------------------

def true_db(f: float) -> float:
    """Analytical true envelope — richer Bark-normalised 5-term cosine.

    The higher-order terms create meaningful curvature that the linear-Hz basis
    can only represent approximately, exposing the Bark vs Linear performance gap
    even on dense frames.
    """
    x = _normalized_position_bark(f, LO, HI)
    return (-14.0
            +  8.0 * math.cos(1.0 * math.pi * x)
            -  5.5 * math.cos(2.0 * math.pi * x)
            +  3.0 * math.cos(3.0 * math.pi * x)
            -  1.5 * math.cos(4.0 * math.pi * x)
            +  0.6 * math.cos(5.0 * math.pi * x))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_partials(freqs: list[float], dbs: list[float], smr: float = 10.0,
                  noise_db: float = 0.0, seed: int = 0) -> list:
    """Build a partial list.  noise_db adds Gaussian amplitude jitter (sigma=noise_db)."""
    import random
    rng = random.Random(seed)
    return [
        [f, 10.0 ** ((db + (rng.gauss(0.0, noise_db) if noise_db else 0.0)) / 20.0), smr]
        for f, db in zip(freqs, dbs)
    ]


def _interp_grid(grid_hz: list[float], grid_db: list[float], f: float) -> float:
    if not grid_hz:
        return float("nan")
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
    t = (f - grid_hz[lo]) / max(grid_hz[hi] - grid_hz[lo], 1e-12)
    return grid_db[lo] + t * (grid_db[hi] - grid_db[lo])


def rms_at_freqs(
    grid_hz: list[float],
    grid_db: list[float],
    eval_freqs: list[float],
) -> float:
    if not grid_hz:
        return float("inf")
    errs = [(_interp_grid(grid_hz, grid_db, f) - true_db(f)) ** 2 for f in eval_freqs]
    return math.sqrt(sum(errs) / max(len(errs), 1))


# ---------------------------------------------------------------------------
# S1 — Dense harmonic (40 pts)
# ---------------------------------------------------------------------------

def scenario_s1() -> tuple[float, float, float]:
    """40 harmonics of 200 Hz, ±1.5 dB amplitude jitter; error on dense 40-pt grid."""
    fund  = 200.0
    freqs = [fund * k for k in range(1, 41) if fund * k <= HI]
    dbs   = [true_db(f) for f in freqs]
    parts = make_partials(freqs, dbs, noise_db=1.5, seed=1)

    lin_g,  lin_db,  _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=False)
    bark_g, bark_db, _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=True)
    new_g,  new_db,  _, _  = fit_new_model(parts, SR, LO, HI, ORD, REG)

    return (
        rms_at_freqs(lin_g,  lin_db,  EVAL_FREQS),
        rms_at_freqs(bark_g, bark_db, EVAL_FREQS),
        rms_at_freqs(new_g,  new_db,  EVAL_FREQS),
    )


# ---------------------------------------------------------------------------
# S2 — Sparse guitar (7 pts)
# ---------------------------------------------------------------------------

def scenario_s2() -> tuple[float, float, float]:
    """7 harmonics of 200 Hz (200–1400 Hz only), ±1 dB jitter; error on dense 40-pt grid."""
    fund  = 200.0
    freqs = [fund * k for k in range(1, 8)]   # 200 – 1400 Hz
    dbs   = [true_db(f) for f in freqs]
    parts = make_partials(freqs, dbs, noise_db=1.0, seed=2)

    lin_g,  lin_db,  _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=False)
    bark_g, bark_db, _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=True)
    new_g,  new_db,  _, _  = fit_new_model(parts, SR, LO, HI, ORD, REG)

    return (
        rms_at_freqs(lin_g,  lin_db,  EVAL_FREQS),
        rms_at_freqs(bark_g, bark_db, EVAL_FREQS),
        rms_at_freqs(new_g,  new_db,  EVAL_FREQS),
    )


# ---------------------------------------------------------------------------
# S3 — Outlier partials (19 pts, 2 outliers at +15 dB)
# ---------------------------------------------------------------------------

def scenario_s3() -> tuple[float, float, float]:
    """19 quasi-harmonic partials; 2 have +15 dB outlier offset; error on dense grid."""
    fund  = 200.0
    freqs = [fund * k for k in range(1, 20) if fund * k <= HI]   # up to 19 pts
    dbs   = [true_db(f) for f in freqs]
    # Two prominent outliers.
    dbs_out = list(dbs)
    for i in [4, 11]:
        dbs_out[i] += 15.0

    parts = make_partials(freqs, dbs_out, noise_db=0.5, seed=3)

    lin_g,  lin_db,  _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=False)
    bark_g, bark_db, _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=True)
    new_g,  new_db,  _, _  = fit_new_model(parts, SR, LO, HI, ORD, REG)

    return (
        rms_at_freqs(lin_g,  lin_db,  EVAL_FREQS),
        rms_at_freqs(bark_g, bark_db, EVAL_FREQS),
        rms_at_freqs(new_g,  new_db,  EVAL_FREQS),
    )


# ---------------------------------------------------------------------------
# S4 — Clustered + virtual (4 pts clustered + cross-frame virtual)
# ---------------------------------------------------------------------------

def scenario_s4() -> tuple[float, float, float]:
    """Previous frame: 7 well-spread partials → prev_model for virtual injection.
    Current frame: 4 partials clustered around 1 kHz; error on dense 40-pt grid."""
    prev_freqs = [100.0, 270.0, 600.0, 1400.0, 3200.0, 5600.0, 7800.0]
    prev_parts = make_partials(prev_freqs, [true_db(f) for f in prev_freqs], noise_db=1.0, seed=4)
    _, _, _, prev_model = fit_new_model(prev_parts, SR, LO, HI, ORD, REG)

    curr_freqs = [910.0, 970.0, 1030.0, 1090.0]
    curr_parts = make_partials(curr_freqs, [true_db(f) for f in curr_freqs], noise_db=0.5, seed=5)

    lin_g,  lin_db,  _      = rdc_fit_and_eval(curr_parts, SR, LO, HI, ORD, REG, use_bark=False)
    bark_g, bark_db, _      = rdc_fit_and_eval(curr_parts, SR, LO, HI, ORD, REG, use_bark=True)
    new_g,  new_db,  _, _   = fit_new_model(curr_parts, SR, LO, HI, ORD, REG,
                                             previous_model=prev_model)

    return (
        rms_at_freqs(lin_g,  lin_db,  EVAL_FREQS),
        rms_at_freqs(bark_g, bark_db, EVAL_FREQS),
        rms_at_freqs(new_g,  new_db,  EVAL_FREQS),
    )


# ---------------------------------------------------------------------------
# S5 — Crotale (6 pts, inharmonic)
# ---------------------------------------------------------------------------

def scenario_s5() -> tuple[float, float, float]:
    """6 inharmonic partials (crotale-like ratios), clean; error on dense 40-pt grid."""
    fund   = 420.0
    ratios = [1.0, 2.76, 5.41, 8.93, 11.9, 15.4]
    freqs  = [fund * r for r in ratios if fund * r <= HI]
    dbs    = [true_db(f) for f in freqs]
    parts  = make_partials(freqs, dbs, noise_db=0.3, seed=6)

    lin_g,  lin_db,  _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=False)
    bark_g, bark_db, _     = rdc_fit_and_eval(parts, SR, LO, HI, ORD, REG, use_bark=True)
    new_g,  new_db,  _, _  = fit_new_model(parts, SR, LO, HI, ORD, REG)

    return (
        rms_at_freqs(lin_g,  lin_db,  EVAL_FREQS),
        rms_at_freqs(bark_g, bark_db, EVAL_FREQS),
        rms_at_freqs(new_g,  new_db,  EVAL_FREQS),
    )


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

SCENARIOS: list[tuple[str, any]] = [
    ("S1: Dense harmonic (40 pts)",           scenario_s1),
    ("S2: Sparse guitar  (7 pts)",            scenario_s2),
    ("S3: Outlier partials (19 pts, 2 out)",  scenario_s3),
    ("S4: Clustered+virtual (4 pts + virt)",  scenario_s4),
    ("S5: Crotale (6 pts, inharmonic)",       scenario_s5),
]


def run_benchmark(label: str = "Current") -> dict[str, tuple[float, float, float]]:
    results: dict[str, tuple[float, float, float]] = {}
    for name, fn in SCENARIOS:
        results[name] = fn()
    return results


def print_table(results: dict[str, tuple[float, float, float]], label: str = "Model") -> None:
    w = 42
    hdr = f"  {'Scenario':<{w}}  {'Linear':>8}  {'Bark':>8}  {label:>10}"
    sep = "  " + "-" * w + "  " + "-" * 8 + "  " + "-" * 8 + "  " + "-" * 10

    print(hdr)
    print(sep)

    lin_sum, bark_sum, new_sum = 0.0, 0.0, 0.0
    for name, (l, b, n) in results.items():
        print(f"  {name:<{w}}  {l:8.2f}  {b:8.2f}  {n:10.2f}")
        lin_sum  += l
        bark_sum += b
        new_sum  += n

    n = len(results)
    print(sep)
    print(f"  {'Mean':<{w}}  {lin_sum/n:8.2f}  {bark_sum/n:8.2f}  {new_sum/n:10.2f}")


def compare_tables(
    before: dict[str, tuple[float, float, float]],
    after:  dict[str, tuple[float, float, float]],
) -> None:
    w = 42
    hdr = (f"  {'Scenario':<{w}}  {'Linear':>8}  {'Bark':>8}"
           f"  {'Old New':>8}  {'Impr New':>8}  {'Delta':>7}")
    sep = "  " + "-" * w + "  " + ("-" * 8 + "  ") * 4 + "-" * 7

    print(hdr)
    print(sep)

    lin_sum = bark_sum = old_sum = new_sum = 0.0
    for name in before:
        l, b, o = before[name]
        _,  _, n = after[name]
        delta = o - n   # positive = improvement
        print(f"  {name:<{w}}  {l:8.2f}  {b:8.2f}"
              f"  {o:8.2f}  {n:8.2f}  {delta:+7.3f}")
        lin_sum  += l
        bark_sum += b
        old_sum  += o
        new_sum  += n

    cnt = len(before)
    print(sep)
    print(f"  {'Mean':<{w}}  {lin_sum/cnt:8.2f}  {bark_sum/cnt:8.2f}"
          f"  {old_sum/cnt:8.2f}  {new_sum/cnt:8.2f}  {(old_sum-new_sum)/cnt:+7.3f}")

    if old_sum > 0:
        pct = (old_sum - new_sum) / old_sum * 100.0
        print(f"\n  Improvement (old → improved): {pct:+.1f}%")
    if lin_sum > 0:
        pct_lin = (lin_sum - new_sum) / lin_sum * 100.0
        print(f"  New vs Linear: {pct_lin:.1f}% error reduction")
    if bark_sum > 0:
        pct_bark = (bark_sum - new_sum) / bark_sum * 100.0
        print(f"  New vs Bark:   {pct_bark:.1f}% error reduction")


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser(description="Three-model RMS error benchmark.")
    parser.add_argument("--baseline", action="store_true",
                        help="Print only the baseline table (no before/after).")
    args = parser.parse_args()

    print("Running benchmark …")
    results = run_benchmark()
    print()
    print_table(results, label="New")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

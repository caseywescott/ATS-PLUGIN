#!/usr/bin/env python3
"""Focused tests for Phase 4 Task 4.2 temporal smoothing."""

from __future__ import annotations

import math
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "Tests"))
sys.path.insert(0, str(ROOT / "tools"))

from benchmark_phase4_task42 import evaluate_task42, fit_sequence, fixture_t2  # type: ignore

_FAILURES: list[str] = []
_PASSED = 0


def _ok(name: str) -> None:
    global _PASSED
    _PASSED += 1
    print(f"  ok: {name}")


def _fail(name: str, message: str) -> None:
    _FAILURES.append(f"{name}: {message}")
    print(f"  FAIL: {name}: {message}", file=sys.stderr)


def _assert(name: str, condition: bool, message: str) -> None:
    if condition:
        _ok(name)
    else:
        _fail(name, message)


def test_task42_gates() -> None:
    results = evaluate_task42()

    _assert(
        "task42_t1_jitter_gate",
        results["T1"]["improvement_pct"] >= 20.0,
        f"T1 jitter improvement={results['T1']['improvement_pct']:.2f}% should be >= 20%",
    )
    _assert(
        "task42_t2_onset_gate",
        results["T2"]["worsening_pct"] <= 5.0,
        f"T2 onset worsening={results['T2']['worsening_pct']:.2f}% should be <= 5%",
    )
    _assert(
        "task42_t3_motion_not_flattened",
        results["T3"]["smoothed_motion_db"] >= 0.80 * results["T3"]["truth_motion_db"],
        f"T3 smoothed motion={results['T3']['smoothed_motion_db']:.4f} dB should retain at least 80% "
        f"of truth motion={results['T3']['truth_motion_db']:.4f} dB",
    )
    _assert(
        "task42_t3_tracking_improves",
        results["T3"]["smoothed_tracking_error_db"] <= results["T3"]["raw_tracking_error_db"],
        f"T3 tracking error should improve: raw={results['T3']['raw_tracking_error_db']:.4f} dB "
        f"smoothed={results['T3']['smoothed_tracking_error_db']:.4f} dB",
    )

    for key in ("S1", "S3", "S5"):
        delta = results[key]["smoothed_rms_db"] - results[key]["raw_rms_db"]
        _assert(
            f"task42_{key.lower()}_no_regression",
            delta <= 0.02,
            f"{key} regression delta={delta:.6f} dB should stay <= 0.02 dB",
        )


def test_task42_transient_gate_diagnostics() -> None:
    frames, _ = fixture_t2()
    sequence = fit_sequence(frames, smoothing_enabled=True)
    onset_diag = sequence[1][2]

    _assert(
        "task42_temporal_applied_after_initial_frame",
        bool(onset_diag.get("temporal_applied", False)),
        f"expected temporal smoothing to apply on onset frame, got {onset_diag}",
    )
    _assert(
        "task42_transient_reason_recorded",
        "transient_boundary" in str(onset_diag.get("temporal_gate_reason", "")),
        f"expected transient gate reason, got {onset_diag.get('temporal_gate_reason')}",
    )
    _assert(
        "task42_gain_is_finite",
        math.isfinite(float(onset_diag.get("temporal_gain_mean", float("nan")))),
        f"expected finite temporal gain mean, got {onset_diag.get('temporal_gain_mean')}",
    )


def main() -> int:
    print("Running Python temporal smoothing tests ...")
    test_task42_gates()
    test_task42_transient_gate_diagnostics()

    print(f"\n{_PASSED} passed, {len(_FAILURES)} failed.")
    if _FAILURES:
        for failure in _FAILURES:
            print(f"  FAIL: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

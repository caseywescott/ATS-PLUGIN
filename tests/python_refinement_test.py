#!/usr/bin/env python3
"""Focused tests for Phase 4 Task 4.4 refinement."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).parent
sys.path.insert(0, str(ROOT))

from benchmark_phase4_task44 import evaluate_task44  # type: ignore

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


def test_task44_gates() -> None:
    results = evaluate_task44()

    r1 = results["R1_mild_tracking_error"]
    r2 = results["R2_sparse_plus_residual_bump"]
    s1 = results["S1_dense_clean"]
    s3 = results["S3_outlier_partials"]
    runtime = results["runtime"]

    r1_improvement = (r1["raw_rms_db"] - r1["refined_rms_db"]) / max(r1["raw_rms_db"], 1.0e-9) * 100.0
    r2_improvement = (r2["raw_rms_db"] - r2["refined_rms_db"]) / max(r2["raw_rms_db"], 1.0e-9) * 100.0
    s1_delta = s1["refined_rms_db"] - s1["raw_rms_db"]
    s3_delta = s3["refined_rms_db"] - s3["raw_rms_db"]

    _assert(
        "task44_targeted_fixture_gate",
        max(r1_improvement, r2_improvement) >= 5.0,
        f"expected at least one targeted fixture to improve >= 5%, got R1={r1_improvement:.2f}% R2={r2_improvement:.2f}%",
    )
    _assert(
        "task44_r1_monotonic_loss",
        bool(r1["refined_diag"].get("refinement_monotonic", False)),
        f"expected monotonic loss on R1, got {r1['refined_diag'].get('refinement_loss_history')}",
    )
    _assert(
        "task44_r2_monotonic_loss",
        bool(r2["refined_diag"].get("refinement_monotonic", False)),
        f"expected monotonic loss on R2, got {r2['refined_diag'].get('refinement_loss_history')}",
    )
    _assert(
        "task44_s1_clean_no_regression",
        s1_delta <= 0.02,
        f"S1 regression delta={s1_delta:.6f} dB should stay <= 0.02 dB",
    )
    _assert(
        "task44_s3_outlier_no_regression",
        s3_delta <= 0.02,
        f"S3 regression delta={s3_delta:.6f} dB should stay <= 0.02 dB",
    )
    _assert(
        "task44_runtime_budget",
        runtime["ratio"] <= 2.5,
        f"runtime ratio={runtime['ratio']:.3f}x should stay <= 2.5x",
    )


def test_task44_diagnostics_are_populated() -> None:
    results = evaluate_task44()
    diag = results["R1_mild_tracking_error"]["refined_diag"]

    _assert(
        "task44_refinement_applied_flag",
        bool(diag.get("refinement_applied", False)),
        f"expected refinement to apply, got {diag}",
    )
    _assert(
        "task44_iteration_count_positive",
        int(diag.get("refinement_iteration_count", 0)) >= 1,
        f"expected refinement iterations, got {diag.get('refinement_iteration_count')}",
    )
    _assert(
        "task44_runtime_recorded",
        float(diag.get("refinement_runtime_ms", 0.0)) > 0.0,
        f"expected positive runtime, got {diag.get('refinement_runtime_ms')}",
    )
    _assert(
        "task44_loss_decreases",
        float(diag.get("refinement_loss_final", 1.0)) <= float(diag.get("refinement_loss_initial", 0.0)),
        f"expected loss decrease, got {diag.get('refinement_loss_history')}",
    )


def main() -> int:
    print("Running Python refinement tests …")
    test_task44_gates()
    test_task44_diagnostics_are_populated()

    print(f"\n{_PASSED} passed, {len(_FAILURES)} failed.")
    if _FAILURES:
        for failure in _FAILURES:
            print(f"  FAIL: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Task 4.4 benchmark: optional refinement against dense target spectra."""

from __future__ import annotations

from phase4_task44_fixtures import (
    benchmark_runtime_ratio,
    fixture_r1_mild_tracking_error,
    fixture_r2_sparse_plus_residual_bump,
    fixture_s1_dense_clean,
    fixture_s3_outlier_partials,
    run_fixture,
)


def improvement_pct(raw_rms_db: float, refined_rms_db: float) -> float:
    if raw_rms_db <= 1.0e-9:
        return 0.0
    return (raw_rms_db - refined_rms_db) / raw_rms_db * 100.0


def print_fixture(result: dict, gate: str) -> None:
    refined_diag = result["refined_diag"]
    print(
        f"{result['label']:<30} "
        f"raw={result['raw_rms_db']:.6f} dB  "
        f"refined={result['refined_rms_db']:.6f} dB  "
        f"delta={improvement_pct(result['raw_rms_db'], result['refined_rms_db']):>7.2f}%  "
        f"gate={gate}"
    )
    print(
        f"  loss: {refined_diag.get('refinement_loss_initial', 0.0):.6f} -> "
        f"{refined_diag.get('refinement_loss_final', 0.0):.6f}  "
        f"iters={refined_diag.get('refinement_iteration_count', 0)}  "
        f"runtime={refined_diag.get('refinement_runtime_ms', 0.0):.3f} ms"
    )


def evaluate_task44() -> dict:
    fixtures = [
        (fixture_r1_mild_tracking_error(), "improve by 5-12%"),
        (fixture_r2_sparse_plus_residual_bump(), "improve by 5-12%"),
        (fixture_s1_dense_clean(), "change <= 0.02 dB"),
        (fixture_s3_outlier_partials(), "regress <= 0.02 dB"),
    ]

    results = {}
    for fixture, _ in fixtures:
        results[fixture["label"]] = run_fixture(fixture)

    runtime = benchmark_runtime_ratio(fixture_r1_mild_tracking_error())
    results["runtime"] = runtime
    return results


def main() -> int:
    fixtures = [
        (fixture_r1_mild_tracking_error(), "improve by 5-12%"),
        (fixture_r2_sparse_plus_residual_bump(), "improve by 5-12%"),
        (fixture_s1_dense_clean(), "change <= 0.02 dB"),
        (fixture_s3_outlier_partials(), "regress <= 0.02 dB"),
    ]

    print("Running Task 4.4 benchmark …\n")
    results = {}
    for fixture, gate in fixtures:
        result = run_fixture(fixture)
        results[fixture["label"]] = result
        print_fixture(result, gate)

    runtime = benchmark_runtime_ratio(fixture_r1_mild_tracking_error())
    results["runtime"] = runtime
    print("\nRuntime")
    print(
        f"  raw={runtime['raw_ms']:.3f} ms  refined={runtime['refined_ms']:.3f} ms  "
        f"ratio={runtime['ratio']:.3f}x"
    )

    r1_improvement = improvement_pct(
        results["R1_mild_tracking_error"]["raw_rms_db"],
        results["R1_mild_tracking_error"]["refined_rms_db"],
    )
    r2_improvement = improvement_pct(
        results["R2_sparse_plus_residual_bump"]["raw_rms_db"],
        results["R2_sparse_plus_residual_bump"]["refined_rms_db"],
    )
    s1_delta = (
        results["S1_dense_clean"]["refined_rms_db"]
        - results["S1_dense_clean"]["raw_rms_db"]
    )
    s3_delta = (
        results["S3_outlier_partials"]["refined_rms_db"]
        - results["S3_outlier_partials"]["raw_rms_db"]
    )
    monotonic_ok = all(
        bool(results[label]["refined_diag"].get("refinement_monotonic", False))
        for label in (
            "R1_mild_tracking_error",
            "R2_sparse_plus_residual_bump",
            "S1_dense_clean",
            "S3_outlier_partials",
        )
    )
    targeted_ok = max(r1_improvement, r2_improvement) >= 5.0
    clean_ok = s1_delta <= 0.02 and s3_delta <= 0.02
    runtime_ok = runtime["ratio"] <= 2.5
    overall_ok = targeted_ok and monotonic_ok and clean_ok and runtime_ok

    print("\nSummary")
    print(f"  R1 improvement: {r1_improvement:.2f}%")
    print(f"  R2 improvement: {r2_improvement:.2f}%")
    print(f"  S1 delta: {s1_delta:+.6f} dB")
    print(f"  S3 delta: {s3_delta:+.6f} dB")
    print(f"  Monotonic loss: {'pass' if monotonic_ok else 'fail'}")
    print(f"  Runtime budget (<= 2.5x): {'pass' if runtime_ok else 'fail'}")
    print(f"  Stop/go: {'pass' if overall_ok else 'fail'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

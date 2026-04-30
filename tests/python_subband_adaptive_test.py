#!/usr/bin/env python3
"""Focused tests for the Phase 4 Task 4.3 sub-band adaptive prototype."""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from phase4_task43_fixtures import (
    SUBBAND_OPTIONS,
    boundary_sweep_metrics,
    fixture_b1_low_dense_high_sparse,
    fixture_b2_bright_high_cluster,
    fixture_s1_dense_harmonic,
    fixture_s5_inharmonic,
    run_fixture,
)

_FAILURES: list[str] = []
_PASSED = 0


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


def test_dense_frames_fall_back_to_global_model() -> None:
    partials, truth_fn = fixture_s1_dense_harmonic()
    result = run_fixture(partials, truth_fn)
    diag = result["subband_diag"]
    _assert(
        "dense_global_noop_reason",
        not diag.get("subband_applied", True) and diag.get("subband_reason") == "dense_global_noop",
        f"expected dense_global_noop, got {diag.get('subband_reason')}",
    )
    _assert(
        "dense_global_noop_rms_match",
        abs(result["subband_rms_db"] - result["global_rms_db"]) <= 1.0e-9,
        f"expected identical RMS, got global={result['global_rms_db']:.9f} subband={result['subband_rms_db']:.9f}",
    )


def test_b1_mixed_density_improves_with_clean_seams() -> None:
    partials, truth_fn = fixture_b1_low_dense_high_sparse()
    result = run_fixture(partials, truth_fn)
    diag = result["subband_diag"]
    improvement = (result["global_rms_db"] - result["subband_rms_db"]) / result["global_rms_db"] * 100.0
    _assert(
        "b1_subband_applied",
        diag.get("subband_applied", False),
        "expected sub-band path to run on B1",
    )
    _assert(
        "b1_improves_by_eight_percent",
        improvement >= 8.0,
        f"expected >= 8% improvement, got {improvement:.2f}%",
    )
    _assert(
        "b1_seams_under_threshold",
        diag["band_seam_rms_db"] <= diag["band_seam_rms_threshold_db"]
        and diag["band_seam_max_db"] <= diag["band_seam_max_threshold_db"],
        f"seams too large: rms={diag['band_seam_rms_db']:.3f}, max={diag['band_seam_max_db']:.3f}",
    )


def test_b2_bright_cluster_seams_stay_below_threshold() -> None:
    partials, truth_fn = fixture_b2_bright_high_cluster()
    result = run_fixture(partials, truth_fn)
    diag = result["subband_diag"]
    _assert(
        "b2_subband_applied",
        diag.get("subband_applied", False),
        "expected sub-band path to run on B2",
    )
    _assert(
        "b2_seam_rms_threshold",
        diag["band_seam_rms_db"] <= diag["band_seam_rms_threshold_db"],
        f"expected seam RMS <= threshold, got {diag['band_seam_rms_db']:.3f}",
    )
    _assert(
        "b2_seam_max_threshold",
        diag["band_seam_max_db"] <= diag["band_seam_max_threshold_db"],
        f"expected seam max <= threshold, got {diag['band_seam_max_db']:.3f}",
    )


def test_sparse_inharmonic_uses_local_band_orders() -> None:
    partials, truth_fn = fixture_s5_inharmonic()
    result = run_fixture(partials, truth_fn)
    diag = result["subband_diag"]
    orders = [band["effective_order"] for band in diag.get("subbands", [])]
    _assert(
        "s5_subband_applied",
        diag.get("subband_applied", False),
        "expected sub-band path to run on S5",
    )
    _assert(
        "s5_improves_over_global",
        result["subband_rms_db"] < result["global_rms_db"],
        f"expected S5 improvement, got global={result['global_rms_db']:.6f} subband={result['subband_rms_db']:.6f}",
    )
    _assert(
        "s5_band_orders_are_local",
        len(set(orders)) >= 2 and min(orders) == 0,
        f"expected varied local orders including 0-order band, got {orders}",
    )


def test_boundary_sweep_reports_gated_frames() -> None:
    sweep = boundary_sweep_metrics()
    anchors = [frame_diag["subband_global_anchor_weight"] for frame_diag in sweep["frame_diags"]]
    seam_values = [frame_diag["band_seam_rms_db"] for frame_diag in sweep["frame_diags"]]
    _assert(
        "boundary_zipper_metrics_finite",
        math.isfinite(sweep["zipper_rms_db"]) and math.isfinite(sweep["zipper_max_db"]),
        f"expected finite zipper metrics, got rms={sweep['zipper_rms_db']} max={sweep['zipper_max_db']}",
    )
    _assert(
        "boundary_high_seam_frames_use_anchor",
        max(anchors) >= 0.69 and max(seam_values) > SUBBAND_OPTIONS.seam_rms_threshold_db,
        f"expected strong global anchor on high-seam frames, got anchors={anchors} seams={seam_values}",
    )


def main() -> int:
    print("Running Python sub-band adaptive tests …")
    test_dense_frames_fall_back_to_global_model()
    test_b1_mixed_density_improves_with_clean_seams()
    test_b2_bright_cluster_seams_stay_below_threshold()
    test_sparse_inharmonic_uses_local_band_orders()
    test_boundary_sweep_reports_gated_frames()

    print(f"\n{_PASSED} passed, {len(_FAILURES)} failed.")
    if _FAILURES:
        for failure in _FAILURES:
            print(f"  FAIL: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Task 4.3 benchmark: sub-band adaptive cepstrum on mixed-density fixtures."""

from __future__ import annotations

from phase4_task43_fixtures import (
    boundary_sweep_metrics,
    fixture_b1_low_dense_high_sparse,
    fixture_b2_bright_high_cluster,
    fixture_s1_dense_harmonic,
    fixture_s2_sparse_guitar,
    fixture_s5_inharmonic,
    run_fixture,
)


def format_delta(global_rms_db: float, subband_rms_db: float) -> str:
    if global_rms_db <= 1.0e-9:
        return "n/a"
    percent = (global_rms_db - subband_rms_db) / global_rms_db * 100.0
    return f"{percent:.2f}%"


def print_fixture(label: str, result: dict, gate: str) -> None:
    diag = result["subband_diag"]
    print(
        f"{label:<28} "
        f"global={result['global_rms_db']:.6f} dB  "
        f"subband={result['subband_rms_db']:.6f} dB  "
        f"delta={format_delta(result['global_rms_db'], result['subband_rms_db']):>8}  "
        f"gate={gate}"
    )
    if diag.get("subband_applied"):
        print(
            f"  seams: rms={diag['band_seam_rms_db']:.6f} dB  "
            f"max={diag['band_seam_max_db']:.6f} dB  "
            f"anchor={diag['subband_global_anchor_weight']:.3f}"
        )
        print(
            "  bands: "
            + ", ".join(
                f"{band['label']} pts={band['point_count']} order={band['effective_order']} "
                f"lambda={band['regularization']:.6f}"
                for band in diag["subbands"]
            )
        )
    else:
        print(f"  subband path not applied: {diag.get('subband_reason', 'unknown')}")


def main() -> int:
    fixtures = [
        ("S1 dense harmonic", fixture_s1_dense_harmonic(), "regress < 0.03 dB"),
        ("S2 sparse guitar", fixture_s2_sparse_guitar(), "improve by 5-10%"),
        ("S5 inharmonic", fixture_s5_inharmonic(), "improve by 10-20%"),
        ("B1 low-dense/high-sparse", fixture_b1_low_dense_high_sparse(), "improve by >= 8%"),
        ("B2 bright high cluster", fixture_b2_bright_high_cluster(), "seams below threshold"),
    ]

    print("Running Task 4.3 benchmark …\n")
    results = []
    for label, (partials, truth_fn), gate in fixtures:
        result = run_fixture(partials, truth_fn)
        print_fixture(label, result, gate)
        results.append((label, result))

    sweep = boundary_sweep_metrics()
    sweep_seams = [
        (frame_diag["band_seam_rms_db"], frame_diag["band_seam_max_db"], frame_diag["subband_global_anchor_weight"])
        for frame_diag in sweep["frame_diags"]
    ]
    print("\nBoundary sweep")
    print(
        f"  zipper_rms={sweep['zipper_rms_db']:.6f} dB  "
        f"zipper_max={sweep['zipper_max_db']:.6f} dB"
    )
    print(
        "  frame seams: "
        + ", ".join(
            f"rms={rms_db:.3f}/max={max_db:.3f}/anchor={anchor:.3f}"
            for rms_db, max_db, anchor in sweep_seams
        )
    )

    s1_result = next(result for label, result in results if label == "S1 dense harmonic")
    s2_result = next(result for label, result in results if label == "S2 sparse guitar")
    s5_result = next(result for label, result in results if label == "S5 inharmonic")
    b1_result = next(result for label, result in results if label == "B1 low-dense/high-sparse")
    b2_result = next(result for label, result in results if label == "B2 bright high cluster")

    s1_regression = s1_result["subband_rms_db"] - s1_result["global_rms_db"]
    s2_delta = (s2_result["global_rms_db"] - s2_result["subband_rms_db"]) / s2_result["global_rms_db"] * 100.0
    s5_delta = (s5_result["global_rms_db"] - s5_result["subband_rms_db"]) / s5_result["global_rms_db"] * 100.0
    b1_delta = (b1_result["global_rms_db"] - b1_result["subband_rms_db"]) / b1_result["global_rms_db"] * 100.0

    seam_ok = (
        b2_result["subband_diag"]["band_seam_rms_db"] <= b2_result["subband_diag"]["band_seam_rms_threshold_db"]
        and b2_result["subband_diag"]["band_seam_max_db"] <= b2_result["subband_diag"]["band_seam_max_threshold_db"]
    )
    zipper_ok = sweep["zipper_rms_db"] <= 0.75 and sweep["zipper_max_db"] <= 1.80
    overall_ok = (
        s1_regression <= 0.03
        and s5_delta >= 10.0
        and b1_delta >= 8.0
        and seam_ok
        and zipper_ok
        and s2_delta >= 5.0
    )

    print("\nSummary")
    print(f"  S1 regression: {s1_regression:+.6f} dB")
    print(f"  S2 improvement: {s2_delta:.2f}%")
    print(f"  S5 improvement: {s5_delta:.2f}%")
    print(f"  B1 improvement: {b1_delta:.2f}%")
    print(f"  B2 seam gate: {'pass' if seam_ok else 'fail'}")
    print(f"  Boundary sweep gate: {'pass' if zipper_ok else 'fail'}")
    print(f"  Stop/go: {'pass' if overall_ok else 'fail'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

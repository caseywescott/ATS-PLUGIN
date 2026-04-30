#!/usr/bin/env python3
"""Benchmark GCV lambda selection on the committed ATSA reference fixtures.

This script exists for Commit A of the GCV lambda plan:
  - verify the synthetic acceptance scenarios,
  - log deterministic GCV lambda profiles for the four ATSA reference fixtures,
  - compare opt-in GCV against Bark and fixed-lambda New RDC on the same frames.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import math
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from visualize_envelopes import fit_new_model, rdc_fit_and_eval, run_frame_dump


SR = 48000.0
LO = 80.0
HI = 16000.0


def _smooth_env(freqs: list[float]) -> list[float]:
    return [-6.0 * math.log10(max(f, 1.0) / 1000.0) ** 2 for f in freqs]


def _make_partials(freqs_hz: list[float], amps_db: list[float]) -> list[list[float]]:
    return [[f, 10.0 ** (db / 20.0), 10.0] for f, db in zip(freqs_hz, amps_db)]


def _fmt(value: float | int) -> str:
    if isinstance(value, int):
        return str(value)
    return format(float(value), ".12g")


def _median(values: list[float]) -> float:
    if not values:
        return float("nan")
    return statistics.median(values)


def synthetic_acceptance(order: int, regularization: float) -> dict[str, float]:
    noisy_freqs = [120.0 + 620.0 * i for i in range(10)]
    noisy_clean = _smooth_env(noisy_freqs)
    noisy_frame = [db + (8.0 if i % 2 == 0 else -8.0) for i, db in enumerate(noisy_clean)]
    _, _, noisy_diag, _ = fit_new_model(
        _make_partials(noisy_freqs, noisy_frame),
        SR,
        LO,
        HI,
        order=order,
        regularization=regularization,
        use_gcv=True,
    )

    dense_freqs = [120.0 + 420.0 * i for i in range(30)]
    dense_clean = _smooth_env(dense_freqs)
    _, _, dense_diag, _ = fit_new_model(
        _make_partials(dense_freqs, dense_clean),
        SR,
        LO,
        HI,
        order=order,
        regularization=regularization,
        use_gcv=True,
    )

    return {
        "noisy_10_lambda": float(noisy_diag.get("gcv_lambda") or 0.0),
        "dense_30_lambda": float(dense_diag.get("gcv_lambda") or 0.0),
    }


def analyze_fixture(
    wav_path: Path,
    ats_offline: str,
    cache_dir: Path,
    analysis_type: int,
    order: int,
    regularization: float,
) -> dict[str, object]:
    cache_path = cache_dir / f"{wav_path.stem}_frames.json"
    with contextlib.redirect_stdout(io.StringIO()):
        dump = run_frame_dump(wav_path, ats_offline, analysis_type, str(cache_path))

    sample_rate = float(dump["sample_rate"])
    lowest_freq_hz = float(dump.get("lowest_freq_hz", 20.0))
    highest_freq_hz = float(dump.get("highest_freq_hz", sample_rate * 0.5))

    previous_fixed = None
    previous_gcv = None
    lambda_profile: list[float] = []
    fixed_beats_bark = 0
    gcv_beats_bark = 0
    gcv_beats_fixed = 0

    for frame in dump["frame_data"]:
        partials = frame["partials"]

        _, _, bark_diag = rdc_fit_and_eval(
            partials,
            sample_rate,
            lowest_freq_hz,
            highest_freq_hz,
            order=order,
            regularization=regularization,
            use_bark=True,
        )
        _, _, fixed_diag, previous_fixed = fit_new_model(
            partials,
            sample_rate,
            lowest_freq_hz,
            highest_freq_hz,
            order=order,
            regularization=regularization,
            previous_model=previous_fixed,
        )
        _, _, gcv_diag, previous_gcv = fit_new_model(
            partials,
            sample_rate,
            lowest_freq_hz,
            highest_freq_hz,
            order=order,
            regularization=regularization,
            previous_model=previous_gcv,
            use_gcv=True,
        )

        bark_err = float(bark_diag.get("weighted_error_db", float("inf")))
        fixed_err = float(fixed_diag.get("weighted_error_db", float("inf")))
        gcv_err = float(gcv_diag.get("weighted_error_db", float("inf")))

        if fixed_err < bark_err:
            fixed_beats_bark += 1
        if gcv_err < bark_err:
            gcv_beats_bark += 1
        if gcv_err < fixed_err:
            gcv_beats_fixed += 1

        lam = gcv_diag.get("gcv_lambda")
        if lam is not None:
            lambda_profile.append(float(lam))

    total_frames = len(dump["frame_data"])
    max_lambda_hits = sum(1 for lam in lambda_profile if abs(lam - 100.0) < 1.0e-12)
    return {
        "fixture_id": wav_path.stem,
        "frames": total_frames,
        "fixed_beats_bark": fixed_beats_bark,
        "gcv_beats_bark": gcv_beats_bark,
        "gcv_beats_fixed": gcv_beats_fixed,
        "lambda_profile": lambda_profile,
        "lambda_min": min(lambda_profile) if lambda_profile else float("nan"),
        "lambda_median": _median(lambda_profile),
        "lambda_mean": statistics.fmean(lambda_profile) if lambda_profile else float("nan"),
        "lambda_max": max(lambda_profile) if lambda_profile else float("nan"),
        "lambda_max_hits": max_lambda_hits,
    }


def print_text(synthetic: dict[str, float], fixtures: list[dict[str, object]]) -> None:
    print("Synthetic acceptance")
    print(
        f"  noisy_10_lambda={_fmt(synthetic['noisy_10_lambda'])} "
        f"(gate: >= 1e-3)"
    )
    print(
        f"  dense_30_lambda={_fmt(synthetic['dense_30_lambda'])} "
        f"(gate: <= 1e-3)"
    )
    print()

    overall_frames = sum(int(entry["frames"]) for entry in fixtures)
    overall_fixed = sum(int(entry["fixed_beats_bark"]) for entry in fixtures)
    overall_gcv_bark = sum(int(entry["gcv_beats_bark"]) for entry in fixtures)
    overall_gcv_fixed = sum(int(entry["gcv_beats_fixed"]) for entry in fixtures)

    print("ATSA reference fixture summary")
    for entry in fixtures:
        frames = int(entry["frames"])
        print(
            f"  {entry['fixture_id']}: frames={frames} "
            f"fixed<bark={entry['fixed_beats_bark']}/{frames} "
            f"gcv<bark={entry['gcv_beats_bark']}/{frames} "
            f"gcv<fixed={entry['gcv_beats_fixed']}/{frames} "
            f"lambda[min/median/mean/max]={_fmt(entry['lambda_min'])}/"
            f"{_fmt(entry['lambda_median'])}/{_fmt(entry['lambda_mean'])}/"
            f"{_fmt(entry['lambda_max'])} "
            f"max_hits={entry['lambda_max_hits']}"
        )
    print(
        f"  OVERALL: frames={overall_frames} "
        f"fixed<bark={overall_fixed}/{overall_frames} ({overall_fixed / overall_frames:.1%}) "
        f"gcv<bark={overall_gcv_bark}/{overall_frames} ({overall_gcv_bark / overall_frames:.1%}) "
        f"gcv<fixed={overall_gcv_fixed}/{overall_frames} ({overall_gcv_fixed / overall_frames:.1%})"
    )


def print_markdown(synthetic: dict[str, float], fixtures: list[dict[str, object]]) -> None:
    overall_frames = sum(int(entry["frames"]) for entry in fixtures)
    overall_fixed = sum(int(entry["fixed_beats_bark"]) for entry in fixtures)
    overall_gcv_bark = sum(int(entry["gcv_beats_bark"]) for entry in fixtures)
    overall_gcv_fixed = sum(int(entry["gcv_beats_fixed"]) for entry in fixtures)

    print("# GCV Lambda Baselines")
    print()
    print("## Synthetic Acceptance")
    print()
    print("| Scenario | Selected lambda | Gate | Result |")
    print("| --- | ---: | --- | --- |")
    noisy_ok = synthetic["noisy_10_lambda"] >= 1.0e-3
    dense_ok = synthetic["dense_30_lambda"] <= 1.0e-3 + 1.0e-12
    print(
        f"| Noisy 10-partial synthetic | `{_fmt(synthetic['noisy_10_lambda'])}` | `>= 1e-3` | "
        f"{'pass' if noisy_ok else 'fail'} |"
    )
    print(
        f"| Dense 30-partial synthetic | `{_fmt(synthetic['dense_30_lambda'])}` | `<= 1e-3` | "
        f"{'pass' if dense_ok else 'fail'} |"
    )
    print()
    print("## ATSA Reference Fixture Summary")
    print()
    print("| Fixture | Frames | Fixed < Bark | GCV < Bark | GCV < Fixed | Lambda min | Lambda median | Lambda mean | Lambda max | Max-lambda hits |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for entry in fixtures:
        frames = int(entry["frames"])
        print(
            f"| `{entry['fixture_id']}` | {frames} | "
            f"{entry['fixed_beats_bark']}/{frames} | "
            f"{entry['gcv_beats_bark']}/{frames} | "
            f"{entry['gcv_beats_fixed']}/{frames} | "
            f"`{_fmt(entry['lambda_min'])}` | "
            f"`{_fmt(entry['lambda_median'])}` | "
            f"`{_fmt(entry['lambda_mean'])}` | "
            f"`{_fmt(entry['lambda_max'])}` | "
            f"{entry['lambda_max_hits']} |"
        )
    print(
        f"| `OVERALL` | {overall_frames} | {overall_fixed}/{overall_frames} | "
        f"{overall_gcv_bark}/{overall_frames} | {overall_gcv_fixed}/{overall_frames} | - | - | - | - | - |"
    )
    print()
    print("## Frame Lambda Profiles")
    print()
    for entry in fixtures:
        pairs = ", ".join(
            f"{index}:{_fmt(value)}" for index, value in enumerate(entry["lambda_profile"])
        )
        print(f"### `{entry['fixture_id']}`")
        print()
        print("```text")
        print(pairs)
        print("```")
        print()


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark GCV lambda profiles on reference fixtures.")
    parser.add_argument(
        "--fixtures-dir",
        default="tests/fixtures/atsa_references",
        help="Directory containing committed *_resynth.wav reference fixtures.",
    )
    parser.add_argument(
        "--ats-offline",
        default="./build/tools/ats_offline",
        help="Path to ats_offline (default: ./build/tools/ats_offline).",
    )
    parser.add_argument(
        "--cache-dir",
        default="build/gcv-benchmark-cache",
        help="Directory for cached frame dumps.",
    )
    parser.add_argument("--type", dest="analysis_type", type=int, default=4, choices=[1, 2, 3, 4])
    parser.add_argument("--order", type=int, default=16)
    parser.add_argument("--reg", type=float, default=1.0e-3)
    parser.add_argument("--markdown", action="store_true", default=False,
                        help="Emit markdown suitable for docs/SPECTRAL_ENVELOPE_BASELINES_GCV.md.")
    args = parser.parse_args()

    ats_offline = Path(args.ats_offline)
    if not ats_offline.is_file():
        print(f"error: ats_offline not found: {ats_offline}", file=sys.stderr)
        return 1

    fixtures_dir = Path(args.fixtures_dir)
    fixtures = sorted(fixtures_dir.glob("*_resynth.wav"))
    if len(fixtures) != 4:
        print(f"error: expected 4 reference fixtures in {fixtures_dir}, found {len(fixtures)}", file=sys.stderr)
        return 1

    cache_dir = Path(args.cache_dir)
    cache_dir.mkdir(parents=True, exist_ok=True)

    synthetic = synthetic_acceptance(args.order, args.reg)
    entries = [
        analyze_fixture(path, str(ats_offline), cache_dir, args.analysis_type, args.order, args.reg)
        for path in fixtures
    ]

    if args.markdown:
        print_markdown(synthetic, entries)
    else:
        print_text(synthetic, entries)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

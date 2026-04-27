#!/usr/bin/env python3
"""Batch spectral-envelope comparison plots for fixture WAVs.

Generates a per-fixture PNG that overlays:
  - legacy linear-Hz RDC from tools/visualize_envelopes.py
  - legacy Bark-warped RDC from tools/visualize_envelopes.py
  - the current branch RDC model: Bark fit axis + Bark-space curvature prior
    + adjacent-frame low-weight virtual gap points

The input WAVs are analyzed with ats_offline --dump-frames. A representative
frame is selected per fixture by the largest mean absolute deviation between
the new model and the legacy Bark overlay, which tends to surface the frames
where the branch change is most visible.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import tempfile
from pathlib import Path
from typing import Any

import numpy as np

from visualize_envelopes import (
    _EnvelopeModel as EnvelopeModel,
    fit_new_model,
    rdc_fit_and_eval,
    run_frame_dump,
)


def _set_mpl_env() -> None:
    cache_dir = Path(tempfile.gettempdir()) / "codex-matplotlib"
    cache_dir.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(cache_dir))


def _mean_abs_delta(a: list[float], b: list[float]) -> float:
    if not a or not b or len(a) != len(b):
        return 0.0
    return float(np.mean(np.abs(np.asarray(a) - np.asarray(b))))


def _plot_fixture(result: dict[str, Any], output_path: Path) -> None:
    _set_mpl_env()
    import matplotlib.pyplot as plt

    figure = plt.figure(figsize=(12, 7))
    axis = figure.add_axes([0.08, 0.14, 0.88, 0.78])
    axis.set_xscale("log")
    axis.set_xlim(result["lowest_freq_hz"], result["highest_freq_hz"])
    axis.set_xlabel("Frequency (Hz)")
    axis.set_ylabel("Amplitude (dB)")
    axis.grid(True, which="both", alpha=0.25)

    partial_freqs = result["partial_freqs_hz"]
    partial_dbs = result["partial_dbs"]
    axis.plot(partial_freqs, partial_dbs, "o", color="#4a9fd5", markersize=4, alpha=0.8, label="Partials")
    axis.plot(result["linear_grid_hz"], result["linear_grid_db"], "-", color="#e05c2a", linewidth=1.6,
              label="Legacy RDC (linear)")
    axis.plot(result["bark_grid_hz"], result["bark_grid_db"], "--", color="#2ab07a", linewidth=1.5,
              label="Legacy RDC (Bark)")
    axis.plot(result["new_grid_hz"], result["new_grid_db"], "-", color="#5a3fc0", linewidth=2.2,
              label="New RDC (Bark curvature + gap fill)")
    axis.legend(loc="upper right", fontsize=8)

    axis.set_title(
        f"{result['fixture_id']}  |  frame {result['frame_index']} at {result['time_s']:.4f}s\n"
        f"selected by max mean |new-Bark| across frames",
        fontsize=10,
    )

    all_values = partial_dbs + result["linear_grid_db"] + result["bark_grid_db"] + result["new_grid_db"]
    if all_values:
        axis.set_ylim(min(all_values) - 4.0, max(all_values) + 4.0)

    info = (
        f"Active partials: {len(partial_freqs)}\n"
        f"Legacy linear: err={result['linear_diag']['weighted_error_db']:.2f} dB, "
        f"cond={result['linear_diag']['condition_estimate']:.2e}, "
        f"fallback={result['linear_diag'].get('fallback') or 'none'}\n"
        f"Legacy Bark: err={result['bark_diag']['weighted_error_db']:.2f} dB, "
        f"cond={result['bark_diag']['condition_estimate']:.2e}, "
        f"fallback={result['bark_diag'].get('fallback') or 'none'}\n"
        f"New model: err={result['new_diag']['weighted_error_db']:.2f} dB, "
        f"cond={result['new_diag']['condition_estimate']:.2e}, "
        f"virtual_pts={result['new_diag']['virtual_points']}, "
        f"fallback={result['new_diag'].get('fallback') or 'none'}\n"
        f"mean |new-linear|={result['delta_new_linear_db']:.2f} dB, "
        f"mean |new-Bark|={result['delta_new_bark_db']:.2f} dB"
    )
    axis.text(0.01, 0.01, info, transform=axis.transAxes, va="bottom", fontsize=8, family="monospace")

    figure.savefig(output_path, dpi=160)
    plt.close(figure)


def analyze_fixture(wav_path: Path,
                    ats_offline: str,
                    analysis_type: int,
                    order: int,
                    regularization: float,
                    cache_dir: Path) -> dict[str, Any]:
    cache_path = cache_dir / f"{wav_path.stem}_frames.json"
    dump = run_frame_dump(wav_path, ats_offline, analysis_type, str(cache_path))
    frame_data = dump["frame_data"]
    sample_rate = float(dump["sample_rate"])
    lowest_freq_hz = float(dump.get("lowest_freq_hz", 20.0))
    highest_freq_hz = float(dump.get("highest_freq_hz", sample_rate * 0.5))

    previous_model: EnvelopeModel | None = None
    frames: list[dict[str, Any]] = []
    for frame_index, frame in enumerate(frame_data):
        linear_grid_hz, linear_grid_db, linear_diag = rdc_fit_and_eval(
            frame["partials"],
            sample_rate,
            lowest_freq_hz,
            highest_freq_hz,
            order=order,
            regularization=regularization,
            use_bark=False,
        )
        bark_grid_hz, bark_grid_db, bark_diag = rdc_fit_and_eval(
            frame["partials"],
            sample_rate,
            lowest_freq_hz,
            highest_freq_hz,
            order=order,
            regularization=regularization,
            use_bark=True,
        )
        new_grid_hz, new_grid_db, new_diag, previous_model = fit_new_model(
            frame["partials"],
            sample_rate,
            lowest_freq_hz,
            highest_freq_hz,
            order=order,
            regularization=regularization,
            previous_model=previous_model,
        )

        partial_freqs = [float(entry[0]) for entry in frame["partials"] if float(entry[1]) > 0.0]
        partial_dbs = [20.0 * math.log10(max(float(entry[1]), 1.0e-12))
                       for entry in frame["partials"] if float(entry[1]) > 0.0]
        delta_new_bark = _mean_abs_delta(new_grid_db, bark_grid_db)
        delta_new_linear = _mean_abs_delta(new_grid_db, linear_grid_db)
        frames.append({
            "fixture_id": wav_path.stem,
            "frame_index": frame_index,
            "time_s": float(frame["time_s"]),
            "partial_freqs_hz": partial_freqs,
            "partial_dbs": partial_dbs,
            "linear_grid_hz": linear_grid_hz,
            "linear_grid_db": linear_grid_db,
            "bark_grid_hz": bark_grid_hz,
            "bark_grid_db": bark_grid_db,
            "new_grid_hz": new_grid_hz,
            "new_grid_db": new_grid_db,
            "linear_diag": linear_diag,
            "bark_diag": bark_diag,
            "new_diag": new_diag,
            "delta_new_bark_db": delta_new_bark,
            "delta_new_linear_db": delta_new_linear,
            "lowest_freq_hz": lowest_freq_hz,
            "highest_freq_hz": highest_freq_hz,
        })

    selected = max(frames, key=lambda frame: (frame["delta_new_bark_db"], len(frame["partial_freqs_hz"])))
    return {
        "fixture_id": wav_path.stem,
        "frames": len(frame_data),
        "sample_rate": sample_rate,
        "selected_frame": selected,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Batch comparison plots for spectral-envelope fixtures.")
    parser.add_argument("wavs", nargs="+", help="Input WAV fixtures to analyze.")
    parser.add_argument("--ats-offline", default="./build/tools/ats_offline",
                        help="Path to ats_offline (default: ./build/tools/ats_offline).")
    parser.add_argument("--out-dir", default="build/envelope-comparison-plots",
                        help="Directory for PNGs and summary JSON.")
    parser.add_argument("--type", dest="analysis_type", type=int, default=4, choices=[1, 2, 3, 4],
                        help="ATS analysis type (default: 4).")
    parser.add_argument("--order", type=int, default=16, help="Cepstrum order for all fits.")
    parser.add_argument("--reg", type=float, default=1.0e-3, help="Base regularization.")
    args = parser.parse_args()

    ats_offline = Path(args.ats_offline)
    if not ats_offline.is_file():
        raise SystemExit(f"ats_offline not found: {ats_offline}")

    out_dir = Path(args.out_dir)
    cache_dir = out_dir / "cache"
    out_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)

    summary: dict[str, Any] = {"fixtures": []}
    for wav in args.wavs:
        wav_path = Path(wav)
        if not wav_path.is_file():
            raise SystemExit(f"WAV not found: {wav_path}")

        fixture = analyze_fixture(wav_path,
                                  str(ats_offline),
                                  args.analysis_type,
                                  args.order,
                                  args.reg,
                                  cache_dir)
        selected = fixture["selected_frame"]
        png_path = out_dir / f"{fixture['fixture_id']}_frame{selected['frame_index']:04d}.png"
        _plot_fixture(selected, png_path)
        fixture["selected_frame_png"] = str(png_path)
        summary["fixtures"].append({
            "fixture_id": fixture["fixture_id"],
            "frames": fixture["frames"],
            "sample_rate": fixture["sample_rate"],
            "selected_frame": {
                "frame_index": selected["frame_index"],
                "time_s": selected["time_s"],
                "active_partials": len(selected["partial_freqs_hz"]),
                "delta_new_bark_db": selected["delta_new_bark_db"],
                "delta_new_linear_db": selected["delta_new_linear_db"],
                "linear_diag": selected["linear_diag"],
                "bark_diag": selected["bark_diag"],
                "new_diag": selected["new_diag"],
                "png": str(png_path),
            },
        })
        print(
            f"{fixture['fixture_id']}: frame {selected['frame_index']} at {selected['time_s']:.4f}s | "
            f"mean |new-Bark|={selected['delta_new_bark_db']:.2f} dB | "
            f"mean |new-linear|={selected['delta_new_linear_db']:.2f} dB | "
            f"png={png_path}"
        )

    summary_path = out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

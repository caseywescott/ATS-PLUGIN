#!/usr/bin/env python3
"""Batch spectral-envelope comparison plots for fixture WAVs.

Generates a per-fixture PNG that overlays three models (default):
  1. Legacy RDC (linear Hz) — orange solid
  2. Legacy RDC (Bark) — green dashed
  3. New RDC (Bark + curvature prior + BIC + IRLS + gap fill) — purple bold

Pass --gcv to also overlay the GCV-regularized variant (cyan bold dotted).
GCV is opt-in because the current ATSA reference baseline still favors the
fixed-λ New RDC path overall: GCV beats Bark on 43.7% of frames versus 55.2%
for fixed-λ New RDC. See docs/SPECTRAL_ENVELOPE_BASELINES_GCV.md.

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

    show_gcv = bool(result.get("gcv_grid_hz"))

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
              label=f"Linear RDC  err={result['linear_diag']['weighted_error_db']:.2f} dB")
    axis.plot(result["bark_grid_hz"], result["bark_grid_db"], "--", color="#2ab07a", linewidth=1.5,
              label=f"Bark RDC (legacy)  err={result['bark_diag']['weighted_error_db']:.2f} dB")
    axis.plot(result["new_grid_hz"], result["new_grid_db"], "-", color="#5a3fc0", linewidth=2.2,
              label=f"New RDC (IRLS+BIC+fill)  err={result['new_diag']['weighted_error_db']:.2f} dB")
    if show_gcv:
        gd = result["gcv_diag"]
        axis.plot(result["gcv_grid_hz"], result["gcv_grid_db"], "-.", color="#00bcd4", linewidth=2.0,
                  label=f"New RDC + GCV λ={gd.get('gcv_lambda', 0.0):.2e}  "
                        f"err={gd['weighted_error_db']:.2f} dB")
    axis.legend(loc="upper right", fontsize=8)

    axis.set_title(
        f"{result['fixture_id']}  |  frame {result['frame_index']} at {result['time_s']:.4f}s\n"
        f"selected by max mean |new-Bark| across frames  |  {len(partial_freqs)} active partials",
        fontsize=10,
    )

    all_values = (partial_dbs + result["linear_grid_db"] + result["bark_grid_db"]
                  + result["new_grid_db"])
    if show_gcv:
        all_values = all_values + result["gcv_grid_db"]
    if all_values:
        axis.set_ylim(min(all_values) - 4.0, max(all_values) + 4.0)

    nd = result["new_diag"]
    info = (
        f"Linear RDC :  err={result['linear_diag']['weighted_error_db']:.2f} dB  "
        f"cond={result['linear_diag']['condition_estimate']:.1e}  "
        f"fallback={result['linear_diag'].get('fallback') or 'none'}\n"
        f"Bark RDC   :  err={result['bark_diag']['weighted_error_db']:.2f} dB  "
        f"cond={result['bark_diag']['condition_estimate']:.1e}  "
        f"fallback={result['bark_diag'].get('fallback') or 'none'}\n"
        f"New RDC    :  err={nd['weighted_error_db']:.2f} dB  "
        f"cond={nd['condition_estimate']:.1e}  virt={nd['virtual_points']}  "
        f"order={nd['effective_order']}  fallback={nd.get('fallback') or 'none'}"
    )
    if show_gcv:
        gd = result["gcv_diag"]
        gcv_lam_str = f"{gd.get('gcv_lambda', 0.0):.2e}" if gd.get("gcv_lambda") else "N/A"
        info += (
            f"\nNew+GCV    :  err={gd['weighted_error_db']:.2f} dB  "
            f"cond={gd['condition_estimate']:.1e}  virt={gd['virtual_points']}  "
            f"order={gd['effective_order']}  λ={gcv_lam_str}  fallback={gd.get('fallback') or 'none'}"
        )
    axis.text(0.01, 0.01, info, transform=axis.transAxes, va="bottom", fontsize=7.5,
              family="monospace", bbox=dict(boxstyle="round,pad=0.3", fc="white", alpha=0.7))

    figure.savefig(output_path, dpi=160)
    plt.close(figure)


def analyze_fixture(wav_path: Path,
                    ats_offline: str,
                    analysis_type: int,
                    order: int,
                    regularization: float,
                    cache_dir: Path,
                    show_gcv: bool = False) -> dict[str, Any]:
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
        # GCV is opt-in (--gcv flag). The current ATSA reference baseline still
        # favors fixed-λ New RDC overall: 43.7% vs 55.2% frame-win rate against Bark.
        gcv_grid_hz, gcv_grid_db, gcv_diag = [], [], {}
        if show_gcv:
            gcv_grid_hz, gcv_grid_db, gcv_diag, _ = fit_new_model(
                frame["partials"],
                sample_rate,
                lowest_freq_hz,
                highest_freq_hz,
                order=order,
                regularization=regularization,
                previous_model=previous_model,
                use_gcv=True,
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
            "gcv_grid_hz": gcv_grid_hz,
            "gcv_grid_db": gcv_grid_db,
            "linear_diag": linear_diag,
            "bark_diag": bark_diag,
            "new_diag": new_diag,
            "gcv_diag": gcv_diag,
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
    parser.add_argument("--gcv", action="store_true", default=False,
                        help="Also overlay GCV-regularized New RDC (opt-in; default off). "
                             "The current ATSA baseline still favors fixed-lambda New RDC, "
                             "so New RDC without GCV remains the production default.")
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
                                  cache_dir,
                                  show_gcv=args.gcv)
        selected = fixture["selected_frame"]
        png_path = out_dir / f"{fixture['fixture_id']}_frame{selected['frame_index']:04d}.png"
        _plot_fixture(selected, png_path)
        fixture["selected_frame_png"] = str(png_path)
        selected_summary: dict[str, Any] = {
            "frame_index": selected["frame_index"],
            "time_s": selected["time_s"],
            "active_partials": len(selected["partial_freqs_hz"]),
            "delta_new_bark_db": selected["delta_new_bark_db"],
            "delta_new_linear_db": selected["delta_new_linear_db"],
            "linear_diag": selected["linear_diag"],
            "bark_diag": selected["bark_diag"],
            "new_diag": selected["new_diag"],
            "png": str(png_path),
        }
        if args.gcv:
            selected_summary["gcv_diag"] = selected["gcv_diag"]
        summary["fixtures"].append({
            "fixture_id": fixture["fixture_id"],
            "frames": fixture["frames"],
            "sample_rate": fixture["sample_rate"],
            "selected_frame": selected_summary,
        })
        ld = selected["linear_diag"]
        bd = selected["bark_diag"]
        nd = selected["new_diag"]
        print(
            f"\n{fixture['fixture_id']}\n"
            f"  frame {selected['frame_index']} @ {selected['time_s']:.4f}s  "
            f"partials={len(selected['partial_freqs_hz'])}\n"
            f"  Linear RDC  : err={ld['weighted_error_db']:.3f} dB  "
            f"cond={ld['condition_estimate']:.2e}  fallback={ld.get('fallback') or 'none'}\n"
            f"  Bark RDC    : err={bd['weighted_error_db']:.3f} dB  "
            f"cond={bd['condition_estimate']:.2e}  fallback={bd.get('fallback') or 'none'}\n"
            f"  New RDC     : err={nd['weighted_error_db']:.3f} dB  "
            f"cond={nd['condition_estimate']:.2e}  order={nd['effective_order']}  "
            f"virt={nd['virtual_points']}  fallback={nd.get('fallback') or 'none'}"
            + (
                f"\n  New+GCV     : err={selected['gcv_diag']['weighted_error_db']:.3f} dB  "
                f"cond={selected['gcv_diag']['condition_estimate']:.2e}  "
                f"order={selected['gcv_diag']['effective_order']}  "
                f"λ={selected['gcv_diag'].get('gcv_lambda', 0.0):.2e}  "
                f"fallback={selected['gcv_diag'].get('fallback') or 'none'}"
                if args.gcv else ""
            )
            + f"\n  png={png_path}"
        )

    summary_path = out_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

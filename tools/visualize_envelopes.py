#!/usr/bin/env python3
"""Interactive frame-by-frame spectral envelope visualizer for ATS analysis.

Runs ats_offline --dump-frames on a WAV file (converting to mono 16-bit first
if needed), then shows an interactive matplotlib browser with:

  - Partial scatter plot (freq Hz vs dB), dot size proportional to SMR weight.
  - RDC envelope curve (numpy re-implementation of the C++ RegularizedCepstrum
    estimator using the same linear-Hz cosine basis and second-derivative penalty).
  - Optional Bark-warped RDC envelope for comparison.
  - Frame navigation: ← → arrow keys, Page Up/Down (+10 frames), slider.

Usage:
  python3 tools/visualize_envelopes.py <input.wav> \\
      --ats-offline ./build/tools/ats_offline \\
      [--type 4] [--order 16] [--reg 1e-3] [--bark] [--cache <frames.json>]

Press Q or Escape to quit.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


# ---------------------------------------------------------------------------
# WAV conversion (stereo / 24-bit → mono 16-bit for ats_offline)
# ---------------------------------------------------------------------------

def convert_to_mono16(src: Path, dst: Path) -> None:
    try:
        import soundfile as sf  # type: ignore
    except ImportError:
        raise SystemExit("Install soundfile:  pip install soundfile")
    import struct as st
    data, sr = sf.read(str(src), dtype="float32", always_2d=True)
    mono = data.mean(axis=1).clip(-1.0, 1.0)
    samples_i16 = (mono * 32767.0).astype("int16")
    with wave.open(str(dst), "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(samples_i16.tobytes())


# ---------------------------------------------------------------------------
# RDC envelope estimator (numpy port of RegularizedCepstrum.cpp)
# ---------------------------------------------------------------------------

def _normalized_position_linear(freq_hz: float,
                                 min_hz: float, max_hz: float) -> float:
    """Linear-Hz normalised position in [0, 1]. Clamped."""
    return max(0.0, min(1.0, (freq_hz - min_hz) / (max_hz - min_hz)))


def _bark(f: float) -> float:
    return 6.0 * math.asinh(f / 600.0)


def _normalized_position_bark(freq_hz: float,
                               min_hz: float, max_hz: float) -> float:
    b_min = _bark(max(1.0, min_hz))
    b_max = _bark(max_hz)
    b_val = _bark(max(1.0, freq_hz))
    if b_max <= b_min:
        return 0.0
    return max(0.0, min(1.0, (b_val - b_min) / (b_max - b_min)))


def _build_basis_row(pos: float, order: int) -> list[float]:
    """Cosine basis: [1, 2cos(pi*pos), 2cos(2pi*pos), ...]"""
    theta = math.pi * pos
    row = [1.0]
    for k in range(1, order + 1):
        row.append(2.0 * math.cos(k * theta))
    return row


def rdc_fit_and_eval(
    frame_partials: list,   # list of [freq_hz, amplitude, smr]
    sample_rate: float,
    lowest_freq_hz: float,
    highest_freq_hz: float,
    order: int = 16,
    regularization: float = 1e-3,
    use_bark: bool = False,
    amp_floor: float = 1e-12,
    grid_size: int = 512,
) -> tuple[list[float], list[float], dict]:
    """Fit RDC envelope and evaluate on a log-spaced frequency grid.

    Returns (grid_freqs_hz, grid_env_db, diagnostics_dict).
    On failure returns empty lists and diagnostics with 'error' key.
    """
    import numpy as np

    min_hz = max(lowest_freq_hz, 1.0)
    max_hz = min(highest_freq_hz, sample_rate / 2.0)
    if max_hz <= min_hz:
        return [], [], {"error": "invalid frequency range"}

    def norm_pos(f: float) -> float:
        if use_bark:
            return _normalized_position_bark(f, min_hz, max_hz)
        return _normalized_position_linear(f, min_hz, max_hz)

    # Build weighted fit points from active partials.
    freqs, y_db, weights = [], [], []
    for entry in frame_partials:
        freq_hz, amplitude, smr = float(entry[0]), float(entry[1]), float(entry[2])
        if amplitude <= 0.0 or freq_hz <= 0.0:
            continue

        db = 20.0 * math.log10(max(amplitude, amp_floor))

        # Amplitude weight (normalised 0-1 from -120..0 dB range)
        amp_w = max(0.0, min(1.0, (db - (-120.0)) / 120.0))
        # SMR weight (normalised 0-1 from -10..30 dB range)
        smr_w = max(0.0, min(1.0, (smr - (-10.0)) / 40.0))
        w = max(1e-4, amp_w * smr_w)

        freqs.append(freq_hz)
        y_db.append(db)
        weights.append(w)

    n_pts = len(freqs)
    if n_pts == 0:
        return [], [], {"error": "no active partials", "fit_points": 0}

    # Reduce order for sparse frames (mirror C++ reduceOrderForSparseFrames).
    eff_order = order
    eff_reg = regularization
    fallback = None
    if n_pts <= eff_order:
        eff_order = max(0, n_pts - 1)
        eff_reg = max(eff_reg, 1e-2)
        fallback = "order_reduced_sparse"

    n_coeffs = eff_order + 1

    # Build B (n_pts × n_coeffs) and weight vector.
    B = np.array([_build_basis_row(norm_pos(f), eff_order) for f in freqs])
    W = np.array(weights)
    Y = np.array(y_db)

    # Second-derivative Tikhonov penalty: diagonal [0, 1^4, 2^4, 3^4, ...]
    D = np.diag([0.0] + [float(k) ** 4 for k in range(1, n_coeffs)])

    # Weighted normal equations: (B^T W B + reg * D) c = B^T W y
    BtW = (B * W[:, None]).T          # n_coeffs × n_pts
    A = BtW @ B + eff_reg * D
    rhs = BtW @ Y

    # Solve; fall back to well-conditioned constant if singular.
    try:
        c = np.linalg.solve(A, rhs)
    except np.linalg.LinAlgError:
        mean_db = float(np.average(Y, weights=W))
        c = np.array([mean_db] + [0.0] * eff_order)
        fallback = "constant_fallback"

    # Condition estimate: (max diag of Cholesky / min)^2 proxy via eigvals.
    try:
        eigvals = np.linalg.eigvalsh(A)
        cond = float(eigvals[-1] / max(eigvals[0], 1e-30))
    except Exception:
        cond = float("inf")

    # Weighted fit error.
    predicted = B @ c
    residuals = predicted - Y
    weighted_err = float(math.sqrt(np.sum(W * residuals ** 2) / max(W.sum(), 1e-30)))

    diag = {
        "fit_points": n_pts,
        "effective_order": eff_order,
        "weighted_error_db": weighted_err,
        "condition_estimate": cond,
        "fallback": fallback,
    }

    # Evaluate on log-spaced grid.
    grid_freqs = np.exp(
        np.linspace(math.log(min_hz), math.log(max_hz), grid_size)
    ).tolist()

    grid_db = []
    for gf in grid_freqs:
        pos = norm_pos(gf)
        basis = _build_basis_row(pos, eff_order)
        grid_db.append(float(np.dot(c, basis)))

    return grid_freqs, grid_db, diag


# ---------------------------------------------------------------------------
# Frame-dump runner
# ---------------------------------------------------------------------------

def run_frame_dump(wav_path: Path, ats_offline: str,
                   analysis_type: int, cache_path: str | None) -> dict:
    """Run ats_offline --dump-frames, optionally caching the JSON."""
    if cache_path and os.path.isfile(cache_path):
        print(f"  Loading cached frame dump: {cache_path}")
        with open(cache_path, "r") as f:
            return json.load(f)

    with tempfile.TemporaryDirectory() as tmp:
        mono_wav = Path(tmp) / "mono16.wav"
        try:
            convert_to_mono16(wav_path, mono_wav)
        except Exception as exc:
            raise SystemExit(f"Conversion to mono 16-bit failed: {exc}")

        dump_path = Path(tmp) / "frames.json"
        out_wav   = Path(tmp) / "resynth.wav"

        cmd = [
            ats_offline,
            "--type", str(analysis_type),
            "--dump-frames", str(dump_path),
            str(mono_wav),
            str(out_wav),
        ]
        print(f"  Running: {' '.join(cmd)}")
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

        with open(dump_path, "r") as f:
            data = json.load(f)

    if cache_path:
        with open(cache_path, "w") as f:
            json.dump(data, f)
        print(f"  Frame dump cached to: {cache_path}")

    return data


# ---------------------------------------------------------------------------
# Interactive matplotlib viewer
# ---------------------------------------------------------------------------

def run_viewer(dump: dict, wav_name: str,
               order: int, reg: float, show_bark: bool) -> None:
    try:
        import matplotlib.pyplot as plt
        import matplotlib.widgets as widgets
    except ImportError:
        raise SystemExit("Install matplotlib:  pip install matplotlib")

    frame_data   = dump["frame_data"]
    sample_rate  = float(dump["sample_rate"])
    lowest_freq  = float(dump.get("lowest_freq_hz", 20.0))
    highest_freq = float(dump.get("highest_freq_hz", sample_rate / 2.0))
    n_frames     = len(frame_data)

    if n_frames == 0:
        raise SystemExit("No frames in dump.")

    # Pre-compute envelopes for all frames (fast with numpy).
    print(f"  Pre-computing RDC envelopes for {n_frames} frames …", end="", flush=True)
    envelopes_lin  = []
    envelopes_bark = [] if show_bark else None
    diagnostics    = []

    for fd in frame_data:
        gf, gdb, diag = rdc_fit_and_eval(
            fd["partials"], sample_rate, lowest_freq, highest_freq,
            order=order, regularization=reg, use_bark=False
        )
        envelopes_lin.append((gf, gdb))
        diagnostics.append(diag)
        if show_bark:
            gf_b, gdb_b, _ = rdc_fit_and_eval(
                fd["partials"], sample_rate, lowest_freq, highest_freq,
                order=order, regularization=reg, use_bark=True
            )
            envelopes_bark.append((gf_b, gdb_b))

    print(" done.")

    # ---- Figure layout ----
    fig = plt.figure(figsize=(13, 7))
    fig.suptitle(f"{wav_name}  —  use ← → / PgUp PgDn to navigate, Q to quit",
                 fontsize=10)

    ax_main = fig.add_axes([0.07, 0.20, 0.88, 0.70])
    ax_slide = fig.add_axes([0.12, 0.06, 0.76, 0.04])

    ax_main.set_xlabel("Frequency (Hz)")
    ax_main.set_ylabel("Amplitude (dB)")
    ax_main.set_xscale("log")
    ax_main.set_xlim(max(lowest_freq, 20.0), highest_freq)
    ax_main.grid(True, which="both", alpha=0.3)

    # Compute global dB range for stable y-axis.
    all_dbs = []
    for fd in frame_data:
        for entry in fd["partials"]:
            a = float(entry[1])
            if a > 0.0:
                all_dbs.append(20.0 * math.log10(max(a, 1e-12)))
    y_min = min(all_dbs) - 6 if all_dbs else -100
    y_max = max(all_dbs) + 6 if all_dbs else 0
    ax_main.set_ylim(y_min, y_max)

    # Initial plot objects.
    scatter, = ax_main.plot([], [], "o", color="#4a9fd5", markersize=4,
                            alpha=0.8, label="Partials", zorder=3)
    env_line, = ax_main.plot([], [], "-", color="#e05c2a", linewidth=1.8,
                             alpha=0.9, label=f"RDC (linear, order={order})",
                             zorder=2)
    if show_bark:
        bark_line, = ax_main.plot([], [], "--", color="#2ab07a", linewidth=1.5,
                                  alpha=0.85, label=f"RDC (Bark, order={order})",
                                  zorder=2)
    else:
        bark_line = None

    ax_main.legend(loc="upper right", fontsize=8)
    info_text = ax_main.text(0.01, 0.97, "", transform=ax_main.transAxes,
                             va="top", fontsize=8, family="monospace",
                             color="#333333")

    slider = widgets.Slider(ax_slide, "Frame", 0, n_frames - 1,
                            valinit=0, valstep=1, color="#4a9fd5")

    state = {"frame": 0, "updating": False}

    def draw_frame(idx: int, from_slider: bool = False) -> None:
        if state["updating"]:
            return
        state["updating"] = True
        try:
            idx = max(0, min(n_frames - 1, idx))
            state["frame"] = idx

            fd = frame_data[idx]
            pts = fd["partials"]
            freqs = [float(e[0]) for e in pts if float(e[1]) > 0.0]
            amps  = [20.0 * math.log10(max(float(e[1]), 1e-12))
                     for e in pts if float(e[1]) > 0.0]

            scatter.set_data(freqs, amps)

            gf, gdb = envelopes_lin[idx]
            env_line.set_data(gf, gdb)
            if bark_line is not None:
                gfb, gdbb = envelopes_bark[idx]
                bark_line.set_data(gfb, gdbb)

            diag = diagnostics[idx]
            fallback_str = (f"  fallback={diag['fallback']}"
                            if diag.get("fallback") else "")
            info = (
                f"Frame {idx}/{n_frames-1}   t={fd['time_s']:.4f}s   "
                f"{len(freqs)} active partials\n"
                f"fit_pts={diag.get('fit_points',0)}  "
                f"eff_order={diag.get('effective_order',0)}  "
                f"weighted_err={diag.get('weighted_error_db',0.0):.2f} dB  "
                f"cond≈{diag.get('condition_estimate',0.0):.1e}"
                f"{fallback_str}"
            )
            info_text.set_text(info)

            if not from_slider:
                slider.set_val(idx)
            fig.canvas.draw_idle()
        finally:
            state["updating"] = False

    def on_slider(val: float) -> None:
        draw_frame(int(round(val)), from_slider=True)

    def on_key(event) -> None:
        f = state["frame"]
        if event.key in ("right", "d"):
            draw_frame(f + 1)
        elif event.key in ("left", "a"):
            draw_frame(f - 1)
        elif event.key == "pagedown":
            draw_frame(f + 10)
        elif event.key == "pageup":
            draw_frame(f - 10)
        elif event.key == "home":
            draw_frame(0)
        elif event.key == "end":
            draw_frame(n_frames - 1)
        elif event.key in ("q", "escape"):
            plt.close("all")

    slider.on_changed(on_slider)
    fig.canvas.mpl_connect("key_press_event", on_key)

    draw_frame(0)
    print("  Viewer open.  ← / → arrows or slider to navigate.  Q to quit.")
    plt.show()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Interactive spectral envelope browser for ATS partial frames."
    )
    parser.add_argument("wav", help="Input WAV file (any sample rate/channels).")
    parser.add_argument("--ats-offline", default="./build/tools/ats_offline",
                        help="Path to ats_offline binary (default: ./build/tools/ats_offline).")
    parser.add_argument("--type", dest="analysis_type", type=int, default=4,
                        choices=[1, 2, 3, 4],
                        help="ATS analysis type (default: 4).")
    parser.add_argument("--order", type=int, default=16,
                        help="RDC cepstrum order (default: 16).")
    parser.add_argument("--reg", type=float, default=1e-3,
                        help="RDC regularization (default: 1e-3).")
    parser.add_argument("--bark", action="store_true",
                        help="Overlay a Bark-warped RDC envelope for comparison.")
    parser.add_argument("--cache", default=None, metavar="PATH",
                        help="Cache frame-dump JSON at PATH to skip re-analysis on re-runs.")
    args = parser.parse_args()

    wav_path = Path(args.wav)
    if not wav_path.is_file():
        print(f"error: WAV not found: {wav_path}", file=sys.stderr)
        return 1

    ats_offline = args.ats_offline
    if not os.path.isfile(ats_offline):
        print(f"error: ats_offline not found: {ats_offline}", file=sys.stderr)
        return 1

    print(f"Analyzing: {wav_path.name}")
    dump = run_frame_dump(wav_path, ats_offline, args.analysis_type, args.cache)
    print(
        f"  {dump['frames']} frames  |  {dump['partials']} partials  |"
        f"  sr={dump['sample_rate']} Hz  |"
        f"  dur={dump['duration_seconds']:.3f}s"
    )

    run_viewer(dump, wav_path.name, args.order, args.reg, args.bark)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

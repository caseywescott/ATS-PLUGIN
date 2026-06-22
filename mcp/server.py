"""
ATS MCP server v1.0 — offline spectral modeling tools.

Tools:
  Model I/O:        summarize_model, validate_model, analyze_audio, render_model,
                    time_stretch_model, compare_models, prune_model, export_model
  Descriptors:      measure_descriptors
  Transforms:       transform_model
  Envelopes:        build_envelope, compare_envelopes
  Transients:       detect_onsets, extract_transient_layer, render_with_transients
  Morphing:         plan_morph
  Batch/workflow:   batch_analyze, batch_render, sweep_frec, generate_variations
  Recipe:           create_recipe, validate_recipe
  Diagnostics:      check_build, list_fixtures, estimate_analysis_time,
                    run_golden_compare, evaluate_morph_fixture
"""

import hashlib
import json
import math
import os
import random
import re
import shutil
import subprocess
import struct
import wave
from pathlib import Path
from typing import Optional

from mcp.server.fastmcp import FastMCP

from ats_parser import (
    load_ats, load_ats_frames, load_ats_frames_full, write_ats,
    _theoretical_file_size,
)

_REPO_ROOT = Path(__file__).parent.parent
_TESTS_DIR = _REPO_ROOT / "tests"
_FIXTURES_DIR = _TESTS_DIR / "fixtures"
_TOOLS_DIR = Path(
    os.environ.get("ATS_TOOLS_DIR", str(_REPO_ROOT / "build-plugin" / "tools"))
)

mcp = FastMCP("ats-tools")

_ALL_CLI_TOOLS = [
    "ats_analyze", "ats_synth", "ats_prune",
    "ats_transform", "ats_envelope", "ats_transient", "ats_morph_plan",
]


def _find_tool(name: str) -> Optional[Path]:
    p = _TOOLS_DIR / name
    return p if p.exists() else None


def _require_tool(name: str) -> Path:
    p = _find_tool(name)
    if p is None:
        raise RuntimeError(
            f"CLI tool '{name}' not found at {_TOOLS_DIR / name}. "
            f"Build it first:\n"
            f"  cmake --build {_REPO_ROOT / 'build-plugin'} --target {name}"
        )
    return p


def _run(cmd: list, tool_name: str) -> subprocess.CompletedProcess:
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"{tool_name} exited with code {result.returncode}:\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def _parse_json_stdout(tool_name: str, result: subprocess.CompletedProcess) -> dict:
    """Extract JSON from stdout that may contain ats_load() diagnostic lines before it."""
    start = result.stdout.find('{')
    if start == -1:
        raise RuntimeError(
            f"{tool_name} produced no JSON.\nstdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return json.loads(result.stdout[start:])


# =============================================================================
# MODEL I/O
# =============================================================================

@mcp.tool()
def summarize_model(ats_path: str) -> dict:
    """
    Read an ATS spectral model file and return a JSON summary.

    Returns header metadata (type, sample rate, frame/window size),
    partial and frame counts, frequency range, and per-Bark-band mean noise
    energies when the model includes noise (type 3 or 4).
    """
    return load_ats(ats_path).to_dict()


@mcp.tool()
def validate_model(ats_path: str) -> dict:
    """
    Validate an ATS file for structural integrity.

    Checks: magic number, file size vs. header formula, positive frame/partial
    counts, finite header values, type in {1,2,3,4}.

    Returns {"valid": bool, "errors": [str]}.
    """
    errors = []
    p = Path(ats_path)
    if not p.exists():
        return {"valid": False, "errors": [f"File not found: '{ats_path}'"]}

    try:
        summary = load_ats(ats_path)
        h = summary.header
    except (ValueError, struct.error) as exc:
        return {"valid": False, "errors": [str(exc)]}

    if h.type not in (1, 2, 3, 4):
        errors.append(f"Invalid ATS type {h.type} (must be 1-4)")
    if h.partials <= 0:
        errors.append(f"partials={h.partials} (must be >0)")
    if h.frames <= 0:
        errors.append(f"frames={h.frames} (must be >0)")
    if h.duration <= 0.0:
        errors.append(f"duration={h.duration} (must be >0)")
    if h.sr <= 0.0:
        errors.append(f"sample_rate={h.sr} (must be >0)")
    for field, val in [("max_amp", h.max_amp), ("max_freq", h.max_freq)]:
        if not math.isfinite(val):
            errors.append(f"{field}={val} is not finite")

    expected = _theoretical_file_size(h.partials, h.frames, h.has_phase(), h.has_noise())
    if summary.file_size_bytes != expected:
        errors.append(
            f"File size {summary.file_size_bytes} != expected {expected} "
            f"(header says {h.partials}p × {h.frames}f, type={h.type})"
        )

    return {"valid": len(errors) == 0, "errors": errors}


@mcp.tool()
def analyze_audio(
    audio_path: str,
    ats_path: str,
    analysis_type: int = 4,
    multi_res: bool = False,
    no_slopes: bool = False,
    # Time range
    start_time: float = 0.0,
    duration: float = 0.0,
    # Frequency range
    lowest_freq_hz: float = 20.0,
    highest_freq_hz: float = 20000.0,
    # Windowing
    window_cycles: int = 4,
    window_type: int = 1,
    window_size: int = 1024,
    hop_size: float = 0.25,
    # Peak detection
    lowest_mag_db: float = -60.0,
    freq_dev: float = 0.1,
    # Tracking
    track_len: int = 3,
    min_seg_len: int = 3,
    min_gap_len: int = 3,
    last_peak_cont: float = 0.0,
    smr_cont: float = 0.0,
    smr_threshold_db: float = 30.0,
    min_seg_smr_db: float = 60.0,
    # Multi-resolution (only used when multi_res=True)
    mr_window_mult: float = 2.0,
    mr_crossover_hz: float = 500.0,
) -> dict:
    """
    Analyze a mono WAV file with ATSA peak tracking and write an ATS model.

    Exposes all ANARGS analysis parameters. Defaults match ATSA classic.

    Args:
        audio_path:       Path to a mono WAV file.
        ats_path:         Destination .ats file path.
        analysis_type:    1=amp+freq  2=+phase  3=+noise  4=+phase+noise (default 4).
        multi_res:        Enable multi-resolution analysis front-end.
        no_slopes:        Skip DDM slope computation (legacy LIN3 baseline).

        start_time:       Analysis start time in seconds (default 0.0).
        duration:         Duration to analyze in seconds (0.0 = whole file).

        lowest_freq_hz:   Lowest tracked frequency in Hz (default 20.0).
        highest_freq_hz:  Highest tracked frequency in Hz (default 20000.0).

        window_cycles:    Window size in cycles (default 4; overrides window_size).
        window_type:      0=Blackman  1=Blackman-Harris  2=Hanning (default 1).
        window_size:      Window size in samples (default 1024).
        hop_size:         Hop size as fraction of window (default 0.25).

        lowest_mag_db:    Lowest magnitude threshold in dB (default -60.0).
        freq_dev:         Frequency deviation tolerance for tracking (default 0.1).

        track_len:        Tracking history length in frames (default 3).
        min_seg_len:      Minimum segment length in frames (default 3).
        min_gap_len:      Minimum gap length in frames (default 3).
        last_peak_cont:   Last peak continuation factor (default 0.0).
        smr_cont:         SMR continuation factor (default 0.0).
        smr_threshold_db: SMR threshold in dB (default 30.0).
        min_seg_smr_db:   Minimum segment SMR in dB (default 60.0).

        mr_window_mult:   Multi-res window multiplier for low-freq band (default 2.0).
        mr_crossover_hz:  Multi-res crossover frequency in Hz (default 500.0).

    Returns {"ats_path": str, "model": <summary>, "stdout": str}.
    """
    if analysis_type not in (1, 2, 3, 4):
        raise ValueError("analysis_type must be 1, 2, 3, or 4")
    if window_type not in (0, 1, 2):
        raise ValueError("window_type must be 0 (Blackman), 1 (Blackman-Harris), or 2 (Hanning)")

    tool = _require_tool("ats_analyze")
    cmd = [
        str(tool),
        "--type",           str(analysis_type),
        "--start",          str(start_time),
        "--duration",       str(duration),
        "--lowest-freq",    str(lowest_freq_hz),
        "--highest-freq",   str(highest_freq_hz),
        "--win-cycles",     str(window_cycles),
        "--win-type",       str(window_type),
        "--win-size",       str(window_size),
        "--hop-size",       str(hop_size),
        "--lowest-mag",     str(lowest_mag_db),
        "--freq-dev",       str(freq_dev),
        "--track-len",      str(track_len),
        "--min-seg-len",    str(min_seg_len),
        "--min-gap-len",    str(min_gap_len),
        "--last-peak-cont", str(last_peak_cont),
        "--smr-cont",       str(smr_cont),
        "--smr-threshold",  str(smr_threshold_db),
        "--min-seg-smr",    str(min_seg_smr_db),
    ]
    if multi_res:
        cmd += ["--multi-res",
                "--mr-win-mult",  str(mr_window_mult),
                "--mr-crossover", str(mr_crossover_hz)]
    if no_slopes:
        cmd.append("--no-slopes")
    cmd += [str(audio_path), str(ats_path)]

    result = _run(cmd, "ats_analyze")
    return {"ats_path": str(ats_path), "model": load_ats(ats_path).to_dict(), "stdout": result.stdout}


@mcp.tool()
def render_model(
    ats_path: str,
    wav_path: str,
    amp: float = 1.0,
    frec: float = 1.0,
    noise_balance: float = 1.0,
) -> dict:
    """
    Render an ATS spectral model to a mono WAV via oscillator-bank synthesis.

    Args:
        ats_path:      Path to an existing .ats file.
        wav_path:      Destination WAV path.
        amp:           Amplitude scale (default 1.0).
        frec:          Frequency scale (default 1.0; 2.0 = one octave up).
        noise_balance: Residual noise amplitude scale — 0.0 = sinusoids only,
                       1.0 = full noise (default). Maps to SPARAMS.ramp.
                       Equivalent to kTimbreNoiseBalance in the plugin.
    """
    tool = _require_tool("ats_synth")
    cmd = [str(tool), "--amp", str(amp), "--frec", str(frec),
           "--ramp", str(noise_balance), str(ats_path), str(wav_path)]
    result = _run(cmd, "ats_synth")
    return {"wav_path": str(wav_path), "amp": amp, "frec": frec,
            "noise_balance": noise_balance, "stdout": result.stdout}


@mcp.tool()
def time_stretch_model(
    ats_path: str,
    wav_path: str,
    stretch: float = 1.0,
    beg: float = 0.0,
    end: Optional[float] = None,
    amp: float = 1.0,
    noise_balance: float = 1.0,
) -> dict:
    """
    Render an ATS model with time-domain stretching (maps to SPARAMS.max_stretch).

    Args:
        ats_path:      Input .ats file.
        wav_path:      Output WAV.
        stretch:       Time stretch factor (>1 stretches; default 1.0).
        beg:           Start time in seconds within the model (default 0.0).
        end:           End time in seconds (None = model duration).
        amp:           Amplitude scale.
        noise_balance: Residual noise amplitude scale (0=sinusoids only, 1=full noise).
    """
    tool = _require_tool("ats_synth")
    cmd = [str(tool), "--amp", str(amp), "--stretch", str(stretch),
           "--beg", str(beg), "--ramp", str(noise_balance)]
    if end is not None:
        cmd += ["--end", str(end)]
    cmd += [str(ats_path), str(wav_path)]
    result = _run(cmd, "ats_synth")
    return {"wav_path": str(wav_path), "stretch": stretch, "beg": beg, "end": end,
            "noise_balance": noise_balance, "stdout": result.stdout}


@mcp.tool()
def compare_models(ats_path_a: str, ats_path_b: str) -> dict:
    """
    Diff two ATS models: header scalars + per-Bark-band energy deltas (B − A).
    """
    a = load_ats(ats_path_a)
    b = load_ats(ats_path_b)
    da, db = a.to_dict(), b.to_dict()
    scalar_keys = [
        "type", "sample_rate", "frame_size", "window_size",
        "partials", "active_partials", "frames", "duration_s",
        "max_amp", "max_freq_hz", "has_phase", "has_noise",
    ]
    diffs = {k: {"a": da[k], "b": db[k]} for k in scalar_keys if da.get(k) != db.get(k)}
    band_delta = None
    if da.get("mean_band_energies") and db.get("mean_band_energies"):
        band_delta = [
            round(db["mean_band_energies"][i] - da["mean_band_energies"][i], 8)
            for i in range(len(da["mean_band_energies"]))
        ]
    return {"a": str(ats_path_a), "b": str(ats_path_b), "scalar_diffs": diffs, "band_energy_delta": band_delta}


@mcp.tool()
def prune_model(
    ats_path: str,
    output_path: str,
    smr_threshold: float = 0.0,
    min_active_frames: int = 1,
    output_type: Optional[int] = None,
) -> dict:
    """
    Prune low-energy partials from an ATS model and save the result.

    Calls ats_prune CLI which runs optimize_sound() (removes partials below
    SMR threshold and optionally with fewer than min_active_frames active frames).

    Args:
        smr_threshold:     Partials with mean SMR below this are dropped.
        min_active_frames: Partials active in fewer than this many frames are dropped.
        output_type:       Re-save as different type (None = keep original type).
    """
    tool = _require_tool("ats_prune")
    cmd = [str(tool), "--smr-threshold", str(smr_threshold),
           "--min-active-frames", str(min_active_frames)]
    if output_type is not None:
        cmd += ["--type", str(output_type)]
    cmd += [str(ats_path), str(output_path)]
    result = _run(cmd, "ats_prune")
    return {"output_path": str(output_path), "model": load_ats(output_path).to_dict(), "stdout": result.stdout}


@mcp.tool()
def export_model(ats_path: str, output_path: str, output_type: int) -> dict:
    """
    Re-export an ATS model as a different type (e.g. strip phase: type 4 → type 3).

    Uses ats_prune with threshold=0 and no frame minimum, just a type conversion.
    Type 1=amp+freq, 2=+phase, 3=+noise, 4=+phase+noise.
    """
    if output_type not in (1, 2, 3, 4):
        raise ValueError("output_type must be 1, 2, 3, or 4")
    tool = _require_tool("ats_prune")
    cmd = [str(tool), "--smr-threshold", "0.0", "--min-active-frames", "1",
           "--type", str(output_type), str(ats_path), str(output_path)]
    result = _run(cmd, "ats_prune")
    return {"output_path": str(output_path), "output_type": output_type,
            "model": load_ats(output_path).to_dict(), "stdout": result.stdout}


# =============================================================================
# DESCRIPTORS
# =============================================================================

@mcp.tool()
def measure_descriptors(ats_path: str, frame_stride: int = 1) -> dict:
    """
    Compute spectral descriptors from an ATS model's partial data.

    Descriptors computed per frame then aggregated globally:
      centroid_hz    — amplitude-weighted mean frequency
      spread_hz      — amplitude-weighted standard deviation of frequency
      brightness     — centroid / nyquist (0=low, 1=high)
      tilt_db_oct    — spectral tilt: slope of 20·log10(amp) vs log2(freq) (dB/octave)
      rms_envelope   — per-frame RMS of active partial amplitudes

    Args:
        frame_stride: Sample every Nth frame (default 1 = all frames).
    """
    summary = load_ats(ats_path)
    nyquist = summary.header.sr / 2.0
    frames = load_ats_frames(ats_path)

    centroids = []
    spreads = []
    rms_env = []
    tilt_xs = []  # log2(freq) across all active (frame, partial)
    tilt_ys = []  # 20*log10(amp+eps) across all active

    eps = 1e-30
    for fi, frm in enumerate(frames):
        if fi % frame_stride != 0:
            continue
        amps = frm["amps"]
        freqs = frm["freqs"]
        active_a = [a for a, f in zip(amps, freqs) if a > 0.0 and f > 0.0]
        active_f = [f for a, f in zip(amps, freqs) if a > 0.0 and f > 0.0]
        if not active_a:
            rms_env.append(0.0)
            continue

        total_amp = sum(active_a)
        centroid = sum(a * f for a, f in zip(active_a, active_f)) / total_amp
        spread = math.sqrt(
            sum(a * (f - centroid) ** 2 for a, f in zip(active_a, active_f)) / total_amp
        )
        rms = math.sqrt(sum(a * a for a in active_a) / len(active_a))

        centroids.append(centroid)
        spreads.append(spread)
        rms_env.append(rms)

        for a, f in zip(active_a, active_f):
            tilt_xs.append(math.log2(f))
            tilt_ys.append(20.0 * math.log10(a + eps))

    # Spectral tilt via least-squares slope
    tilt = 0.0
    if len(tilt_xs) >= 2:
        n = len(tilt_xs)
        sx = sum(tilt_xs)
        sy = sum(tilt_ys)
        sxx = sum(x * x for x in tilt_xs)
        sxy = sum(x * y for x, y in zip(tilt_xs, tilt_ys))
        denom = n * sxx - sx * sx
        if abs(denom) > 1e-12:
            tilt = (n * sxy - sx * sy) / denom  # dB per octave

    mean_centroid = sum(centroids) / len(centroids) if centroids else 0.0
    mean_spread = sum(spreads) / len(spreads) if spreads else 0.0

    return {
        "centroid_hz": round(mean_centroid, 2),
        "spread_hz": round(mean_spread, 2),
        "brightness": round(mean_centroid / nyquist, 4) if nyquist > 0 else 0.0,
        "tilt_db_oct": round(tilt, 4),
        "rms_envelope": [round(r, 6) for r in rms_env],
        "frames_analyzed": len(centroids),
    }


# =============================================================================
# SPECTRAL TRANSFORMS
# =============================================================================

@mcp.tool()
def transform_model(
    ats_path: str,
    output_path: str,
    freq_mode: str = "Linear",
    freq_amount: float = 0.0,
    freq_pivot_hz: float = 440.0,
    amp_scale: float = 1.0,
    noise_scale: float = 1.0,
    output_type: Optional[int] = None,
) -> dict:
    """
    Apply frequency warping, amplitude scaling, and noise scaling to an ATS model.

    Frequency modes (backed by AtsFrequencyShape):
      Linear    — uniform Hz offset (freq_amount in Hz)
      Stretch   — stretch/compress around pivot (freq_amount: 0=identity, +1=double spread)
      Compress  — inverse of Stretch
      Harmonic  — quantize toward nearest harmonic of pivot (freq_amount: 0=none, 1=full snap)

    Args:
        freq_mode:     "Linear" | "Stretch" | "Compress" | "Harmonic"
        freq_amount:   Mode-specific warp amount.
        freq_pivot_hz: Pivot frequency for Stretch/Compress/Harmonic.
        amp_scale:     Multiply all partial amplitudes by this factor.
        noise_scale:   Multiply all band energies by this factor.
        output_type:   Save as different type (None = keep original).
    """
    valid_modes = {"Linear", "Stretch", "Compress", "Harmonic"}
    if freq_mode not in valid_modes:
        raise ValueError(f"freq_mode must be one of {sorted(valid_modes)}")
    tool = _require_tool("ats_transform")
    cmd = [
        str(tool),
        "--freq-mode",    freq_mode,
        "--freq-amount",  str(freq_amount),
        "--freq-pivot-hz", str(freq_pivot_hz),
        "--amp-scale",    str(amp_scale),
        "--noise-scale",  str(noise_scale),
    ]
    if output_type is not None:
        cmd += ["--type", str(output_type)]
    cmd += [str(ats_path), str(output_path)]
    result = _run(cmd, "ats_transform")
    return {"output_path": str(output_path), "model": load_ats(output_path).to_dict(), "stdout": result.stdout}


# =============================================================================
# SURGICAL EDITS  (pure Python read-modify-write)
# =============================================================================

@mcp.tool()
def edit_partials(
    ats_path: str,
    output_path: str,
    partial_indices: Optional[list] = None,
    amp_scale: float = 1.0,
    freq_scale: float = 1.0,
    freq_shift_hz: float = 0.0,
    zero: bool = False,
    invert_selection: bool = False,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
) -> dict:
    """
    Surgically edit amplitude and frequency of individual partials.

    Reads the ATS binary, modifies selected partials in [frame_start, frame_end),
    and writes a new file. max_amp and max_freq in the header are recomputed.

    Args:
        partial_indices:   List of 0-based partial indices to operate on (None = all).
        amp_scale:         Multiply amplitude of selected partials by this factor.
        freq_scale:        Multiply frequency of selected partials by this factor.
        freq_shift_hz:     Add this offset (Hz) to frequency of selected partials.
        zero:              If True, set amp/freq to 0 for selected partials.
        invert_selection:  If True, apply to all partials EXCEPT those listed.
        frame_start:       First frame to modify (inclusive, default 0).
        frame_end:         Last frame to modify (exclusive, None = all frames).

    Examples:
        # Mute the fundamental only in the first 10 frames
        edit_partials(src, out, partial_indices=[0], zero=True, frame_end=10)

        # Solo partials 0-2 (zero everything else)
        edit_partials(src, out, partial_indices=[0,1,2], zero=True, invert_selection=True)
    """
    header, frames = load_ats_frames_full(ats_path)
    n_par = header.partials
    n_fra = header.frames
    f0 = max(0, frame_start)
    f1 = n_fra if frame_end is None else min(frame_end, n_fra)

    if partial_indices is None:
        targets = set(range(n_par))
    else:
        requested = set(partial_indices)
        targets = (set(range(n_par)) - requested) if invert_selection else requested

    modified_frames = 0
    for fi, frm in enumerate(frames):
        if not (f0 <= fi < f1):
            continue
        for i in targets:
            if i < 0 or i >= n_par:
                continue
            if zero:
                frm["amps"][i] = 0.0
                frm["freqs"][i] = 0.0
                if frm["phases"] is not None:
                    frm["phases"][i] = 0.0
            else:
                frm["amps"][i] *= amp_scale
                frm["freqs"][i] = frm["freqs"][i] * freq_scale + freq_shift_hz
        modified_frames += 1

    write_ats(header, frames, output_path)
    result = load_ats(output_path).to_dict()
    return {
        "output_path": output_path,
        "partials_modified": len(targets),
        "frames_modified": modified_frames,
        "model": result,
    }


@mcp.tool()
def edit_bands(
    ats_path: str,
    output_path: str,
    band_indices: Optional[list] = None,
    energy_scale: float = 1.0,
    zero: bool = False,
    invert_selection: bool = False,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
) -> dict:
    """
    Surgically edit the 25 Bark-band residual noise energies.

    Requires a type 3 or type 4 ATS file (has_noise=True). Reads the model,
    modifies band energies in [frame_start, frame_end), and writes a new file.

    Band indices 0–24 ≈ ATSA critical bands: 0≈0–100 Hz, 24≈15–20 kHz.

    Args:
        band_indices:      List of 0-based band indices (0–24). None = all 25.
        energy_scale:      Multiply selected band energies by this factor.
        zero:              If True, set selected band energies to 0.
        invert_selection:  If True, apply to all bands EXCEPT those listed.
        frame_start:       First frame to modify (inclusive, default 0).
        frame_end:         Last frame to modify (exclusive, None = all frames).

    Examples:
        # Remove high-frequency noise above band 15 (~8 kHz)
        edit_bands(src, out, band_indices=list(range(15, 25)), zero=True)

        # Solo bands 8-12 in the first 20 frames
        edit_bands(src, out, band_indices=list(range(8,13)), zero=True,
                   invert_selection=True, frame_end=20)
    """
    header, frames = load_ats_frames_full(ats_path)

    if not header.has_noise():
        raise ValueError(
            f"ATS file type {header.type} has no residual noise bands. "
            "edit_bands requires type 3 or 4."
        )

    n_fra = header.frames
    f0 = max(0, frame_start)
    f1 = n_fra if frame_end is None else min(frame_end, n_fra)

    if band_indices is None:
        targets = set(range(25))
    else:
        requested = set(band_indices)
        targets = (set(range(25)) - requested) if invert_selection else requested

    modified_frames = 0
    for fi, frm in enumerate(frames):
        if not (f0 <= fi < f1):
            continue
        be = frm["band_energies"]
        for b in targets:
            if 0 <= b < 25:
                be[b] = 0.0 if zero else be[b] * energy_scale
        modified_frames += 1

    write_ats(header, frames, output_path)
    result = load_ats(output_path).to_dict()
    return {
        "output_path": output_path,
        "bands_modified": len(targets),
        "frames_modified": modified_frames,
        "model": result,
    }


@mcp.tool()
def freeze_partial(
    ats_path: str,
    output_path: str,
    partial_indices: Optional[list] = None,
    frame_index: int = 0,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
) -> dict:
    """
    Freeze one or more partials at the amplitude and frequency of a single frame.

    Copies amp, freq (and phase if present) from frame_index to every frame in
    [frame_start, frame_end). Useful for locking a partial steady or repairing
    a tracking error over a range of frames.

    Args:
        partial_indices: Partials to freeze (None = all).
        frame_index:     Source frame whose values are copied.
        frame_start:     First destination frame (inclusive, default 0).
        frame_end:       Last destination frame (exclusive, None = all frames).
    """
    header, frames = load_ats_frames_full(ats_path)
    n_par = header.partials
    n_fra = header.frames

    if not (0 <= frame_index < n_fra):
        raise ValueError(f"frame_index {frame_index} out of range [0, {n_fra})")

    f0 = max(0, frame_start)
    f1 = n_fra if frame_end is None else min(frame_end, n_fra)
    targets = set(range(n_par)) if partial_indices is None else set(partial_indices)

    src = frames[frame_index]
    modified_frames = 0
    for fi, frm in enumerate(frames):
        if not (f0 <= fi < f1):
            continue
        for i in targets:
            if 0 <= i < n_par:
                frm["amps"][i] = src["amps"][i]
                frm["freqs"][i] = src["freqs"][i]
                if frm["phases"] is not None and src["phases"] is not None:
                    frm["phases"][i] = src["phases"][i]
        modified_frames += 1

    write_ats(header, frames, output_path)
    return {
        "output_path": output_path,
        "frozen_from_frame": frame_index,
        "partials_frozen": len(targets),
        "frames_modified": modified_frames,
        "model": load_ats(output_path).to_dict(),
    }


@mcp.tool()
def smooth_partial_envelope(
    ats_path: str,
    output_path: str,
    window_frames: int = 5,
    partial_indices: Optional[list] = None,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
) -> dict:
    """
    Smooth the amplitude envelope of partials over time via a box average.

    For each selected partial, replaces each frame's amplitude with the mean
    over a symmetric window of width (2 × window_frames + 1) centered on that
    frame. Equivalent to the plugin's smoothPartialEnvelope operation.

    Useful for removing analysis jitter or reducing amplitude roughness on
    sustained tones without affecting frequency content.

    Args:
        window_frames:   Half-width of the smoothing window in frames (default 5).
                         Larger = smoother. Window is clamped at file boundaries.
        partial_indices: Partials to smooth (None = all active partials).
        frame_start:     First frame to smooth (inclusive).
        frame_end:       Last frame to smooth (exclusive, None = all frames).
    """
    header, frames = load_ats_frames_full(ats_path)
    n_par = header.partials
    n_fra = header.frames
    f0 = max(0, frame_start)
    f1 = n_fra if frame_end is None else min(frame_end, n_fra)
    targets = set(range(n_par)) if partial_indices is None else set(partial_indices)

    # Extract amp arrays per partial for efficient windowed average
    orig_amps = [[frames[fi]["amps"][i] for fi in range(n_fra)] for i in range(n_par)]

    r = window_frames
    for fi in range(f0, f1):
        for i in targets:
            if not (0 <= i < n_par):
                continue
            lo = max(0, fi - r)
            hi = min(n_fra, fi + r + 1)
            window = orig_amps[i][lo:hi]
            frames[fi]["amps"][i] = sum(window) / len(window) if window else 0.0

    write_ats(header, frames, output_path)
    return {
        "output_path": output_path,
        "window_frames": window_frames,
        "partials_smoothed": len(targets),
        "frames_processed": f1 - f0,
        "model": load_ats(output_path).to_dict(),
    }


@mcp.tool()
def tilt_spectral(
    ats_path: str,
    output_path: str,
    tilt_db_per_oct: float = 0.0,
    pivot_hz: float = 1000.0,
    freq_lo_hz: float = 0.0,
    freq_hi_hz: float = 24000.0,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
) -> dict:
    """
    Apply a spectral tilt (dB/octave slope) to partial amplitudes.

    For each partial whose mean frequency falls in [freq_lo_hz, freq_hi_hz],
    multiplies its amplitude by:

        gain = 10 ^ (tilt_db_per_oct × log2(freq / pivot_hz) / 20)

    Positive tilt boosts high partials (brighter). Negative tilt attenuates
    them (warmer). Equivalent to the plugin's tiltPartialEnvelope operation
    and the kTimbreTiltDbPerOct synthesis parameter — but applied at the
    model level so it persists to the ATS file.

    Args:
        tilt_db_per_oct: Slope in dB per octave (e.g. +6=bright, -6=warm).
        pivot_hz:        Frequency around which tilt is 0 dB (default 1000 Hz).
        freq_lo_hz:      Lower bound of frequency selection (default 0).
        freq_hi_hz:      Upper bound of frequency selection (default 24000).
        frame_start:     First frame to modify (inclusive).
        frame_end:       Last frame to modify (exclusive, None = all frames).
    """
    import math as _math
    header, frames = load_ats_frames_full(ats_path)
    n_par = header.partials
    n_fra = header.frames
    f0 = max(0, frame_start)
    f1 = n_fra if frame_end is None else min(frame_end, n_fra)

    # Precompute per-partial mean freq to determine which partials are in range
    summary = load_ats(ats_path)
    mean_freqs = summary.partial_mean_freqs

    eps = 1e-9
    for fi in range(f0, f1):
        frm = frames[fi]
        for i in range(n_par):
            mf = mean_freqs[i]
            if mf <= 0.0 or not (freq_lo_hz <= mf <= freq_hi_hz):
                continue
            inst_freq = frm["freqs"][i]
            if inst_freq <= 0.0:
                continue
            gain = 10.0 ** (tilt_db_per_oct * _math.log2(inst_freq / (pivot_hz + eps)) / 20.0)
            frm["amps"][i] = max(0.0, frm["amps"][i] * gain)

    write_ats(header, frames, output_path)
    n_in_range = sum(1 for mf in mean_freqs if freq_lo_hz <= mf <= freq_hi_hz and mf > 0)
    return {
        "output_path": output_path,
        "tilt_db_per_oct": tilt_db_per_oct,
        "pivot_hz": pivot_hz,
        "partials_in_freq_range": n_in_range,
        "frames_processed": f1 - f0,
        "model": load_ats(output_path).to_dict(),
    }


@mcp.tool()
def smooth_residual_bands(
    ats_path: str,
    output_path: str,
    window_frames: int = 5,
    band_indices: Optional[list] = None,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
) -> dict:
    """
    Smooth residual band energies over time via a box average.

    For each selected Bark band, replaces each frame's energy with the mean
    over a symmetric window of width (2 × window_frames + 1). Equivalent to
    the plugin's smoothResidualBands operation.

    Requires type 3 or 4 (has_noise=True). Useful for removing discontinuities
    in the noise floor or softening abrupt residual transients.

    Args:
        window_frames: Half-width of smoothing window in frames (default 5).
        band_indices:  Bands to smooth (None = all 25 Bark bands).
        frame_start:   First frame to smooth (inclusive).
        frame_end:     Last frame to smooth (exclusive, None = all frames).
    """
    header, frames = load_ats_frames_full(ats_path)
    if not header.has_noise():
        raise ValueError(
            f"ATS file type {header.type} has no residual bands. Requires type 3 or 4."
        )

    n_fra = header.frames
    f0 = max(0, frame_start)
    f1 = n_fra if frame_end is None else min(frame_end, n_fra)
    targets = list(range(25)) if band_indices is None else [b for b in band_indices if 0 <= b < 25]

    # Snapshot original values before mutation
    orig = [[frames[fi]["band_energies"][b] for fi in range(n_fra)] for b in range(25)]

    r = window_frames
    for fi in range(f0, f1):
        for b in targets:
            lo = max(0, fi - r)
            hi = min(n_fra, fi + r + 1)
            window = orig[b][lo:hi]
            frames[fi]["band_energies"][b] = sum(window) / len(window) if window else 0.0

    write_ats(header, frames, output_path)
    return {
        "output_path": output_path,
        "window_frames": window_frames,
        "bands_smoothed": len(targets),
        "frames_processed": f1 - f0,
        "model": load_ats(output_path).to_dict(),
    }


@mcp.tool()
def tilt_residual_bands(
    ats_path: str,
    output_path: str,
    tilt_db_per_band: float = 0.0,
    band_indices: Optional[list] = None,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
) -> dict:
    """
    Apply a linear energy tilt (dB per band step) across Bark residual bands.

    For the i-th selected band (0-indexed within the selection), multiplies
    its energy by:

        gain = 10 ^ (tilt_db_per_band × i / 20)

    Positive tilt boosts higher bands (more high-frequency noise). Negative
    tilt attenuates them. Equivalent to the plugin's tiltResidualBands.

    Requires type 3 or 4. Use this to reshape the noise floor's spectral shape
    without changing overall level.

    Args:
        tilt_db_per_band: dB per band-step slope (e.g. -1 = gentle high-freq rolloff).
        band_indices:     Bands to tilt (None = all 25 Bark bands).
        frame_start:      First frame to modify (inclusive).
        frame_end:        Last frame to modify (exclusive, None = all frames).
    """
    import math as _math
    header, frames = load_ats_frames_full(ats_path)
    if not header.has_noise():
        raise ValueError(
            f"ATS file type {header.type} has no residual bands. Requires type 3 or 4."
        )

    n_fra = header.frames
    f0 = max(0, frame_start)
    f1 = n_fra if frame_end is None else min(frame_end, n_fra)
    targets = list(range(25)) if band_indices is None else [b for b in band_indices if 0 <= b < 25]

    gains = [10.0 ** (tilt_db_per_band * idx / 20.0) for idx in range(len(targets))]

    for fi in range(f0, f1):
        be = frames[fi]["band_energies"]
        for idx, b in enumerate(targets):
            be[b] = max(0.0, be[b] * gains[idx])

    write_ats(header, frames, output_path)
    return {
        "output_path": output_path,
        "tilt_db_per_band": tilt_db_per_band,
        "bands_tilted": len(targets),
        "frames_processed": f1 - f0,
        "model": load_ats(output_path).to_dict(),
    }


# =============================================================================
# SPECTRAL ENVELOPES
# =============================================================================

@mcp.tool()
def build_envelope(
    ats_path: str,
    frame_start: int = 0,
    frame_end: Optional[int] = None,
    stride: int = 1,
) -> dict:
    """
    Build a True-Envelope (cepstral) spectral envelope per frame.

    Calls ats_envelope CLI which uses BarkSpectralEnvelope::build().
    Returns per-frame Bark-gridded log-amplitude bins and formant peaks.

    Args:
        frame_start: First frame (inclusive, default 0).
        frame_end:   Last frame (exclusive, None = all).
        stride:      Step between frames (default 1).
    """
    tool = _require_tool("ats_envelope")
    cmd = [str(tool), "--mode", "envelope"]
    if frame_end is not None:
        cmd += ["--frames", f"{frame_start}-{frame_end}"]
    else:
        cmd += ["--frames", f"{frame_start}-"]
    cmd += ["--stride", str(stride), str(ats_path)]
    result = _run(cmd, "ats_envelope")
    return _parse_json_stdout("ats_envelope", result)


@mcp.tool()
def compare_envelopes(
    ats_path_a: str,
    ats_path_b: str,
    frame_a: int = 0,
    frame_b: int = 0,
) -> dict:
    """
    Formant-aligned comparison of two ATS model spectral envelopes.

    Calls ats_envelope with --compare mode, backed by FormantAlignedEnvelopeMorph.
    Returns envelope_a, envelope_b, and the formant warp mapping.
    """
    tool = _require_tool("ats_envelope")
    cmd = [
        str(tool), "--mode", "compare",
        "--frame-a", str(frame_a),
        "--frame-b", str(frame_b),
        str(ats_path_a), str(ats_path_b),
    ]
    result = _run(cmd, "ats_envelope")
    return _parse_json_stdout("ats_envelope", result)


# =============================================================================
# TRANSIENTS
# =============================================================================

def _detect_params_flags(
    weight_det_flux: float,
    weight_res_flux: float,
    weight_hfc_flux: float,
    weight_birth: float,
    weight_phase_dev: float,
    use_phase_deviation: bool,
    norm_window_radius: int,
    peak_window_radius: int,
    threshold_lambda: float,
    threshold_auto_scale: float,
    onset_floor: float,
    min_gap_seconds: float,
    pre_margin_seconds: float,
    max_attack_frames: int,
) -> list:
    """Build the CLI flag list for SmearnovDetectParams."""
    flags = [
        "--weight-det",     str(weight_det_flux),
        "--weight-res",     str(weight_res_flux),
        "--weight-hfc",     str(weight_hfc_flux),
        "--weight-birth",   str(weight_birth),
        "--weight-phase",   str(weight_phase_dev),
        "--norm-window",    str(norm_window_radius),
        "--peak-window",    str(peak_window_radius),
        "--threshold-lambda", str(threshold_lambda),
        "--threshold-scale",  str(threshold_auto_scale),
        "--onset-floor",    str(onset_floor),
        "--min-gap",        str(min_gap_seconds),
        "--pre-margin",     str(pre_margin_seconds),
        "--max-attack-frames", str(max_attack_frames),
    ]
    if use_phase_deviation:
        flags.append("--use-phase-dev")
    return flags


@mcp.tool()
def detect_onsets(
    ats_path: str,
    # Fusion weights for detection functions D1..D5
    weight_det_flux: float = 1.0,
    weight_res_flux: float = 0.8,
    weight_hfc_flux: float = 0.6,
    weight_birth: float = 0.7,
    weight_phase_dev: float = 0.4,
    use_phase_deviation: bool = False,
    # Normalization
    norm_window_radius: int = 8,
    # Peak picking
    peak_window_radius: int = 6,
    threshold_lambda: float = 0.0,
    threshold_auto_scale: float = 1.5,
    onset_floor: float = 1e-6,
    min_gap_seconds: float = 0.03,
    # Attack window
    pre_margin_seconds: float = 0.005,
    max_attack_frames: int = 0,
) -> dict:
    """
    Detect onset frames using Smearnov flux-based detection with full parameter control.

    Five detection functions are fused: D1=deterministic energy flux, D2=residual flux,
    D3=residual HFC flux, D4=partial-birth rate, D5=phase deviation (type 2/4 only).
    Returns {"onsets": [{"frame", "pre_frame", "peak_frame", "time_s", "strength"}]}.
    Pass the result directly to suppress_onsets or smear_attacks.

    Args:
        weight_det_flux: D1 weight — sensitivity to amplitude onsets in partials (default 1.0)
        weight_res_flux: D2 weight — sensitivity to residual energy bursts (default 0.8)
        weight_hfc_flux: D3 weight — high-frequency content changes (default 0.6)
        weight_birth:    D4 weight — new partial births (default 0.7)
        weight_phase_dev: D5 weight — phase deviation from expected (default 0.4)
        use_phase_deviation: Force D5 on even for type 1/3 (auto-enabled for type 2/4)
        norm_window_radius: Running median/MAD normalization window radius in frames
        peak_window_radius: Local-max detection window radius in frames
        threshold_lambda: Fixed additive threshold; <=0 → auto from global MAD
        threshold_auto_scale: Multiplier on global MAD for auto threshold (lower=more onsets)
        onset_floor: Absolute minimum combined strength to accept an onset
        min_gap_seconds: Minimum refractory gap between consecutive onsets
        pre_margin_seconds: Pre-onset margin folded into the attack window
        max_attack_frames: Cap on attack window length (0=uncapped)
    """
    tool = _require_tool("ats_transient")
    flags = _detect_params_flags(
        weight_det_flux, weight_res_flux, weight_hfc_flux, weight_birth, weight_phase_dev,
        use_phase_deviation, norm_window_radius, peak_window_radius,
        threshold_lambda, threshold_auto_scale, onset_floor,
        min_gap_seconds, pre_margin_seconds, max_attack_frames,
    )
    result = _run([str(tool), "--mode", "detect"] + flags + [str(ats_path)], "ats_transient")
    return _parse_json_stdout("ats_transient", result)


@mcp.tool()
def extract_transient_layer(
    ats_path: str,
    weight_det_flux: float = 1.0,
    weight_res_flux: float = 0.8,
    weight_hfc_flux: float = 0.6,
    weight_birth: float = 0.7,
    weight_phase_dev: float = 0.4,
    use_phase_deviation: bool = False,
    norm_window_radius: int = 8,
    peak_window_radius: int = 6,
    threshold_lambda: float = 0.0,
    threshold_auto_scale: float = 1.5,
    onset_floor: float = 1e-6,
    min_gap_seconds: float = 0.03,
    pre_margin_seconds: float = 0.005,
    max_attack_frames: int = 0,
) -> dict:
    """
    Extract the transient event layer (same detection params as detect_onsets).

    Returns {"onsets": [...], "events": [{"frame", "time_s", "strength",
    "deterministic_energy", "band_energy": [float × 25]}]}.
    """
    tool = _require_tool("ats_transient")
    flags = _detect_params_flags(
        weight_det_flux, weight_res_flux, weight_hfc_flux, weight_birth, weight_phase_dev,
        use_phase_deviation, norm_window_radius, peak_window_radius,
        threshold_lambda, threshold_auto_scale, onset_floor,
        min_gap_seconds, pre_margin_seconds, max_attack_frames,
    )
    result = _run([str(tool), "--mode", "extract"] + flags + [str(ats_path)], "ats_transient")
    return _parse_json_stdout("ats_transient", result)


@mcp.tool()
def smear_attacks(
    ats_path: str,
    output_path: str,
    # SmearnovParams
    blur_ms: float = 0.0,
    sharpen_amount: float = 0.0,
    exaggerate_db: float = 0.0,
    max_db: float = 12.0,
    mix: float = 1.0,
    target: str = "both",
    # SmearnovDetectParams
    weight_det_flux: float = 1.0,
    weight_res_flux: float = 0.8,
    weight_hfc_flux: float = 0.6,
    weight_birth: float = 0.7,
    weight_phase_dev: float = 0.4,
    use_phase_deviation: bool = False,
    norm_window_radius: int = 8,
    peak_window_radius: int = 6,
    threshold_lambda: float = 0.0,
    threshold_auto_scale: float = 1.5,
    onset_floor: float = 1e-6,
    min_gap_seconds: float = 0.03,
    pre_margin_seconds: float = 0.005,
    max_attack_frames: int = 0,
) -> dict:
    """
    Reshape onset attack envelopes in-place: blur, sharpen, or exaggerate transient attacks.

    Detects onsets with Smearnov, then applies SmearnovParams to each onset's attack window
    [pre_frame, peak_frame]. Writes modified model to output_path.

    Args:
        blur_ms: Gaussian blur of attack amplitude envelope in milliseconds.
                 Spreads attack energy forward in time — softens transients.
        sharpen_amount: Unsharp-mask contrast boost. Increases slope of the attack rise.
        exaggerate_db: Boost (positive) or cut (negative) the attack peak amplitude in dB.
                       Clamped to ±max_db. Ramps up to peak_frame, holds across decay tail.
        max_db: Maximum magnitude for exaggerate_db (default 12 dB).
        mix: Dry/wet blend: 0.0=unchanged, 1.0=full effect (default 1.0).
        target: Which data to modify: "partials" | "residual" | "both" (default "both").

    At least one of blur_ms > 0, sharpen_amount > 0, or exaggerate_db ≠ 0 must be set.
    Returns {"onsets_processed", "changed", "blur_ms", "sharpen_amount", "exaggerate_db",
             "mix", "target", "output_path"}.
    """
    if target not in ("partials", "residual", "both"):
        raise ValueError(f"target must be 'partials', 'residual', or 'both', got {target!r}")
    if blur_ms == 0.0 and sharpen_amount == 0.0 and exaggerate_db == 0.0:
        raise ValueError("At least one of blur_ms, sharpen_amount, or exaggerate_db must be non-zero")

    tool = _require_tool("ats_transient")
    detect_flags = _detect_params_flags(
        weight_det_flux, weight_res_flux, weight_hfc_flux, weight_birth, weight_phase_dev,
        use_phase_deviation, norm_window_radius, peak_window_radius,
        threshold_lambda, threshold_auto_scale, onset_floor,
        min_gap_seconds, pre_margin_seconds, max_attack_frames,
    )
    smear_flags = [
        "--blur-ms",       str(blur_ms),
        "--sharpen",       str(sharpen_amount),
        "--exaggerate-db", str(exaggerate_db),
        "--max-db",        str(max_db),
        "--mix",           str(mix),
        "--target",        target,
        "--output-ats",    str(output_path),
    ]
    cmd = [str(tool), "--mode", "smear"] + detect_flags + smear_flags + [str(ats_path)]
    result = _run(cmd, "ats_transient")
    return _parse_json_stdout("ats_transient", result)


@mcp.tool()
def suppress_onsets(
    ats_path: str,
    output_path: str,
    onset_indices: list,
    onsets: Optional[list] = None,
    mode: str = "zero",
    target: str = "both",
    # Detection params used only when onsets is None
    min_gap_seconds: float = 0.03,
    threshold_auto_scale: float = 1.5,
) -> dict:
    """
    Selectively suppress or fade individual onset attacks by index.

    Workflow: call detect_onsets() first to inspect the onset list, then pass
    onset_indices (0-based) of the onsets you want removed or attenuated.

    Args:
        onset_indices: 0-based indices into the onset list of onsets to suppress.
        onsets: Onset list from a prior detect_onsets() call. If None, re-detects
                internally using min_gap_seconds and threshold_auto_scale.
        mode: "zero" — zero all amplitudes/energies in [pre_frame, peak_frame].
              "fade_out" — linear ramp from 1.0 at pre_frame to 0.0 at peak_frame.
        target: "partials" | "residual" | "both" (default "both").
        min_gap_seconds: Detection param used only when onsets is None.
        threshold_auto_scale: Detection param used only when onsets is None.

    Returns {"onsets_suppressed", "onset_indices", "mode", "target",
             "frames_modified", "output_path"}.
    """
    if mode not in ("zero", "fade_out"):
        raise ValueError(f"mode must be 'zero' or 'fade_out', got {mode!r}")
    if target not in ("partials", "residual", "both"):
        raise ValueError(f"target must be 'partials', 'residual', or 'both', got {target!r}")

    # Auto-detect if caller didn't supply an onset list
    if onsets is None:
        r = detect_onsets(ats_path,
                          min_gap_seconds=min_gap_seconds,
                          threshold_auto_scale=threshold_auto_scale)
        onsets = r["onsets"]

    if not onsets:
        raise ValueError("No onsets found — nothing to suppress")

    n_onsets = len(onsets)
    for idx in onset_indices:
        if idx < 0 or idx >= n_onsets:
            raise ValueError(f"onset_index {idx} out of range (0–{n_onsets - 1})")

    header, frames = load_ats_frames_full(ats_path)
    n_frames = len(frames)
    do_partials = target in ("partials", "both")
    do_residual = target in ("residual", "both")

    if do_residual and not header.has_noise():
        raise ValueError("Residual suppression requires type 3 or 4 (model has no noise bands)")

    frames_modified = set()
    for idx in onset_indices:
        onset = onsets[idx]
        start = max(0, min(onset["pre_frame"], n_frames - 1))
        end   = max(start, min(onset["peak_frame"], n_frames - 1))
        span  = max(1, end - start)

        for fi in range(start, end + 1):
            if mode == "zero":
                gain = 0.0
            else:  # fade_out: 1.0 at pre_frame → 0.0 at peak_frame
                if start == end:
                    gain = 0.0
                else:
                    progress = (fi - start) / (end - start)
                    gain = max(0.0, 1.0 - progress)

            if do_partials:
                frames[fi]["amps"] = [a * gain for a in frames[fi]["amps"]]
            if do_residual and frames[fi].get("band_energies"):
                frames[fi]["band_energies"] = [e * gain for e in frames[fi]["band_energies"]]
            frames_modified.add(fi)

    write_ats(header, frames, output_path)

    return {
        "onsets_suppressed": len(onset_indices),
        "onset_indices": onset_indices,
        "mode": mode,
        "target": target,
        "frames_modified": len(frames_modified),
        "output_path": str(output_path),
    }


@mcp.tool()
def render_with_transients(
    ats_path: str,
    wav_path: str,
    amp: float = 1.0,
    transient_gain: float = 1.0,
) -> dict:
    """
    Render an ATS model with the transient re-injection layer mixed in.

    Two-step: ats_synth produces the sinusoidal base; ats_transient --mode render
    produces the transient layer WAV; this tool mixes them in Python.

    Args:
        transient_gain: Scale applied to the transient layer before mixing (default 1.0).
    """
    import tempfile
    import array as _array

    synth_tool = _require_tool("ats_synth")
    trans_tool = _require_tool("ats_transient")

    with tempfile.TemporaryDirectory() as tmp:
        base_wav = str(Path(tmp) / "base.wav")
        trans_wav = str(Path(tmp) / "transient.wav")

        _run([str(synth_tool), "--amp", str(amp), str(ats_path), base_wav], "ats_synth")
        _run([str(trans_tool), "--mode", "render", "--output-wav", trans_wav, str(ats_path)], "ats_transient")

        # Mix: read both WAVs, sum samples, write output
        def _read_wav_samples(p):
            with wave.open(p, "rb") as wf:
                sr = wf.getframerate()
                n = wf.getnframes()
                raw = wf.readframes(n)
                sw = wf.getsampwidth()
            fmt = {1: "b", 2: "h", 4: "i"}.get(sw, "h")
            samps = _array.array(fmt, raw)
            scale = 1.0 / (2 ** (sw * 8 - 1))
            return [s * scale for s in samps], sr, sw

        base_s, sr, sw = _read_wav_samples(base_wav)
        trans_s, _, _  = _read_wav_samples(trans_wav)

        # Pad shorter to same length
        n = max(len(base_s), len(trans_s))
        base_s  += [0.0] * (n - len(base_s))
        trans_s += [0.0] * (n - len(trans_s))
        mixed = [b + transient_gain * t for b, t in zip(base_s, trans_s)]

        # Write output
        peak = max((abs(s) for s in mixed), default=1.0)
        if peak > 1.0:
            mixed = [s / peak for s in mixed]  # normalize to prevent clipping

        fmt = {1: "b", 2: "h", 4: "i"}.get(sw, "h")
        iscale = 2 ** (sw * 8 - 1) - 1
        raw_out = _array.array(fmt, (int(s * iscale) for s in mixed))
        with wave.open(str(wav_path), "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(sw)
            wf.setframerate(sr)
            wf.writeframes(raw_out.tobytes())

    return {"wav_path": str(wav_path), "transient_gain": transient_gain,
            "clipped": peak > 1.0, "peak_before_normalize": round(peak, 4)}


# =============================================================================
# MORPHING
# =============================================================================

@mcp.tool()
def plan_morph(
    ats_path_a: str,
    ats_path_b: str,
    plan_output_path: Optional[str] = None,
) -> dict:
    """
    Compute an optimal-transport correspondence plan between two ATS models.

    Uses buildOptimalTransportPlan() from AtsMorphTransport.h on the Bark-position /
    amplitude-mass representation of each model's mean partial distribution.

    Returns the coupling matrix (source_idx, target_idx, mass) and transport cost.
    Optionally saves the plan JSON to plan_output_path for use with evaluate_morph_metrics.

    Args:
        ats_path_a:        Source model.
        ats_path_b:        Target model.
        plan_output_path:  If given, writes the plan JSON here.
    """
    tool = _require_tool("ats_morph_plan")
    cmd = [str(tool), str(ats_path_a), str(ats_path_b)]
    result = _run(cmd, "ats_morph_plan")
    plan = _parse_json_stdout("ats_morph_plan", result)
    if plan_output_path:
        Path(plan_output_path).write_text(json.dumps(plan, indent=2))
        plan["plan_path"] = str(plan_output_path)
    return plan


# =============================================================================
# BATCH / WORKFLOW
# =============================================================================

@mcp.tool()
def batch_analyze(
    audio_dir: str,
    output_dir: str,
    analysis_type: int = 4,
    glob: str = "*.wav",
    multi_res: bool = False,
    no_slopes: bool = False,
    start_time: float = 0.0,
    duration: float = 0.0,
    lowest_freq_hz: float = 20.0,
    highest_freq_hz: float = 20000.0,
    window_cycles: int = 4,
    window_type: int = 1,
    window_size: int = 1024,
    hop_size: float = 0.25,
    lowest_mag_db: float = -60.0,
    freq_dev: float = 0.1,
    track_len: int = 3,
    min_seg_len: int = 3,
    min_gap_len: int = 3,
    last_peak_cont: float = 0.0,
    smr_cont: float = 0.0,
    smr_threshold_db: float = 30.0,
    min_seg_smr_db: float = 60.0,
    mr_window_mult: float = 2.0,
    mr_crossover_hz: float = 500.0,
) -> dict:
    """
    Analyze all matching WAV files in a directory and write ATS models.

    Accepts the same analysis parameters as analyze_audio and applies them
    uniformly to every file in the batch.

    Returns {"files": [...], "errors": [...], "total": int, "succeeded": int}.
    """
    src = Path(audio_dir)
    dst = Path(output_dir)
    dst.mkdir(parents=True, exist_ok=True)
    kwargs = dict(
        analysis_type=analysis_type, multi_res=multi_res, no_slopes=no_slopes,
        start_time=start_time, duration=duration,
        lowest_freq_hz=lowest_freq_hz, highest_freq_hz=highest_freq_hz,
        window_cycles=window_cycles, window_type=window_type,
        window_size=window_size, hop_size=hop_size,
        lowest_mag_db=lowest_mag_db, freq_dev=freq_dev,
        track_len=track_len, min_seg_len=min_seg_len, min_gap_len=min_gap_len,
        last_peak_cont=last_peak_cont, smr_cont=smr_cont,
        smr_threshold_db=smr_threshold_db, min_seg_smr_db=min_seg_smr_db,
        mr_window_mult=mr_window_mult, mr_crossover_hz=mr_crossover_hz,
    )
    files, errors = [], []
    for wav in sorted(src.glob(glob)):
        ats_out = str(dst / (wav.stem + ".ats"))
        try:
            r = analyze_audio(str(wav), ats_out, **kwargs)
            files.append({"audio": str(wav), "ats": ats_out, "model": r["model"]})
        except Exception as exc:
            errors.append({"audio": str(wav), "error": str(exc)})
    return {"files": files, "errors": errors, "total": len(files) + len(errors), "succeeded": len(files)}


@mcp.tool()
def batch_render(
    ats_dir: str,
    output_dir: str,
    amp: float = 1.0,
    frec: float = 1.0,
    glob: str = "*.ats",
) -> dict:
    """
    Render all ATS models in a directory to WAV files.

    Returns {"files": [...], "errors": [...], "total": int, "succeeded": int}.
    """
    src = Path(ats_dir)
    dst = Path(output_dir)
    dst.mkdir(parents=True, exist_ok=True)
    files, errors = [], []
    for ats in sorted(src.glob(glob)):
        wav_out = str(dst / (ats.stem + ".wav"))
        try:
            render_model(str(ats), wav_out, amp, frec)
            files.append({"ats": str(ats), "wav": wav_out})
        except Exception as exc:
            errors.append({"ats": str(ats), "error": str(exc)})
    return {"files": files, "errors": errors, "total": len(files) + len(errors), "succeeded": len(files)}


@mcp.tool()
def sweep_frec(
    ats_path: str,
    output_dir: str,
    frec_values: list,
    amp: float = 1.0,
) -> dict:
    """
    Render an ATS model at multiple frequency scale factors.

    Args:
        frec_values: List of floats, e.g. [0.5, 1.0, 1.5, 2.0].

    Returns {"renders": [{"frec": float, "wav": str}], "errors": [...]}.
    """
    dst = Path(output_dir)
    dst.mkdir(parents=True, exist_ok=True)
    stem = Path(ats_path).stem
    renders, errors = [], []
    for frec in frec_values:
        tag = f"{frec:.3f}".replace(".", "p")
        wav_out = str(dst / f"{stem}_frec{tag}.wav")
        try:
            render_model(ats_path, wav_out, amp, frec)
            renders.append({"frec": frec, "wav": wav_out})
        except Exception as exc:
            errors.append({"frec": frec, "error": str(exc)})
    return {"renders": renders, "errors": errors}


@mcp.tool()
def generate_variations(
    ats_path: str,
    output_dir: str,
    n: int = 4,
    seed: Optional[int] = None,
    variation_space: Optional[dict] = None,
) -> dict:
    """
    Generate N parametric variations of an ATS model via random parameter walks.

    Each variation calls transform_model then render_model. The random walk
    samples from variation_space bounds (or defaults if not provided).

    Default variation_space:
        {"freq_mode": ["Stretch", "Compress", "Harmonic"],
         "freq_amount": [-0.3, 0.3],
         "amp_scale":   [0.7, 1.3],
         "noise_scale": [0.5, 2.0]}

    Args:
        seed: RNG seed for reproducibility (None = random).
    """
    rng = random.Random(seed)
    space = {
        "freq_mode":   ["Stretch", "Compress", "Harmonic"],
        "freq_amount": [-0.3, 0.3],
        "amp_scale":   [0.7, 1.3],
        "noise_scale": [0.5, 2.0],
    }
    if variation_space:
        space.update(variation_space)

    dst = Path(output_dir)
    dst.mkdir(parents=True, exist_ok=True)
    stem = Path(ats_path).stem
    variations, errors = [], []

    for i in range(n):
        params = {
            "freq_mode":   rng.choice(space["freq_mode"]),
            "freq_amount": rng.uniform(*space["freq_amount"]),
            "amp_scale":   rng.uniform(*space["amp_scale"]),
            "noise_scale": rng.uniform(*space["noise_scale"]),
        }
        ats_out = str(dst / f"{stem}_var{i:02d}.ats")
        wav_out = str(dst / f"{stem}_var{i:02d}.wav")
        try:
            transform_model(ats_path, ats_out, **params)
            render_model(ats_out, wav_out)
            variations.append({"index": i, "params": params, "ats_path": ats_out, "wav_path": wav_out})
        except Exception as exc:
            errors.append({"index": i, "params": params, "error": str(exc)})

    return {"variations": variations, "errors": errors, "seed": seed}


# =============================================================================
# RECIPE
# =============================================================================

_RECIPE_REQUIRED_FIELDS = {
    "engine_version", "schema_version",
    "source_a.id", "source_a.hash", "source_a.name",
    "source_a.sample_rate", "source_a.duration_seconds",
    "source_a.frame_hop_seconds", "source_a.frame_count", "source_a.partial_count",
    "source_b.id", "source_b.hash", "source_b.name",
    "source_b.sample_rate", "source_b.duration_seconds",
    "source_b.frame_hop_seconds", "source_b.frame_count", "source_b.partial_count",
}


def _ats_file_id(path: str) -> str:
    """Stable identifier: SHA-1 of file contents (first 4 KB)."""
    data = Path(path).read_bytes()[:4096]
    return hashlib.sha1(data).hexdigest()[:16]


def _source_metadata_from_ats(prefix: str, ats_path: str) -> dict:
    """Build source_a/source_b key=value lines from an ATS file."""
    s = load_ats(ats_path)
    h = s.header
    hop = h.duration / h.frames if h.frames > 0 else 0.0
    file_hash = _ats_file_id(ats_path)
    name = Path(ats_path).stem
    lines = {
        f"{prefix}.id":                  name,
        f"{prefix}.hash":                file_hash,
        f"{prefix}.name":                name,
        f"{prefix}.sample_rate":         str(h.sr),
        f"{prefix}.duration_seconds":    str(round(h.duration, 6)),
        f"{prefix}.frame_hop_seconds":   str(round(hop, 8)),
        f"{prefix}.lowest_frequency_hz": str(round(s.header.max_freq * 0.01, 2)),
        f"{prefix}.highest_frequency_hz": str(round(h.max_freq, 2)),
        f"{prefix}.channel_count":       "1",
        f"{prefix}.frame_count":         str(h.frames),
        f"{prefix}.partial_count":       str(h.partials),
        f"{prefix}.has_phase":           "1" if h.has_phase() else "0",
        f"{prefix}.has_residual":        "1" if h.has_noise() else "0",
    }
    return lines


@mcp.tool()
def create_recipe(
    ats_path_a: str,
    ats_path_b: str,
    default_morph_amount: float = 0.5,
) -> dict:
    """
    Build an AtsMorphRecipe-compatible key=value text from two ATS files.

    Reads header metadata from both files. Returns the recipe as both a
    flat key=value string ("recipe_text") and a parsed dict for inspection.

    Args:
        ats_path_a:           Source A .ats file.
        ats_path_b:           Source B .ats file.
        default_morph_amount: Initial morph slider value (0=A, 1=B).
    """
    for p in (ats_path_a, ats_path_b):
        if not Path(p).exists():
            raise FileNotFoundError(f"ATS file not found: '{p}'")

    kv: dict = {
        "schema_version":       "1",
        "engine_version":       "morph-recipe-v1",
        "default_morph_amount": str(default_morph_amount),
    }
    kv.update(_source_metadata_from_ats("source_a", ats_path_a))
    kv.update(_source_metadata_from_ats("source_b", ats_path_b))

    recipe_text = "".join(f"{k}={v}\n" for k, v in kv.items())
    return {
        "recipe_text": recipe_text,
        "parsed": kv,
        "source_a_path": str(ats_path_a),
        "source_b_path": str(ats_path_b),
    }


@mcp.tool()
def validate_recipe(recipe_text: str) -> dict:
    """
    Validate an AtsMorphRecipe key=value text string.

    Checks: all required fields present, schema_version=1,
    engine_version present, numeric fields parseable.

    Args:
        recipe_text: The recipe as a key=value newline-separated string.

    Returns {"valid": bool, "errors": [str], "parsed_fields": int}.
    """
    kv = {}
    errors = []
    for line in recipe_text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            errors.append(f"Malformed line (no '='): {line!r}")
            continue
        k, v = line.split("=", 1)
        kv[k.strip()] = v.strip()

    missing = _RECIPE_REQUIRED_FIELDS - set(kv.keys())
    for f in sorted(missing):
        errors.append(f"Missing required field: {f}")

    if kv.get("schema_version") not in ("1", None):
        errors.append(f"Unknown schema_version: {kv.get('schema_version')}")

    for field in ("source_a.sample_rate", "source_a.duration_seconds",
                  "source_b.sample_rate", "source_b.duration_seconds"):
        if field in kv:
            try:
                float(kv[field])
            except ValueError:
                errors.append(f"{field} is not a valid float: {kv[field]!r}")

    return {"valid": len(errors) == 0, "errors": errors, "parsed_fields": len(kv)}


# =============================================================================
# DIAGNOSTICS
# =============================================================================

@mcp.tool()
def check_build() -> dict:
    """
    Check which ATS CLI tools are built and runnable.

    Returns {"tools": {"name": {"found": bool, "path": str|null, "help_ok": bool}}}.
    """
    results = {}
    for name in _ALL_CLI_TOOLS:
        p = _find_tool(name)
        help_ok = False
        if p is not None:
            r = subprocess.run([str(p), "--help"], capture_output=True)
            help_ok = r.returncode in (0, 1)  # many tools exit 1 on --help
        results[name] = {"found": p is not None, "path": str(p) if p else None, "help_ok": help_ok}
    built = sum(1 for v in results.values() if v["found"])
    return {"tools": results, "built": built, "total": len(_ALL_CLI_TOOLS)}


@mcp.tool()
def list_fixtures() -> dict:
    """
    List morph test fixtures from tests/fixtures/morph_fixture_manifest.json.

    Returns the manifest dict (categories, fixture pairs, file paths).
    Returns {"available": false} if the manifest does not exist.
    """
    manifest = _FIXTURES_DIR / "morph_fixture_manifest.json"
    if not manifest.exists():
        return {"available": False, "manifest_path": str(manifest)}
    data = json.loads(manifest.read_text())
    return {"available": True, "manifest_path": str(manifest), "manifest": data}


@mcp.tool()
def estimate_analysis_time(
    wav_path: str,
    analysis_type: int = 4,
    multi_res: bool = False,
) -> dict:
    """
    Heuristic estimate of ATSA analysis time for a WAV file.

    Formula: duration_s × base_factor × type_multiplier × multi_res_multiplier
    where base_factor=0.4 for type 1-2, 0.8 for type 3-4.

    Returns {"estimated_seconds": float, "wav_duration_s": float}.
    """
    p = Path(wav_path)
    if not p.exists():
        raise FileNotFoundError(f"WAV file not found: '{wav_path}'")
    with wave.open(str(p), "rb") as wf:
        dur = wf.getnframes() / wf.getframerate()
    base = 0.4 if analysis_type <= 2 else 0.8
    est = dur * base * (1.5 if multi_res else 1.0)
    return {"estimated_seconds": round(est, 2), "wav_duration_s": round(dur, 4)}


@mcp.tool()
def run_golden_compare() -> dict:
    """
    Run the golden-comparison test harness (tests/golden_compare.py).

    Requires ats_analyze and ats_offline to be built and scipy installed.
    Returns {"passed": bool, "stdout": str, "stderr": str}.
    """
    script = _TESTS_DIR / "golden_compare.py"
    if not script.exists():
        return {"passed": False, "stdout": "", "stderr": f"Script not found: {script}"}
    result = subprocess.run(
        ["python3.11", str(script)], capture_output=True, text=True, cwd=str(_REPO_ROOT)
    )
    return {"passed": result.returncode == 0, "stdout": result.stdout, "stderr": result.stderr}


@mcp.tool()
def evaluate_morph_fixture(fixture_name: Optional[str] = None) -> dict:
    """
    Run the morph metric harness (tests/morph_metric_harness.py).

    Args:
        fixture_name: Specific fixture to evaluate (None = run all).

    Returns {"passed": bool, "report": dict|null, "stdout": str, "stderr": str}.
    """
    script = _TESTS_DIR / "morph_metric_harness.py"
    if not script.exists():
        return {"passed": False, "report": None, "stdout": "", "stderr": f"Script not found: {script}"}
    cmd = ["python3.11", str(script)]
    if fixture_name:
        cmd += ["--fixture", fixture_name]
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=str(_REPO_ROOT))
    report = None
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError:
        pass
    return {"passed": result.returncode == 0, "report": report, "stdout": result.stdout, "stderr": result.stderr}


if __name__ == "__main__":
    mcp.run()

#!/usr/bin/env python3
"""
Classic ATSA/Qatsh compatibility baseline.

Runs ats_offline without a modern-mode flag and verifies that the emitted
metadata is sufficient to identify and reproduce the classic analysis run.
"""

from __future__ import annotations

import json
import math
import os
import struct
import subprocess
import sys
import wave


def write_mono_sine_wav(path: str, sample_rate: int, duration_s: float, freq_hz: float) -> None:
    n = int(sample_rate * duration_s)
    with wave.open(path, "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        for i in range(n):
            s = int(0.35 * 32767.0 * math.sin(2.0 * math.pi * freq_hz * i / sample_rate))
            w.writeframes(struct.pack("<h", s))


def wav_rms_peak(path: str) -> tuple[int, float, float]:
    with wave.open(path, "r") as w:
        n = w.getnframes()
        sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        raise RuntimeError("expected 16-bit WAV")
    samples = struct.unpack("<" + "h" * n, raw)
    peak = max(abs(x) for x in samples) / 32768.0
    rms = math.sqrt(sum((x / 32768.0) ** 2 for x in samples) / max(n, 1))
    return n, rms, peak


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: classic_compat_baseline.py <path-to-ats_offline> <build-dir>", file=sys.stderr)
        return 2

    exe = sys.argv[1]
    build_dir = sys.argv[2]
    os.makedirs(build_dir, exist_ok=True)
    in_wav = os.path.join(build_dir, "classic_compat_in.wav")
    out_wav = os.path.join(build_dir, "classic_compat_out.wav")
    metadata_json = os.path.join(build_dir, "classic_compat_metadata.json")

    write_mono_sine_wav(in_wav, 48000, 0.75, 440.0)
    subprocess.run([exe, "--analysis-mode", "classic_ats", "--metadata", metadata_json, in_wav, out_wav], check=True)

    frames, rms, peak = wav_rms_peak(out_wav)
    require(frames > 1000, f"too few output frames: {frames}")
    require(rms > 1e-5, f"rms too low: {rms}")
    require(peak > 1e-5, f"peak too low: {peak}")

    with open(metadata_json, "r", encoding="utf-8") as f:
        metadata = json.load(f)

    require(metadata["schema_version"] == 1, "unexpected metadata schema")
    require(metadata["analysis_mode"] == "classic_ats", "default analysis mode changed")
    require(
        metadata["tracker_mode"] == "classic_recursive_frequency_smr",
        "default tracker mode changed",
    )
    require(metadata["input_sample_rate"] == 48000, "input sample rate missing from metadata")
    require(metadata["input_frames"] == int(48000 * 0.75), "input frame count mismatch")
    require(metadata["rendered_frames"] == frames, "rendered frame count mismatch")
    require(len(metadata["source_hash_fnv1a64"]) == 16, "source hash missing")
    require(len(metadata["parameter_hash_fnv1a64"]) == 16, "parameter hash missing")

    params = metadata["analysis_params"]
    for key in (
        "start",
        "duration",
        "lowest_freq",
        "highest_freq",
        "freq_dev",
        "win_cycles",
        "win_type",
        "win_size",
        "hop_size",
        "lowest_mag",
        "track_len",
        "min_seg_len",
        "min_gap_len",
        "SMR_thres",
        "min_seg_SMR",
        "last_peak_cont",
        "SMR_cont",
        "type",
        "computed_hop_smp",
        "computed_fft_size",
        "computed_frames",
    ):
        require(key in params, f"analysis parameter missing from metadata: {key}")

    require(params["type"] == 4, "ats_offline default analysis type must stay full classic ATS")
    require(metadata["model"]["sample_rate"] == 48000, "model sample rate missing")
    require(metadata["model"]["partials"] > 0, "classic tracker produced no partials")
    require(metadata["model"]["frames"] > 0, "classic tracker produced no frames")

    print(
        "ok: classic default metadata "
        f"partials={metadata['model']['partials']} frames={metadata['model']['frames']} "
        f"rms={rms:.6g} peak={peak:.6g}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)

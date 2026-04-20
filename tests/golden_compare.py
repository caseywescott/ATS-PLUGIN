#!/usr/bin/env python3
"""
Tolerance-based smoke test for ats_offline (PRD §9, docs/DETERMINISM.md).
Uses analysis type 1 (amp + freq only) for a shorter, more stable path.
"""

from __future__ import annotations

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
            s = int(0.4 * 32767.0 * math.sin(2.0 * math.pi * freq_hz * i / sample_rate))
            w.writeframes(struct.pack("<h", s))


def wav_rms_peak(path: str) -> tuple[float, float]:
    with wave.open(path, "r") as w:
        n = w.getnframes()
        sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        raise RuntimeError("expected 16-bit WAV")
    samples = struct.unpack("<" + "h" * n, raw)
    peak = max(abs(x) for x in samples) / 32768.0
    rms = math.sqrt(sum((x / 32768.0) ** 2 for x in samples) / max(n, 1))
    return rms, peak


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: golden_compare.py <path-to-ats_offline> <build-dir>", file=sys.stderr)
        return 2

    exe = sys.argv[1]
    build_dir = sys.argv[2]
    os.makedirs(build_dir, exist_ok=True)
    in_wav = os.path.join(build_dir, "golden_in.wav")
    out_wav = os.path.join(build_dir, "golden_out.wav")

    write_mono_sine_wav(in_wav, 48000, 0.75, 440.0)

    # Type 1: amplitude + frequency only (no residual band path).
    subprocess.run([exe, "--type", "1", in_wav, out_wav], check=True)

    if not os.path.isfile(out_wav):
        print("missing output", file=sys.stderr)
        return 1

    rms, peak = wav_rms_peak(out_wav)
    with wave.open(out_wav, "r") as w:
        frames = w.getnframes()

    if frames < 1000:
        print(f"too few output frames: {frames}", file=sys.stderr)
        return 1
    if rms < 1e-5:
        print(f"rms too low: {rms}", file=sys.stderr)
        return 1
    if peak < 1e-5:
        print(f"peak too low: {peak}", file=sys.stderr)
        return 1

    print(f"ok: frames={frames} rms={rms:.6g} peak={peak:.6g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

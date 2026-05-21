#!/usr/bin/env python3
"""Smoke test for the Modern ATS offline candidate tool."""

from __future__ import annotations

import json
import math
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


def write_two_tone_wav(path: Path, sample_rate: int = 48000) -> None:
    duration_seconds = 0.30
    frames = int(sample_rate * duration_seconds)
    with wave.open(str(path), "w") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(sample_rate)
        data = bytearray()
        for i in range(frames):
            t = i / sample_rate
            sample = 0.36 * math.sin(2.0 * math.pi * 512.0 * t)
            sample += 0.24 * math.sin(2.0 * math.pi * 768.0 * t + 0.3)
            value = max(-1.0, min(1.0, sample))
            data.extend(struct.pack("<h", int(round(value * 32767.0))))
        handle.writeframes(bytes(data))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: modern_offline_tool_test.py <ats_modern_offline>", file=sys.stderr)
        return 2

    tool = Path(sys.argv[1]).resolve()
    require(tool.is_file(), f"tool not found: {tool}")

    with tempfile.TemporaryDirectory() as raw_tmpdir:
        tmpdir = Path(raw_tmpdir)
        input_wav = tmpdir / "input.wav"
        output_wav = tmpdir / "output.wav"
        metadata_json = tmpdir / "metadata.json"
        write_two_tone_wav(input_wav)

        result = subprocess.run(
            [
                str(tool),
                "--analysis-mode",
                "modern_ats",
                "--metadata",
                str(metadata_json),
                str(input_wav),
                str(output_wav),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        require(result.returncode == 0, f"ats_modern_offline failed: {result.stderr[:500]}")
        require(output_wav.is_file(), "modern offline output wav missing")
        require(metadata_json.is_file(), "modern offline metadata missing")

        metadata = json.loads(metadata_json.read_text(encoding="utf-8"))
        require(metadata.get("analysis_mode") == "modern_ats", "metadata analysis_mode should be modern_ats")
        require(metadata.get("tracker_mode") == "modern_assignment", "metadata tracker_mode should be modern_assignment")
        require(metadata.get("input_sample_rate") == 48000, "metadata input sample rate mismatch")
        require(metadata.get("rendered_frames", 0) > 0, "metadata rendered_frames should be positive")
        require(metadata.get("continuity_score", -1.0) >= 0.0, "continuity_score should be emitted")
        require(metadata.get("lifecycle_breaks", -1) >= 0, "lifecycle_breaks should be emitted")
        require(metadata.get("mean_confidence", -1.0) >= 0.0, "mean_confidence should be emitted")
        require(metadata.get("identity_errors", -1) >= 0, "identity_errors should be emitted")
        require(metadata.get("total_residual_energy", -1.0) >= 0.0, "total_residual_energy should be emitted")
        require(metadata.get("orphan_residual_ratio", -1.0) >= 0.0, "orphan_residual_ratio should be emitted")
        require(metadata.get("attack_sharpness", -1.0) >= 0.0, "attack_sharpness should be emitted")
        model = metadata.get("model", {})
        require(model.get("partials", 0) >= 2, "modern offline model should track the two-tone fixture")
        require(model.get("frames", 0) > 0, "modern offline model should emit frames")

    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

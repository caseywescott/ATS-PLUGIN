#!/usr/bin/env python3
"""Generate ATSA reference artifacts from the CCO spectral-envelope audio fixtures.

This script is NOT part of the normal CI test flow.  Run it manually when:
  - New CCO audio files are added to the fixture directory.
  - Reference metrics need to be updated after a deliberate ATSA parameter change.

The script converts each fixture to mono 16-bit PCM (required by ats_offline),
runs ats_offline with --metadata to capture analysis diagnostics, and writes a
stable summary JSON per fixture.  The summary files are committed to the repo so
that acceptance tests can gate against them without re-running the full analysis.

Usage:
  python3 tests/generate_atsa_references.py \\
    --ats-offline ./build/tools/ats_offline \\
    --audio-dir   /path/to/spectral-envelope-audio \\
    --output-dir  tests/fixtures/atsa_references

Optional:
  --fixture-id <stem>   Process only the named fixture (stem of the WAV filename).
  --dry-run             Print what would be run without executing.
  --type <1-4>          ATS analysis type (default: 4, amp+freq+phase+noise).

Requirements:
  - ats_offline must be built and support --metadata.
  - The soundfile Python package must be importable (pip install soundfile).

Output per fixture:
  <output-dir>/<stem>_atsa_metadata.json  -- raw ats_offline metadata JSON
  <output-dir>/<stem>_summary.json        -- stable sorted summary metrics
"""

from __future__ import annotations

import argparse
import cmath
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
# WAV conversion: stereo / 24-bit -> mono 16-bit (required by ats_offline)
# ---------------------------------------------------------------------------

def convert_to_mono16(src: Path, dst: Path) -> None:
    """Write src as mono 16-bit PCM WAV at dst.  Requires the soundfile package."""
    try:
        import soundfile as sf  # type: ignore
    except ImportError:
        raise SystemExit(
            "The 'soundfile' package is required for format conversion.\n"
            "Install it with:  pip install soundfile"
        )
    data, sr = sf.read(str(src), dtype="float32", always_2d=True)
    mono = data.mean(axis=1)  # mix to mono
    # Clamp to [-1, 1] then scale to int16
    mono = mono.clip(-1.0, 1.0)
    samples_i16 = (mono * 32767.0).astype("int16")
    with wave.open(str(dst), "w") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(samples_i16.tobytes())


# ---------------------------------------------------------------------------
# Reconstruction metrics
# ---------------------------------------------------------------------------

def _wav_float_samples(path: str, max_samples: int = 4096) -> list[float]:
    with wave.open(path, "r") as w:
        n = min(w.getnframes(), max_samples)
        sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        return []
    return [x / 32768.0 for x in struct.unpack("<" + "h" * n, raw)]


def _dft_mag(samples: list[float], b: int, n: int) -> float:
    s = sum(v * cmath.exp(-2j * math.pi * b * i / n) for i, v in enumerate(samples))
    return abs(s) / max(len(samples), 1)


def _spectral_distance_db(ref: list[float], cand: list[float],
                           n_bins: int = 16, window: int = 2048) -> float:
    r = (ref + [0.0] * window)[:window]
    c = (cand + [0.0] * window)[:window]
    dist = 0.0
    for b in range(1, n_bins + 1):
        rd = 20.0 * math.log10(max(_dft_mag(r, b, window), 1e-12))
        cd = 20.0 * math.log10(max(_dft_mag(c, b, window), 1e-12))
        dist += abs(cd - rd)
    return dist / n_bins


def reconstruction_metrics(ref_path: str, cand_path: str) -> dict:
    ref = _wav_float_samples(ref_path)
    cand = _wav_float_samples(cand_path)
    n = max(len(ref), len(cand))
    if n == 0:
        return {"normalized_rms_error": 0.0, "spectral_distance_db": 0.0}
    ref_p  = ref  + [0.0] * (n - len(ref))
    cand_p = cand + [0.0] * (n - len(cand))
    ref_rms = math.sqrt(sum(x * x for x in ref_p) / n)
    err_rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(ref_p, cand_p)) / n)
    return {
        "normalized_rms_error": err_rms / max(ref_rms, 1e-12),
        "spectral_distance_db": _spectral_distance_db(ref_p, cand_p),
    }


# ---------------------------------------------------------------------------
# Summary extraction
# ---------------------------------------------------------------------------

def extract_summary(metadata: dict, recon: dict, fixture_id: str) -> dict:
    model = metadata.get("model", {})
    orr = metadata.get("orphan_residual_ratio")
    return {
        "schema_version": 1,
        "fixture_id": fixture_id,
        "analysis_mode": metadata.get("analysis_mode", "classic_ats"),
        "tracker_mode": metadata.get("tracker_mode", "classic_recursive_frequency_smr"),
        "sample_rate": metadata.get("input_sample_rate", 0),
        "partials": model.get("partials", 0),
        "frames": model.get("frames", 0),
        "duration_seconds": model.get("duration_seconds", 0.0),
        "normalized_rms_error": recon.get("normalized_rms_error", 0.0),
        "spectral_distance_db": recon.get("spectral_distance_db", 0.0),
        # Tracker diagnostics emitted by ats_offline --metadata.
        "continuity_score": metadata.get("continuity_score", -1.0),
        "identity_errors": metadata.get("identity_errors", -1),
        # orphan_residual_ratio is null for analysis types 1 and 2.
        "orphan_residual_ratio": orr if orr is not None else -1.0,
        "source_hash_fnv1a64": metadata.get("source_hash_fnv1a64", ""),
        "parameter_hash_fnv1a64": metadata.get("parameter_hash_fnv1a64", ""),
    }


# ---------------------------------------------------------------------------
# Per-fixture processor
# ---------------------------------------------------------------------------

def process_fixture(
    wav_path: Path,
    output_dir: Path,
    ats_offline: str,
    analysis_type: int,
    dry_run: bool,
) -> bool:
    stem = wav_path.stem
    out_metadata = output_dir / f"{stem}_atsa_metadata.json"
    out_summary  = output_dir / f"{stem}_summary.json"
    out_resynth  = output_dir / f"{stem}_resynth.wav"

    print(f"  {stem}:")

    with tempfile.TemporaryDirectory() as tmpdir:
        # Convert to mono 16-bit if needed.
        mono_wav = Path(tmpdir) / f"{stem}_mono16.wav"
        try:
            convert_to_mono16(wav_path, mono_wav)
        except Exception as exc:
            print(f"    ERROR converting to mono 16-bit: {exc}", file=sys.stderr)
            return False

        cmd = [
            ats_offline,
            "--type", str(analysis_type),
            "--analysis-mode", "classic_ats",
            "--metadata", str(out_metadata),
            str(mono_wav),
            str(out_resynth),
        ]
        print(f"    {' '.join(cmd)}")
        if dry_run:
            print("    (dry-run — skipping execution)")
            return True

        try:
            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError as exc:
            print(f"    ERROR: ats_offline failed: {exc}", file=sys.stderr)
            return False

        if not out_metadata.is_file():
            print(f"    ERROR: metadata not written by ats_offline", file=sys.stderr)
            return False
        if not out_resynth.is_file():
            print(f"    ERROR: resynthesis WAV not written by ats_offline", file=sys.stderr)
            return False

        with open(out_metadata, "r", encoding="utf-8") as f:
            metadata = json.load(f)

        # Compare mono-converted input against resynth for reconstruction quality.
        recon = reconstruction_metrics(str(mono_wav), str(out_resynth))

    summary = extract_summary(metadata, recon, stem)
    out_summary.write_text(
        json.dumps(summary, sort_keys=True, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )

    print(
        f"    ok: partials={summary['partials']} frames={summary['frames']}"
        f" continuity={summary['continuity_score']:.4f}"
        f" identity_errors={summary['identity_errors']}"
        f" orphan_residual={summary['orphan_residual_ratio']:.4f}"
        f" rms_err={summary['normalized_rms_error']:.4g}"
        f" spec_dist={summary['spectral_distance_db']:.4g} dB"
    )
    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate ATSA reference artifacts from CCO spectral-envelope audio."
    )
    parser.add_argument("--ats-offline", required=True,
                        help="Path to ats_offline binary.")
    parser.add_argument("--audio-dir", required=True,
                        help="Directory containing CCO WAV fixtures.")
    parser.add_argument("--output-dir", required=True,
                        help="Directory to write reference artifacts (created if absent).")
    parser.add_argument("--fixture-id", default=None,
                        help="Process only this fixture (stem of the WAV filename).")
    parser.add_argument("--type", dest="analysis_type", type=int, default=4,
                        choices=[1, 2, 3, 4],
                        help="ATS analysis type (default: 4).")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print commands without executing.")
    args = parser.parse_args()

    ats_offline = args.ats_offline
    if not os.path.isfile(ats_offline):
        print(f"error: ats_offline not found: {ats_offline}", file=sys.stderr)
        return 1

    audio_dir = Path(args.audio_dir)
    if not audio_dir.is_dir():
        print(f"error: audio-dir not found: {audio_dir}", file=sys.stderr)
        return 1

    wavs = sorted(audio_dir.glob("*.wav"))
    if not wavs:
        print(f"error: no WAV files found in {audio_dir}", file=sys.stderr)
        return 1

    if args.fixture_id:
        wavs = [w for w in wavs if w.stem == args.fixture_id]
        if not wavs:
            print(f"error: fixture '{args.fixture_id}' not found in {audio_dir}",
                  file=sys.stderr)
            return 1

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Generating ATSA references for {len(wavs)} fixture(s) "
          f"(type={args.analysis_type})...")
    if args.dry_run:
        print("(dry-run mode — no files will be written)")

    all_ok = True
    for wav in wavs:
        ok = process_fixture(wav, output_dir, ats_offline, args.analysis_type,
                              args.dry_run)
        if not ok:
            all_ok = False

    if all_ok:
        print(f"Done. Artifacts written to {output_dir}")
        return 0
    print("One or more fixtures failed. See errors above.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

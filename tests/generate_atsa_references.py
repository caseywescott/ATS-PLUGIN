#!/usr/bin/env python3
"""Generate ATSA/Qatsh reference artifacts from curated external audio fixtures.

This script is NOT part of the normal CI test flow. Run it manually when:
  - New external audio fixtures are added to the manifest and audio files are available.
  - Reference metrics need to be updated after a deliberate ATSA parameter change.

The script runs ats_offline (or an ATSA-compatible tool) on each fixture audio clip,
captures the --metadata JSON, and writes a stable summary_metrics JSON suitable for
side-by-side comparison in the external acceptance test.

Usage:
  python3 tests/generate_atsa_references.py \\
    --ats-offline ./build/tools/ats_offline \\
    --manifest tests/fixtures/ats_external_fixture_manifest.json \\
    --output-dir tests/fixtures/reference

Optional:
  --fixture-id <id>   Process only the named fixture (default: all).
  --dry-run           Print what would be run without executing.

Requirements:
  - ats_offline must support: --analysis-mode classic_ats --metadata <json> <in.wav> <out.wav>
  - Fixture audio files must exist at paths described in the manifest.
  - The output directory will be created if it does not exist.

Output per fixture:
  <output-dir>/<fixture_id>_atsa_metadata.json   -- raw ats_offline metadata
  <output-dir>/<fixture_id>_summary.json          -- stable sorted summary metrics
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import wave
from pathlib import Path


# ---------------------------------------------------------------------------
# WAV helpers
# ---------------------------------------------------------------------------

def wav_metrics(path: str) -> dict:
    """Return frame count, RMS, and peak from a 16-bit mono WAV."""
    with wave.open(path, "r") as w:
        n = w.getnframes()
        sw = w.getsampwidth()
        sr = w.getframerate()
        raw = w.readframes(n)
    if sw != 2:
        raise RuntimeError(f"{path}: expected 16-bit WAV (got {sw * 8}-bit)")
    samples = struct.unpack("<" + "h" * n, raw)
    peak = max(abs(x) for x in samples) / 32768.0 if samples else 0.0
    rms = math.sqrt(sum((x / 32768.0) ** 2 for x in samples) / max(n, 1))
    return {"frames": n, "sample_rate": sr, "rms": rms, "peak": peak}


def dft_magnitude(samples: list[float], bin_index: int, n_bins: int) -> float:
    """Unnormalised DFT magnitude at a single bin (used for compact spectral distance)."""
    import cmath
    scale = -2j * math.pi * bin_index / n_bins
    total = sum(s * cmath.exp(scale * i) for i, s in enumerate(samples))
    return abs(total) / max(len(samples), 1)


def spectral_distance_db(ref_samples: list[float], cand_samples: list[float],
                         n_bins: int = 16, window: int = 2048) -> float:
    """Mean absolute spectral distance in dB over the first n_bins DFT bins."""
    ref = ref_samples[:window]
    cand = cand_samples[:window]
    dist = 0.0
    for b in range(1, n_bins + 1):
        ref_mag = dft_magnitude(ref, b, window)
        cand_mag = dft_magnitude(cand, b, window)
        ref_db = 20.0 * math.log10(max(ref_mag, 1e-12))
        cand_db = 20.0 * math.log10(max(cand_mag, 1e-12))
        dist += abs(cand_db - ref_db)
    return dist / n_bins


def wav_float_samples(path: str, max_samples: int = 4096) -> list[float]:
    """Read up to max_samples from a 16-bit WAV as float [-1, 1]."""
    with wave.open(path, "r") as w:
        n = min(w.getnframes(), max_samples)
        sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        return []
    return [x / 32768.0 for x in struct.unpack("<" + "h" * n, raw)]


def rendered_audio_shape_metrics(path: str) -> dict:
    """Compact rendered-audio proxies for transient and residual/noise checks."""
    samples = wav_float_samples(path)
    if not samples:
        return {
            "attack_sharpness": 0.0,
            "max_buffer_step": 0.0,
            "residual_highpass_ratio": 0.0,
        }

    peak = max(abs(x) for x in samples)
    energy = sum(x * x for x in samples)
    rms_value = math.sqrt(energy / len(samples)) if energy > 0.0 else 0.0
    steps = [samples[i] - samples[i - 1] for i in range(1, len(samples))]
    diff_energy = sum(x * x for x in steps)
    return {
        "attack_sharpness": peak / rms_value if rms_value > 0.0 else 0.0,
        "max_buffer_step": max((abs(x) for x in steps), default=0.0),
        "residual_highpass_ratio": diff_energy / (diff_energy + energy) if energy > 0.0 else 0.0,
    }


# ---------------------------------------------------------------------------
# Normalised reconstruction error between two WAVs
# ---------------------------------------------------------------------------

def reconstruction_metrics(ref_path: str, cand_path: str) -> dict:
    ref = wav_float_samples(ref_path)
    cand = wav_float_samples(cand_path)
    n = max(len(ref), len(cand))
    if n == 0:
        return {
            "normalized_rms_error": 0.0,
            "spectral_distance_db": 0.0,
            "source_attack_sharpness": 0.0,
        }

    ref_padded = ref + [0.0] * (n - len(ref))
    cand_padded = cand + [0.0] * (n - len(cand))

    ref_rms = math.sqrt(sum(x * x for x in ref_padded) / n)
    error_rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(ref_padded, cand_padded)) / n)
    normalized_rms = error_rms / max(ref_rms, 1e-12)
    spec_dist = spectral_distance_db(ref_padded, cand_padded)
    shape = rendered_audio_shape_metrics(cand_path)

    return {
        "normalized_rms_error": normalized_rms,
        "spectral_distance_db": spec_dist,
        "source_attack_sharpness": rendered_audio_shape_metrics(ref_path)["attack_sharpness"],
        **shape,
    }


# ---------------------------------------------------------------------------
# Summary extraction from ats_offline metadata
# ---------------------------------------------------------------------------

def extract_summary(metadata: dict, recon: dict) -> dict:
    """Build stable summary metrics from ats_offline metadata + reconstruction comparison."""
    model = metadata.get("model", {})
    return {
        "schema_version": 1,
        "analysis_mode": metadata.get("analysis_mode", "classic_ats"),
        "tracker_mode": metadata.get("tracker_mode", "classic_recursive_frequency_smr"),
        "sample_rate": metadata.get("input_sample_rate", 0),
        "partials": model.get("partials", 0),
        "frames": model.get("frames", 0),
        "normalized_rms_error": recon.get("normalized_rms_error", 0.0),
        "spectral_distance_db": recon.get("spectral_distance_db", 0.0),
        # Continuity and identity are model diagnostics; residual/transient
        # checks prefer compact rendered-audio proxies when available.
        "continuity_score": metadata.get("continuity_score", -1.0),
        "identity_errors": metadata.get("identity_errors", -1),
        "partial_noise_energy": metadata.get("partial_noise_energy", -1.0),
        "orphan_residual_energy": metadata.get("orphan_residual_energy", -1.0),
        "total_residual_energy": metadata.get("total_residual_energy", -1.0),
        "orphan_residual_ratio": metadata.get("orphan_residual_ratio", -1.0),
        "residual_highpass_ratio": recon.get(
            "residual_highpass_ratio",
            metadata.get("residual_highpass_ratio", -1.0),
        ),
        "source_attack_sharpness": recon.get("source_attack_sharpness", -1.0),
        "attack_sharpness": recon.get("attack_sharpness", metadata.get("attack_sharpness", -1.0)),
        "max_buffer_step": recon.get("max_buffer_step", metadata.get("max_buffer_step", -1.0)),
    }


# ---------------------------------------------------------------------------
# Core processing
# ---------------------------------------------------------------------------

def process_fixture(
    fixture: dict,
    manifest_dir: Path,
    output_dir: Path,
    ats_offline: str,
    dry_run: bool,
) -> bool:
    fixture_id = fixture["id"]
    audio_rel = fixture["input_audio"]
    audio_path = manifest_dir / audio_rel

    ref_metadata_rel = fixture.get("atsa_reference", {}).get("metadata", "")
    ref_summary_rel = fixture.get("atsa_reference", {}).get("summary_metrics", "")

    if not audio_path.is_file():
        print(f"  SKIP {fixture_id}: audio not found at {audio_path}", file=sys.stderr)
        print(f"       Provide the file or update the manifest input_audio path.")
        return True  # not a failure — asset just not present

    license_text = fixture.get("provenance", {}).get("license", "")
    if "PENDING" in license_text.upper():
        print(f"  ERROR {fixture_id}: license is still PENDING. "
              f"Resolve license before generating references.", file=sys.stderr)
        return False

    out_wav = output_dir / f"{fixture_id}_resynth.wav"
    out_metadata = output_dir / f"{fixture_id}_atsa_metadata.json"
    out_summary = output_dir / f"{fixture_id}_summary.json"

    cmd = [
        ats_offline,
        "--analysis-mode", "classic_ats",
        "--metadata", str(out_metadata),
        str(audio_path),
        str(out_wav),
    ]
    print(f"  {fixture_id}: {' '.join(cmd)}")
    if dry_run:
        print(f"    (dry-run — skipping execution)")
        return True

    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        print(f"  ERROR {fixture_id}: ats_offline failed: {exc}", file=sys.stderr)
        return False

    if not out_metadata.is_file():
        print(f"  ERROR {fixture_id}: metadata not written by ats_offline", file=sys.stderr)
        return False
    if not out_wav.is_file():
        print(f"  ERROR {fixture_id}: resynthesis WAV not written by ats_offline", file=sys.stderr)
        return False

    with open(out_metadata, "r", encoding="utf-8") as f:
        metadata = json.load(f)

    recon = reconstruction_metrics(str(audio_path), str(out_wav))
    summary = extract_summary(metadata, recon)
    summary["fixture_id"] = fixture_id

    # Write with sorted keys and stable formatting for reproducibility.
    summary_text = json.dumps(summary, sort_keys=True, indent=2, ensure_ascii=True) + "\n"
    out_summary.write_text(summary_text, encoding="utf-8")

    print(f"  ok {fixture_id}: partials={summary['partials']} frames={summary['frames']}"
          f" rms_err={summary['normalized_rms_error']:.4g}"
          f" spec_dist={summary['spectral_distance_db']:.4g} dB")
    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate ATSA reference artifacts from external audio fixtures."
    )
    parser.add_argument("--ats-offline", required=True,
                        help="Path to ats_offline binary (must support --metadata).")
    parser.add_argument("--manifest", required=True,
                        help="Path to ats_external_fixture_manifest.json.")
    parser.add_argument("--output-dir", required=True,
                        help="Directory to write reference artifacts (created if absent).")
    parser.add_argument("--fixture-id", default=None,
                        help="Process only this fixture id (default: all).")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print commands without executing.")
    args = parser.parse_args()

    manifest_path = Path(args.manifest).resolve()
    if not manifest_path.is_file():
        print(f"error: manifest not found: {manifest_path}", file=sys.stderr)
        return 1

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        print(f"error: unsupported manifest schema_version (expected 1)", file=sys.stderr)
        return 1

    fixtures = manifest.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        print("error: manifest fixtures must be a non-empty list", file=sys.stderr)
        return 1

    if args.fixture_id:
        fixtures = [f for f in fixtures if f.get("id") == args.fixture_id]
        if not fixtures:
            print(f"error: fixture id {args.fixture_id!r} not found in manifest", file=sys.stderr)
            return 1

    ats_offline = args.ats_offline
    if not os.path.isfile(ats_offline):
        print(f"error: ats_offline not found: {ats_offline}", file=sys.stderr)
        return 1

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    manifest_dir = manifest_path.parent
    print(f"Generating ATSA references for {len(fixtures)} fixture(s)...")
    if args.dry_run:
        print("(dry-run mode — no files will be written)")

    all_ok = True
    for fixture in fixtures:
        ok = process_fixture(fixture, manifest_dir, output_dir, ats_offline, args.dry_run)
        if not ok:
            all_ok = False

    if all_ok:
        print("Done.")
        return 0
    else:
        print("One or more fixtures failed. See errors above.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""External real-audio acceptance test against ATSA/Qatsh reference artifacts.

This test is gated by the CMake option ATS_ENABLE_EXTERNAL_FIXTURES=ON.
It does NOT run in the default CI ctest pass — only when external audio assets
and pre-generated reference metrics are present.

Usage (normally invoked by CTest, not directly):
  python3 run_external_acceptance.py \\
    <path-to-ats_offline> \\
    <path-to-ats_external_fixture_manifest.json> \\
    <path-to-reference-dir>

Exit codes:
  0  All configured fixtures produced checks and passed.
  1  One or more fixtures failed, were skipped, or produced no checks.
  2  Argument or manifest error.
"""

from __future__ import annotations

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
# Helpers
# ---------------------------------------------------------------------------

def wav_float_samples(path: str, max_samples: int = 4096) -> list[float]:
    with wave.open(path, "r") as w:
        n = min(w.getnframes(), max_samples)
        sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        return []
    return [x / 32768.0 for x in struct.unpack("<" + "h" * n, raw)]


def rms(samples: list[float]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(x * x for x in samples) / len(samples))


def dft_mag(samples: list[float], b: int, n: int) -> float:
    import cmath
    s = sum(v * cmath.exp(-2j * math.pi * b * i / n) for i, v in enumerate(samples))
    return abs(s) / max(len(samples), 1)


def spectral_distance_db(ref: list[float], cand: list[float],
                         n_bins: int = 16, window: int = 2048) -> float:
    r = (ref + [0.0] * window)[:window]
    c = (cand + [0.0] * window)[:window]
    dist = 0.0
    for b in range(1, n_bins + 1):
        rd = 20.0 * math.log10(max(dft_mag(r, b, window), 1e-12))
        cd = 20.0 * math.log10(max(dft_mag(c, b, window), 1e-12))
        dist += abs(cd - rd)
    return dist / n_bins


def reconstruction_metrics(ref_path: str, cand_path: str) -> dict:
    ref = wav_float_samples(ref_path)
    cand = wav_float_samples(cand_path)
    n = max(len(ref), len(cand))
    if n == 0:
        return {"normalized_rms_error": 0.0, "spectral_distance_db": 0.0}
    ref_p = ref + [0.0] * (n - len(ref))
    cand_p = cand + [0.0] * (n - len(cand))
    ref_rms = rms(ref_p)
    err_rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(ref_p, cand_p)) / n)
    return {
        "normalized_rms_error": err_rms / max(ref_rms, 1e-12),
        "spectral_distance_db": spectral_distance_db(ref_p, cand_p),
    }


# ---------------------------------------------------------------------------
# Gate evaluation
# ---------------------------------------------------------------------------

# Effective defaults for threshold fields (may be overridden per fixture).
GATE_DEFAULTS = {
    "max_normalized_rms_error_delta": 5.0e-4,
    "max_spectral_distance_db_delta": 0.5,
    "max_continuity_regression": 0.05,
    "max_identity_error_increase": 2,
    "min_residual_energy_ratio": 0.99,
    "min_transient_sharpness_improvement": 0.05,
}


def merged_thresholds(overrides: dict) -> dict:
    t = dict(GATE_DEFAULTS)
    for key, value in overrides.items():
        # Manifest keys use underscores; map directly.
        t[key] = value
    return t


class CheckResult:
    def __init__(self, check_id: str, passed: bool, observed: float,
                 threshold: float, reference: float, detail: str = ""):
        self.check_id = check_id
        self.passed = passed
        self.observed = observed
        self.threshold = threshold
        self.reference = reference
        self.detail = detail

    def format(self) -> str:
        status = "pass" if self.passed else "FAIL"
        s = (f"  [{status}] {self.check_id}"
             f" observed={self.observed:.6g}"
             f" threshold={self.threshold:.6g}"
             f" reference={self.reference:.6g}")
        if self.detail:
            s += f" -- {self.detail}"
        return s


def check(results: list[CheckResult], check_id: str, passed: bool,
          observed: float, threshold: float, reference: float, detail: str = "") -> None:
    results.append(CheckResult(check_id, passed, observed, threshold, reference, detail))


def evaluate_classic_parity(fixture_id: str, our: dict, ref: dict,
                             thresholds: dict) -> list[CheckResult]:
    results: list[CheckResult] = []
    our_nrms = our.get("normalized_rms_error", 0.0)
    ref_nrms = ref.get("normalized_rms_error", 0.0)
    delta_nrms = abs(our_nrms - ref_nrms)
    limit = thresholds["max_normalized_rms_error_delta"]
    check(results, f"{fixture_id}.classic_parity.normalized_rms_error",
          delta_nrms <= limit, delta_nrms, limit, ref_nrms,
          "our normalized RMS error should stay close to ATSA reference")

    our_spec = our.get("spectral_distance_db", 0.0)
    ref_spec = ref.get("spectral_distance_db", 0.0)
    delta_spec = abs(our_spec - ref_spec)
    spec_limit = thresholds["max_spectral_distance_db_delta"]
    check(results, f"{fixture_id}.classic_parity.spectral_distance_db",
          delta_spec <= spec_limit, delta_spec, spec_limit, ref_spec,
          "our spectral distance should stay close to ATSA reference")
    return results


def evaluate_tracker_continuity(fixture_id: str, our: dict, ref: dict,
                                 thresholds: dict) -> list[CheckResult]:
    results: list[CheckResult] = []
    our_cont = our.get("continuity_score", -1.0)
    ref_cont = ref.get("continuity_score", -1.0)
    if our_cont < 0.0 or ref_cont < 0.0:
        print(f"    note: continuity_score not available for {fixture_id} — gate skipped")
        return results
    regression = ref_cont - our_cont
    limit = thresholds["max_continuity_regression"]
    check(results, f"{fixture_id}.tracker_continuity.continuity_score",
          regression <= limit, our_cont, ref_cont - limit, ref_cont,
          "our tracker continuity should not materially regress vs ATSA")

    our_ie = our.get("identity_errors", -1)
    ref_ie = ref.get("identity_errors", -1)
    if our_ie >= 0 and ref_ie >= 0:
        ie_increase = our_ie - ref_ie
        ie_limit = thresholds["max_identity_error_increase"]
        check(results, f"{fixture_id}.tracker_continuity.identity_errors",
              ie_increase <= ie_limit, float(our_ie), float(ref_ie + ie_limit), float(ref_ie),
              "our tracker identity errors should not materially exceed ATSA reference")
    return results


def evaluate_residual_energy(fixture_id: str, our: dict, ref: dict,
                              thresholds: dict) -> list[CheckResult]:
    results: list[CheckResult] = []
    our_ratio = our.get("orphan_residual_ratio", -1.0)
    ref_ratio = ref.get("orphan_residual_ratio", -1.0)
    if our_ratio < 0.0 or ref_ratio < 0.0:
        print(f"    note: orphan_residual_ratio not available for {fixture_id} — gate skipped")
        return results
    limit = thresholds["min_residual_energy_ratio"]
    # We check that our ratio stays within acceptable bounds relative to reference.
    delta = abs(our_ratio - ref_ratio)
    check(results, f"{fixture_id}.residual_energy_preservation.orphan_residual_ratio",
          delta <= (1.0 - limit), delta, 1.0 - limit, ref_ratio,
          "our orphan residual ratio should match ATSA reference within tolerance")
    return results


def evaluate_transient_preservation(fixture_id: str, our: dict, ref: dict,
                                     thresholds: dict) -> list[CheckResult]:
    results: list[CheckResult] = []
    our_sharp = our.get("attack_sharpness", -1.0)
    ref_sharp = ref.get("attack_sharpness", -1.0)
    if our_sharp < 0.0 or ref_sharp < 0.0:
        print(f"    note: attack_sharpness not available for {fixture_id} — gate skipped")
        return results
    improvement_needed = thresholds["min_transient_sharpness_improvement"]
    threshold_val = ref_sharp - improvement_needed
    check(results, f"{fixture_id}.transient_preservation.attack_sharpness",
          our_sharp >= threshold_val, our_sharp, threshold_val, ref_sharp,
          "our transient attack sharpness should not fall far below ATSA reference")
    return results


GATE_EVALUATORS = {
    "classic_parity": evaluate_classic_parity,
    "tracker_continuity": evaluate_tracker_continuity,
    "residual_energy_preservation": evaluate_residual_energy,
    "transient_preservation": evaluate_transient_preservation,
}


# ---------------------------------------------------------------------------
# Per-fixture runner
# ---------------------------------------------------------------------------

def run_fixture(fixture: dict, manifest_dir: Path, reference_dir: Path,
                ats_offline: str) -> tuple[bool, int, int]:
    """Run one fixture. Returns (any_failures, checks_run, checks_skipped)."""
    fixture_id = fixture["id"]
    audio_rel = fixture.get("input_audio", "")
    audio_path = manifest_dir / audio_rel
    summary_rel = fixture.get("atsa_reference", {}).get("summary_metrics", "")
    summary_path = manifest_dir / summary_rel if summary_rel else None

    print(f"\nFixture: {fixture_id} ({fixture.get('category', '?')})")

    if not audio_path.is_file():
        print(f"  SKIP — audio asset not found: {audio_path}")
        print(f"         Run generate_atsa_references.py once the audio file is available.")
        return False, 0, 1

    if summary_path is None or not summary_path.is_file():
        print(f"  SKIP — reference metrics not found: {summary_path}")
        print(f"         Run: python3 tests/generate_atsa_references.py --ats-offline <bin> "
              f"--manifest ... --output-dir tests/fixtures/reference")
        return False, 0, 1

    ref_metrics = json.loads(summary_path.read_text(encoding="utf-8"))

    # Run ats_offline on the fixture audio to produce our metrics.
    with tempfile.TemporaryDirectory() as tmpdir:
        out_wav = os.path.join(tmpdir, "out.wav")
        out_meta = os.path.join(tmpdir, "meta.json")
        cmd = [
            ats_offline,
            "--analysis-mode", "classic_ats",
            "--metadata", out_meta,
            str(audio_path),
            out_wav,
        ]
        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except subprocess.CalledProcessError as exc:
            print(f"  ERROR — ats_offline failed: {exc.stderr.decode(errors='replace')[:200]}",
                  file=sys.stderr)
            return True, 0, 0

        our_metadata = json.loads(Path(out_meta).read_text(encoding="utf-8"))
        recon = reconstruction_metrics(str(audio_path), out_wav)

    our_metrics: dict = {
        "normalized_rms_error": recon["normalized_rms_error"],
        "spectral_distance_db": recon["spectral_distance_db"],
        "continuity_score": our_metadata.get("continuity_score", -1.0),
        "identity_errors": our_metadata.get("identity_errors", -1),
        "orphan_residual_ratio": our_metadata.get("orphan_residual_ratio", -1.0),
        "attack_sharpness": our_metadata.get("attack_sharpness", -1.0),
        "max_buffer_step": our_metadata.get("max_buffer_step", -1.0),
    }

    enabled_gates = fixture.get("enabled_gates", [])
    overrides = fixture.get("threshold_overrides", {})
    thresholds = merged_thresholds(overrides)

    all_checks: list[CheckResult] = []
    unknown_gates: list[str] = []
    for gate in enabled_gates:
        evaluator = GATE_EVALUATORS.get(gate)
        if evaluator is None:
            unknown_gates.append(gate)
            continue
        all_checks.extend(evaluator(fixture_id, our_metrics, ref_metrics, thresholds))

    if unknown_gates:
        print(f"  WARNING: unknown gate names ignored: {unknown_gates}", file=sys.stderr)

    any_fail = bool(unknown_gates)
    for result in all_checks:
        print(result.format())
        if not result.passed:
            any_fail = True

    if not all_checks:
        print(f"  FAIL — no gate checks produced (gates={enabled_gates})", file=sys.stderr)
        any_fail = True

    return any_fail, len(all_checks), 0


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: run_external_acceptance.py <ats_offline> <manifest.json> <reference-dir>",
            file=sys.stderr,
        )
        return 2

    ats_offline = sys.argv[1]
    manifest_path = Path(sys.argv[2]).resolve()
    reference_dir = Path(sys.argv[3]).resolve()

    if not os.path.isfile(ats_offline):
        print(f"error: ats_offline not found: {ats_offline}", file=sys.stderr)
        return 2
    if not manifest_path.is_file():
        print(f"error: manifest not found: {manifest_path}", file=sys.stderr)
        return 2

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        print("error: unsupported manifest schema_version (expected 1)", file=sys.stderr)
        return 2

    fixtures = manifest.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        print("error: manifest fixtures must be a non-empty list", file=sys.stderr)
        return 2

    manifest_dir = manifest_path.parent
    print(f"External acceptance: {len(fixtures)} fixture(s) in manifest")
    print(f"Reference dir:       {reference_dir}")

    total_failed = 0
    total_checks = 0
    total_skipped = 0

    for fixture in fixtures:
        any_fail, n_checks, n_skip = run_fixture(
            fixture, manifest_dir, reference_dir, ats_offline
        )
        total_checks += n_checks
        total_skipped += n_skip
        if any_fail:
            total_failed += 1

    print(f"\nSummary: {total_checks} checks run, "
          f"{total_skipped} fixture(s) skipped (assets missing), "
          f"{total_failed} fixture(s) failed.")

    if total_failed > 0:
        print("EXTERNAL ACCEPTANCE FAILED", file=sys.stderr)
        return 1

    if total_skipped > 0:
        print("EXTERNAL ACCEPTANCE INCOMPLETE", file=sys.stderr)
        print("One or more configured fixtures were skipped because audio assets or reference metrics are missing.",
              file=sys.stderr)
        print("Add licensed audio files and generated reference metrics, then re-run.", file=sys.stderr)
        return 1

    if total_checks == 0:
        print("EXTERNAL ACCEPTANCE FAILED", file=sys.stderr)
        print("No gate checks were emitted. Check enabled_gates and reference metric fields.", file=sys.stderr)
        return 1

    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

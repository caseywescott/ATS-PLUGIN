#!/usr/bin/env python3
"""External real-audio acceptance test against ATSA/Qatsh reference artifacts.

This test is gated by the CMake option ATS_ENABLE_EXTERNAL_FIXTURES=ON.
It does NOT run in the default CI ctest pass — only when external audio assets
and pre-generated reference metrics are present.

Usage (normally invoked by CTest, not directly):
  python3 run_external_acceptance.py \\
    <path-to-modern-candidate-offline> \\
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

def wav_float_samples(path: str, max_samples=4096) -> list[float]:
    with wave.open(path, "r") as w:
        frame_count = w.getnframes()
        n = frame_count if max_samples is None else min(frame_count, max_samples)
        sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw != 2:
        return []
    return [x / 32768.0 for x in struct.unpack("<" + "h" * n, raw)]


def rendered_audio_shape_metrics(path: str) -> dict:
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


def spectral_distance_for_bins(ref: list[float], cand: list[float],
                               bins: range, window: int = 2048) -> float:
    r = (ref + [0.0] * window)[:window]
    c = (cand + [0.0] * window)[:window]
    values: list[float] = []
    for b in bins:
        rd = 20.0 * math.log10(max(dft_mag(r, b, window), 1e-12))
        cd = 20.0 * math.log10(max(dft_mag(c, b, window), 1e-12))
        values.append(abs(cd - rd))
    return sum(values) / max(len(values), 1)


def normalized_rms_error_for_segment(ref: list[float], cand: list[float],
                                     start: int, end: int) -> float:
    if end <= start:
        return 0.0
    ref_seg = ref[start:end]
    cand_seg = cand[start:end]
    n = min(len(ref_seg), len(cand_seg))
    if n == 0:
        return 0.0
    ref_seg = ref_seg[:n]
    cand_seg = cand_seg[:n]
    ref_rms = rms(ref_seg)
    err_rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(ref_seg, cand_seg)) / n)
    return err_rms / max(ref_rms, 1e-12)


def decay_envelope_metrics(ref: list[float], cand: list[float],
                           window: int = 4096, hop: int = 2048) -> dict:
    n = max(len(ref), len(cand))
    if n == 0:
        return {
            "decay_envelope_error": 0.0,
            "decay_energy_ratio": 1.0,
            "decay_envelope_correlation": 1.0,
            "decay_level_bias": 0.0,
            "decay_envelope_error_early": 0.0,
            "decay_envelope_error_middle": 0.0,
            "decay_envelope_error_late": 0.0,
            "dominant_decay_error_region": "none",
        }

    ref_p = ref + [0.0] * (n - len(ref))
    cand_p = cand + [0.0] * (n - len(cand))
    source_env: list[float] = []
    candidate_env: list[float] = []
    for start in range(0, n, hop):
        end = min(n, start + window)
        if end <= start:
            continue
        source_env.append(rms(ref_p[start:end]))
        candidate_env.append(rms(cand_p[start:end]))

    if not source_env:
        return {
            "decay_envelope_error": 0.0,
            "decay_energy_ratio": 1.0,
            "decay_envelope_correlation": 1.0,
            "decay_level_bias": 0.0,
            "decay_envelope_error_early": 0.0,
            "decay_envelope_error_middle": 0.0,
            "decay_envelope_error_late": 0.0,
            "dominant_decay_error_region": "none",
        }

    err = [c - s for s, c in zip(source_env, candidate_env)]
    source_env_rms = math.sqrt(sum(x * x for x in source_env) / len(source_env))
    error_rms = math.sqrt(sum(x * x for x in err) / len(err))
    source_energy = sum(x * x for x in source_env)
    candidate_energy = sum(x * x for x in candidate_env)
    source_mean = sum(source_env) / len(source_env)
    candidate_mean = sum(candidate_env) / len(candidate_env)
    source_var = sum((x - source_mean) ** 2 for x in source_env)
    candidate_var = sum((x - candidate_mean) ** 2 for x in candidate_env)
    covariance = sum((s - source_mean) * (c - candidate_mean)
                     for s, c in zip(source_env, candidate_env))
    correlation = (covariance / math.sqrt(source_var * candidate_var)
                   if source_var > 1.0e-20 and candidate_var > 1.0e-20 else 1.0)

    region_names = ["early", "middle", "late"]
    region_errors: dict[str, float] = {}
    for region_index, region_name in enumerate(region_names):
        start_index = region_index * len(err) // len(region_names)
        end_index = (region_index + 1) * len(err) // len(region_names)
        region_source = source_env[start_index:end_index]
        region_err = err[start_index:end_index]
        denom = math.sqrt(sum(x * x for x in region_source) / max(len(region_source), 1))
        region_error = math.sqrt(sum(x * x for x in region_err) / max(len(region_err), 1))
        region_errors[region_name] = region_error / max(denom, 1.0e-12)

    dominant_region = max(region_errors, key=region_errors.get)
    return {
        "decay_envelope_error": error_rms / max(source_env_rms, 1.0e-12),
        "decay_energy_ratio": candidate_energy / max(source_energy, 1.0e-12),
        "decay_envelope_correlation": correlation,
        "decay_level_bias": sum(err) / max(sum(source_env), 1.0e-12),
        "decay_envelope_error_early": region_errors["early"],
        "decay_envelope_error_middle": region_errors["middle"],
        "decay_envelope_error_late": region_errors["late"],
        "dominant_decay_error_region": dominant_region,
    }


def reconstruction_attribution_metrics(ref_path: str, cand_path: str) -> dict:
    ref = wav_float_samples(ref_path, max_samples=None)
    cand = wav_float_samples(cand_path, max_samples=None)
    n = max(len(ref), len(cand))
    if n == 0:
        return {
            "attack_normalized_rms_error": 0.0,
            "tail_normalized_rms_error": 0.0,
            "dominant_time_error_region": "none",
            "spectral_distance_low_db": 0.0,
            "spectral_distance_mid_db": 0.0,
            "spectral_distance_high_db": 0.0,
            "dominant_spectral_error_band": "none",
            **decay_envelope_metrics(ref, cand),
        }

    ref_p = ref + [0.0] * (n - len(ref))
    cand_p = cand + [0.0] * (n - len(cand))
    segment = min(2048, n)
    attack_nrms = normalized_rms_error_for_segment(ref_p, cand_p, 0, segment)
    tail_nrms = normalized_rms_error_for_segment(ref_p, cand_p, n - segment, n)
    bands = {
        "low": spectral_distance_for_bins(ref_p, cand_p, range(1, 5)),
        "mid": spectral_distance_for_bins(ref_p, cand_p, range(5, 11)),
        "high": spectral_distance_for_bins(ref_p, cand_p, range(11, 17)),
    }
    dominant_band = max(bands, key=bands.get)
    dominant_time = "attack" if attack_nrms >= tail_nrms else "tail"
    return {
        "attack_normalized_rms_error": attack_nrms,
        "tail_normalized_rms_error": tail_nrms,
        "dominant_time_error_region": dominant_time,
        "spectral_distance_low_db": bands["low"],
        "spectral_distance_mid_db": bands["mid"],
        "spectral_distance_high_db": bands["high"],
        "dominant_spectral_error_band": dominant_band,
        **decay_envelope_metrics(ref_p, cand_p),
    }


def reconstruction_metrics(ref_path: str, cand_path: str) -> dict:
    ref = wav_float_samples(ref_path, max_samples=None)
    cand = wav_float_samples(cand_path, max_samples=None)
    n = max(len(ref), len(cand))
    if n == 0:
        return {
            "normalized_rms_error": 0.0,
            "spectral_distance_db": 0.0,
            "source_residual_highpass_ratio": 0.0,
            "source_attack_sharpness": 0.0,
        }
    ref_p = ref + [0.0] * (n - len(ref))
    cand_p = cand + [0.0] * (n - len(cand))
    ref_rms = rms(ref_p)
    err_rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(ref_p, cand_p)) / n)
    return {
        "normalized_rms_error": err_rms / max(ref_rms, 1e-12),
        "spectral_distance_db": spectral_distance_db(ref_p, cand_p),
        "source_residual_highpass_ratio": rendered_audio_shape_metrics(ref_path)["residual_highpass_ratio"],
        "source_attack_sharpness": rendered_audio_shape_metrics(ref_path)["attack_sharpness"],
        **reconstruction_attribution_metrics(ref_path, cand_path),
        **rendered_audio_shape_metrics(cand_path),
    }


# ---------------------------------------------------------------------------
# Gate evaluation
# ---------------------------------------------------------------------------

# Effective defaults for threshold fields (may be overridden per fixture).
GATE_DEFAULTS = {
    "max_normalized_rms_error_delta": 5.0e-4,
    "max_spectral_distance_db_delta": 0.5,
    "max_source_normalized_rms_error": 0.65,
    "max_source_spectral_distance_db": 3.5,
    "max_continuity_regression": 0.05,
    "max_identity_error_increase": 2,
    "max_residual_highpass_ratio_delta": 0.005,
    "min_residual_energy_ratio": 0.99,
    "min_transient_sharpness_improvement": 0.05,
    "min_transient_sharpness_ratio": 0.95,
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
    limit = ref_nrms + thresholds["max_normalized_rms_error_delta"]
    check(results, f"{fixture_id}.classic_parity.normalized_rms_error",
          our_nrms <= limit, our_nrms, limit, ref_nrms,
          "our normalized RMS error should not regress beyond ATSA reference")
    source_nrms_limit = thresholds.get("max_source_normalized_rms_error", -1.0)
    if source_nrms_limit >= 0.0:
        check(results, f"{fixture_id}.classic_parity.source_normalized_rms_error",
              our_nrms <= source_nrms_limit, our_nrms, source_nrms_limit, ref_nrms,
              "our normalized RMS error should stay within the source-quality ceiling")

    our_spec = our.get("spectral_distance_db", 0.0)
    ref_spec = ref.get("spectral_distance_db", 0.0)
    spec_limit = ref_spec + thresholds["max_spectral_distance_db_delta"]
    check(results, f"{fixture_id}.classic_parity.spectral_distance_db",
          our_spec <= spec_limit, our_spec, spec_limit, ref_spec,
          "our spectral distance should not regress beyond ATSA reference")
    source_spec_limit = thresholds.get("max_source_spectral_distance_db", -1.0)
    if source_spec_limit >= 0.0:
        check(results, f"{fixture_id}.classic_parity.source_spectral_distance_db",
              our_spec <= source_spec_limit, our_spec, source_spec_limit, ref_spec,
              "our spectral distance should stay within the source-quality ceiling")
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
    our_ratio = our.get("residual_highpass_ratio", our.get("orphan_residual_ratio", -1.0))
    baseline_ratio = our.get("source_residual_highpass_ratio",
                             ref.get("source_residual_highpass_ratio", -1.0))
    if baseline_ratio < 0.0:
        baseline_ratio = ref.get("residual_highpass_ratio", ref.get("orphan_residual_ratio", -1.0))
    if our_ratio < 0.0 or baseline_ratio < 0.0:
        print(f"    note: residual_highpass_ratio not available for {fixture_id} — gate skipped")
        return results
    limit = thresholds.get(
        "max_residual_highpass_ratio_delta",
        1.0 - thresholds["min_residual_energy_ratio"],
    )
    delta = abs(our_ratio - baseline_ratio)
    check(results, f"{fixture_id}.residual_energy_preservation.residual_highpass_ratio",
          delta <= limit, delta, limit, baseline_ratio,
          "our rendered residual/noise ratio should preserve the source shape")
    return results


def evaluate_transient_preservation(fixture_id: str, our: dict, ref: dict,
                                     thresholds: dict) -> list[CheckResult]:
    results: list[CheckResult] = []
    our_sharp = our.get("attack_sharpness", -1.0)
    baseline_sharp = our.get("source_attack_sharpness", ref.get("source_attack_sharpness", -1.0))
    if baseline_sharp < 0.0:
        baseline_sharp = ref.get("attack_sharpness", -1.0)
    if our_sharp < 0.0 or baseline_sharp < 0.0:
        print(f"    note: attack_sharpness not available for {fixture_id} — gate skipped")
        return results
    ratio = our_sharp / max(baseline_sharp, 1e-12)
    threshold_val = thresholds.get(
        "min_transient_sharpness_ratio",
        1.0 - thresholds["min_transient_sharpness_improvement"],
    )
    check(results, f"{fixture_id}.transient_preservation.attack_sharpness",
          ratio >= threshold_val, ratio, threshold_val, baseline_sharp,
          "our transient attack sharpness should preserve the source attack")
    return results


GATE_EVALUATORS = {
    "classic_parity": evaluate_classic_parity,
    "tracker_continuity": evaluate_tracker_continuity,
    "residual_energy_preservation": evaluate_residual_energy,
    "transient_preservation": evaluate_transient_preservation,
}


# ---------------------------------------------------------------------------
# Diagnostic reporting
# ---------------------------------------------------------------------------

def float_metric(metrics: dict, key: str, default: float = 0.0) -> float:
    value = metrics.get(key, default)
    return value if isinstance(value, (int, float)) and math.isfinite(value) else default


def reconstruction_diagnostics(fixture_id: str, category: str, metrics: dict,
                               metadata: dict) -> dict:
    source_residual = float_metric(metrics, "source_residual_highpass_ratio", -1.0)
    rendered_residual = float_metric(metrics, "residual_highpass_ratio", -1.0)
    residual_delta = (abs(rendered_residual - source_residual)
                      if source_residual >= 0.0 and rendered_residual >= 0.0 else -1.0)
    source_attack = float_metric(metrics, "source_attack_sharpness", -1.0)
    rendered_attack = float_metric(metrics, "attack_sharpness", -1.0)
    transient_ratio = (rendered_attack / max(source_attack, 1e-12)
                       if source_attack >= 0.0 and rendered_attack >= 0.0 else -1.0)
    model = metadata.get("model", {})
    return {
        "fixture_id": fixture_id,
        "category": category,
        "normalized_rms_error": float_metric(metrics, "normalized_rms_error"),
        "spectral_distance_db": float_metric(metrics, "spectral_distance_db"),
        "attack_normalized_rms_error": float_metric(metrics, "attack_normalized_rms_error"),
        "tail_normalized_rms_error": float_metric(metrics, "tail_normalized_rms_error"),
        "dominant_time_error_region": metrics.get("dominant_time_error_region", "unknown"),
        "spectral_distance_low_db": float_metric(metrics, "spectral_distance_low_db"),
        "spectral_distance_mid_db": float_metric(metrics, "spectral_distance_mid_db"),
        "spectral_distance_high_db": float_metric(metrics, "spectral_distance_high_db"),
        "dominant_spectral_error_band": metrics.get("dominant_spectral_error_band", "unknown"),
        "decay_envelope_error": float_metric(metrics, "decay_envelope_error"),
        "decay_energy_ratio": float_metric(metrics, "decay_energy_ratio", 1.0),
        "decay_envelope_correlation": float_metric(metrics, "decay_envelope_correlation", 1.0),
        "decay_level_bias": float_metric(metrics, "decay_level_bias"),
        "decay_envelope_error_early": float_metric(metrics, "decay_envelope_error_early"),
        "decay_envelope_error_middle": float_metric(metrics, "decay_envelope_error_middle"),
        "decay_envelope_error_late": float_metric(metrics, "decay_envelope_error_late"),
        "dominant_decay_error_region": metrics.get("dominant_decay_error_region", "unknown"),
        "residual_highpass_delta": residual_delta,
        "transient_sharpness_ratio": transient_ratio,
        "orphan_residual_ratio": float_metric(metadata, "orphan_residual_ratio", -1.0),
        "partial_noise_energy": float_metric(metadata, "partial_noise_energy", -1.0),
        "transient_markers": model.get("transients", -1),
        "transient_buffers": model.get("transient_buffers", -1),
    }


def print_reconstruction_diagnostics(diagnostics: dict) -> None:
    time_region = diagnostics["dominant_time_error_region"]
    time_value = diagnostics.get(f"{time_region}_normalized_rms_error", 0.0)
    band = diagnostics["dominant_spectral_error_band"]
    band_value = diagnostics.get(f"spectral_distance_{band}_db", 0.0)
    print("  diagnostics:"
          f" time attack_nrms={diagnostics['attack_normalized_rms_error']:.6g}"
          f" tail_nrms={diagnostics['tail_normalized_rms_error']:.6g}"
          f" dominant={time_region}:{time_value:.6g};"
          f" spectral low={diagnostics['spectral_distance_low_db']:.6g}dB"
          f" mid={diagnostics['spectral_distance_mid_db']:.6g}dB"
          f" high={diagnostics['spectral_distance_high_db']:.6g}dB"
          f" dominant_band={band}:{band_value:.6g}dB;")
    print("  layer proxies:"
          f" residual_highpass_delta={diagnostics['residual_highpass_delta']:.6g}"
          f" transient_sharpness_ratio={diagnostics['transient_sharpness_ratio']:.6g}"
          f" orphan_residual_ratio={diagnostics['orphan_residual_ratio']:.6g}"
          f" partial_noise_energy={diagnostics['partial_noise_energy']:.6g}"
          f" transients={diagnostics['transient_markers']}"
          f" buffers={diagnostics['transient_buffers']}")
    decay_region = diagnostics["dominant_decay_error_region"]
    decay_value = diagnostics.get(f"decay_envelope_error_{decay_region}", 0.0)
    print("  decay envelope:"
          f" error={diagnostics['decay_envelope_error']:.6g}"
          f" energy_ratio={diagnostics['decay_energy_ratio']:.6g}"
          f" correlation={diagnostics['decay_envelope_correlation']:.6g}"
          f" level_bias={diagnostics['decay_level_bias']:.6g}"
          f" early={diagnostics['decay_envelope_error_early']:.6g}"
          f" middle={diagnostics['decay_envelope_error_middle']:.6g}"
          f" late={diagnostics['decay_envelope_error_late']:.6g}"
          f" dominant_decay={decay_region}:{decay_value:.6g}")


def print_diagnostic_ranking(rows: list[dict]) -> None:
    if not rows:
        return
    print("\nDiagnostic ranking by source normalized RMS error:")
    for row in sorted(rows, key=lambda item: item["normalized_rms_error"], reverse=True):
        time_region = row["dominant_time_error_region"]
        time_value = row.get(f"{time_region}_normalized_rms_error", 0.0)
        band = row["dominant_spectral_error_band"]
        band_value = row.get(f"spectral_distance_{band}_db", 0.0)
        print(f"  {row['fixture_id']}:"
              f" nrms={row['normalized_rms_error']:.6g}"
              f" spectral={row['spectral_distance_db']:.6g}dB"
              f" time_focus={time_region}:{time_value:.6g}"
              f" spectral_focus={band}:{band_value:.6g}dB"
              f" decay_error={row['decay_envelope_error']:.6g}"
              f" decay_ratio={row['decay_energy_ratio']:.6g}"
              f" decay_focus={row['dominant_decay_error_region']}"
              f" residual_delta={row['residual_highpass_delta']:.6g}"
              f" transient_ratio={row['transient_sharpness_ratio']:.6g}")


# ---------------------------------------------------------------------------
# Per-fixture runner
# ---------------------------------------------------------------------------

def run_fixture(fixture: dict, manifest_dir: Path, reference_dir: Path,
                candidate_offline: str):
    """Run one fixture. Returns (any_failures, checks_run, checks_skipped, diagnostics)."""
    fixture_id = fixture["id"]
    audio_rel = fixture.get("input_audio", "")
    audio_path = manifest_dir / audio_rel
    summary_rel = fixture.get("atsa_reference", {}).get("summary_metrics", "")
    summary_path = manifest_dir / summary_rel if summary_rel else None

    print(f"\nFixture: {fixture_id} ({fixture.get('category', '?')})")

    if not audio_path.is_file():
        print(f"  SKIP — audio asset not found: {audio_path}")
        print(f"         Run generate_atsa_references.py once the audio file is available.")
        return False, 0, 1, None

    if summary_path is None or not summary_path.is_file():
        print(f"  SKIP — reference metrics not found: {summary_path}")
        print(f"         Run: python3 tests/generate_atsa_references.py --ats-offline <bin> "
              f"--manifest ... --output-dir tests/fixtures/reference")
        return False, 0, 1, None

    ref_metrics = json.loads(summary_path.read_text(encoding="utf-8"))

    # Run the modern candidate offline tool on the fixture audio to produce our metrics.
    candidate_analysis_mode = fixture.get("candidate_analysis_mode", "modern_ats")
    with tempfile.TemporaryDirectory() as tmpdir:
        out_wav = os.path.join(tmpdir, "out.wav")
        out_meta = os.path.join(tmpdir, "meta.json")
        cmd = [
            candidate_offline,
            "--analysis-mode", candidate_analysis_mode,
            "--metadata", out_meta,
            str(audio_path),
            out_wav,
        ]
        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except subprocess.CalledProcessError as exc:
            print(f"  ERROR — candidate offline analysis failed: {exc.stderr.decode(errors='replace')[:200]}",
                  file=sys.stderr)
            return True, 0, 0, None

        our_metadata = json.loads(Path(out_meta).read_text(encoding="utf-8"))
        recon = reconstruction_metrics(str(audio_path), out_wav)

    our_metrics: dict = dict(recon)
    our_metrics.update({
        "continuity_score": our_metadata.get("continuity_score", -1.0),
        "identity_errors": our_metadata.get("identity_errors", -1),
        "orphan_residual_ratio": our_metadata.get("orphan_residual_ratio", -1.0),
        "residual_highpass_ratio": recon.get(
            "residual_highpass_ratio",
            our_metadata.get("residual_highpass_ratio", -1.0),
        ),
        "source_residual_highpass_ratio": recon.get("source_residual_highpass_ratio", -1.0),
        "attack_sharpness": recon.get("attack_sharpness", our_metadata.get("attack_sharpness", -1.0)),
        "source_attack_sharpness": recon.get("source_attack_sharpness", -1.0),
        "max_buffer_step": recon.get("max_buffer_step", our_metadata.get("max_buffer_step", -1.0)),
    })
    diagnostics = reconstruction_diagnostics(
        fixture_id, fixture.get("category", "?"), our_metrics, our_metadata
    )

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
    print_reconstruction_diagnostics(diagnostics)

    if not all_checks:
        print(f"  FAIL — no gate checks produced (gates={enabled_gates})", file=sys.stderr)
        any_fail = True

    return any_fail, len(all_checks), 0, diagnostics


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: run_external_acceptance.py <modern-candidate-offline> <manifest.json> <reference-dir>",
            file=sys.stderr,
        )
        return 2

    candidate_offline = sys.argv[1]
    manifest_path = Path(sys.argv[2]).resolve()
    reference_dir = Path(sys.argv[3]).resolve()

    if not os.path.isfile(candidate_offline):
        print(f"error: candidate offline tool not found: {candidate_offline}", file=sys.stderr)
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
    diagnostic_rows: list[dict] = []

    for fixture in fixtures:
        any_fail, n_checks, n_skip, diagnostics = run_fixture(
            fixture, manifest_dir, reference_dir, candidate_offline
        )
        total_checks += n_checks
        total_skipped += n_skip
        if diagnostics is not None:
            diagnostic_rows.append(diagnostics)
        if any_fail:
            total_failed += 1

    print_diagnostic_ranking(diagnostic_rows)

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

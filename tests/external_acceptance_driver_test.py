#!/usr/bin/env python3
"""Contract tests for the external real-audio acceptance driver."""

from __future__ import annotations

import json
import importlib.util
import stat
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


def write_fixture_wav(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "w") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        handle.writeframes(b"\x00\x00" * 32)


def write_transient_fixture_wav(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    samples = [0] * 4096
    samples[4] = 24000
    for i in range(5, 4096):
        samples[i] = int(3000 * (0.997 ** (i - 5)))
    with wave.open(str(path), "w") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        handle.writeframes(b"".join(int(s).to_bytes(2, "little", signed=True) for s in samples))


def write_fake_ats_offline(path: Path) -> None:
    path.write_text(
        """#!/usr/bin/env python3
import json
import shutil
import sys

metadata_path = sys.argv[sys.argv.index("--metadata") + 1]
analysis_mode = sys.argv[sys.argv.index("--analysis-mode") + 1]
if analysis_mode != "modern_ats":
    print(f"unexpected analysis mode: {analysis_mode}", file=sys.stderr)
    raise SystemExit(7)
input_path = sys.argv[-2]
output_path = sys.argv[-1]
with open(metadata_path, "w", encoding="utf-8") as handle:
    json.dump({}, handle)
shutil.copyfile(input_path, output_path)
""",
        encoding="utf-8",
    )
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def run_driver(driver: Path, tmpdir: Path, manifest: dict) -> subprocess.CompletedProcess[str]:
    fake_ats = tmpdir / "ats_offline"
    write_fake_ats_offline(fake_ats)
    manifest_path = tmpdir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    return subprocess.run(
        [sys.executable, str(driver), str(fake_ats), str(manifest_path), str(tmpdir / "reference")],
        check=False,
        capture_output=True,
        text=True,
    )


def load_driver_module(driver: Path):
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("run_external_acceptance_under_test", driver)
    require(spec is not None and spec.loader is not None, "driver module should be importable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_missing_assets_fail(driver: Path) -> None:
    with tempfile.TemporaryDirectory() as raw_tmpdir:
        tmpdir = Path(raw_tmpdir)
        manifest = {
            "schema_version": 1,
            "fixtures": [
                {
                    "id": "missing_audio",
                    "category": "sustained",
                    "input_audio": "audio/missing.wav",
                    "enabled_gates": ["classic_parity"],
                    "atsa_reference": {"summary_metrics": "reference/missing_summary.json"},
                }
            ],
        }
        result = run_driver(driver, tmpdir, manifest)
        combined = result.stdout + result.stderr
        require(result.returncode == 1, "missing external assets should fail the enabled gate")
        require("EXTERNAL ACCEPTANCE INCOMPLETE" in combined, "missing assets failure should be explicit")


def test_zero_checks_fail(driver: Path) -> None:
    with tempfile.TemporaryDirectory() as raw_tmpdir:
        tmpdir = Path(raw_tmpdir)
        write_fixture_wav(tmpdir / "audio" / "tone.wav")
        reference_path = tmpdir / "reference" / "tone_summary.json"
        reference_path.parent.mkdir(parents=True, exist_ok=True)
        reference_path.write_text(json.dumps({"fixture_id": "tone"}), encoding="utf-8")
        manifest = {
            "schema_version": 1,
            "fixtures": [
                {
                    "id": "tone",
                    "category": "sustained",
                    "input_audio": "audio/tone.wav",
                    "enabled_gates": ["tracker_continuity"],
                    "atsa_reference": {"summary_metrics": "reference/tone_summary.json"},
                }
            ],
        }
        result = run_driver(driver, tmpdir, manifest)
        combined = result.stdout + result.stderr
        require(result.returncode == 1, "fixtures that emit zero checks should fail")
        require("no gate checks produced" in combined, "zero-check failure should name the problem")


def test_shape_gates_use_source_audio(driver: Path) -> None:
    with tempfile.TemporaryDirectory() as raw_tmpdir:
        tmpdir = Path(raw_tmpdir)
        write_transient_fixture_wav(tmpdir / "audio" / "pluck.wav")
        reference_path = tmpdir / "reference" / "pluck_summary.json"
        reference_path.parent.mkdir(parents=True, exist_ok=True)
        reference_path.write_text(json.dumps({
            "fixture_id": "pluck",
            "residual_highpass_ratio": 1.0,
            "attack_sharpness": 100.0,
        }), encoding="utf-8")
        manifest = {
            "schema_version": 1,
            "fixtures": [
                {
                    "id": "pluck",
                    "category": "plucked",
                    "input_audio": "audio/pluck.wav",
                    "enabled_gates": [
                        "residual_energy_preservation",
                        "transient_preservation",
                    ],
                    "threshold_overrides": {
                        "max_residual_highpass_ratio_delta": 0.001,
                        "min_transient_sharpness_ratio": 0.99,
                    },
                    "atsa_reference": {"summary_metrics": "reference/pluck_summary.json"},
                }
            ],
        }
        result = run_driver(driver, tmpdir, manifest)
        combined = result.stdout + result.stderr
        require(result.returncode == 0,
                "copying source audio should pass shape gates even when ATSA summaries differ")
        require("residual_energy_preservation.residual_highpass_ratio" in combined,
                "source-referenced residual gate should emit a check")
        require("transient_preservation.attack_sharpness" in combined,
                "source-referenced transient gate should emit a check")
        require("diagnostics: time attack_nrms=" in combined,
                "real-audio driver should print time-window reconstruction diagnostics")
        require("dominant_band=" in combined,
                "real-audio driver should print dominant spectral-band diagnostics")
        require("layer proxies:" in combined,
                "real-audio driver should print residual/transient layer proxy diagnostics")
        require("decay envelope: error=" in combined,
                "real-audio driver should print decay-envelope reconstruction diagnostics")
        require("decay_focus=" in combined,
                "real-audio driver should print ranked decay-envelope focus diagnostics")
        require("waveform alignment:" in combined,
                "real-audio driver should print waveform timing/phase diagnostics")
        require("align_gain=" in combined,
                "real-audio driver should print ranked waveform alignment diagnostics")
        require("Diagnostic ranking by source normalized RMS error:" in combined,
                "real-audio driver should print a ranked fixture diagnostic summary")


def test_classic_parity_source_ceilings(driver: Path) -> None:
    module = load_driver_module(driver)
    checks = module.evaluate_classic_parity(
        "loose_atsa_reference",
        {
            "normalized_rms_error": 0.70,
            "spectral_distance_db": 3.8,
        },
        {
            "normalized_rms_error": 1.0,
            "spectral_distance_db": 10.0,
        },
        module.merged_thresholds({}),
    )
    by_id = {check.check_id: check for check in checks}
    require(by_id["loose_atsa_reference.classic_parity.normalized_rms_error"].passed,
            "ATSA-relative normalized RMS check should pass for a source-improving candidate")
    require(not by_id["loose_atsa_reference.classic_parity.source_normalized_rms_error"].passed,
            "source normalized RMS ceiling should fail when reconstruction is still too loose")
    require(by_id["loose_atsa_reference.classic_parity.spectral_distance_db"].passed,
            "ATSA-relative spectral check should pass for a source-improving candidate")
    require(not by_id["loose_atsa_reference.classic_parity.source_spectral_distance_db"].passed,
            "source spectral ceiling should fail when reconstruction is still too loose")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: external_acceptance_driver_test.py <run_external_acceptance.py>", file=sys.stderr)
        return 2

    driver = Path(sys.argv[1]).resolve()
    require(driver.is_file(), f"driver not found: {driver}")
    test_missing_assets_fail(driver)
    test_zero_checks_fail(driver)
    test_shape_gates_use_source_audio(driver)
    test_classic_parity_source_ceilings(driver)
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

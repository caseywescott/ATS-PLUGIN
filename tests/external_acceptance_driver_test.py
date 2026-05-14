#!/usr/bin/env python3
"""Contract tests for the external real-audio acceptance driver."""

from __future__ import annotations

import json
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


def test_transient_gate_uses_source_attack(driver: Path) -> None:
    with tempfile.TemporaryDirectory() as raw_tmpdir:
        tmpdir = Path(raw_tmpdir)
        write_transient_fixture_wav(tmpdir / "audio" / "pluck.wav")
        reference_path = tmpdir / "reference" / "pluck_summary.json"
        reference_path.parent.mkdir(parents=True, exist_ok=True)
        reference_path.write_text(json.dumps({
            "fixture_id": "pluck",
            "attack_sharpness": 100.0,
        }), encoding="utf-8")
        manifest = {
            "schema_version": 1,
            "fixtures": [
                {
                    "id": "pluck",
                    "category": "plucked",
                    "input_audio": "audio/pluck.wav",
                    "enabled_gates": ["transient_preservation"],
                    "threshold_overrides": {"min_transient_sharpness_ratio": 0.99},
                    "atsa_reference": {"summary_metrics": "reference/pluck_summary.json"},
                }
            ],
        }
        result = run_driver(driver, tmpdir, manifest)
        combined = result.stdout + result.stderr
        require(result.returncode == 0,
                "copying source audio should pass transient preservation even if ATSA is over-sharp")
        require("transient_preservation.attack_sharpness" in combined,
                "source-referenced transient gate should emit a check")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: external_acceptance_driver_test.py <run_external_acceptance.py>", file=sys.stderr)
        return 2

    driver = Path(sys.argv[1]).resolve()
    require(driver.is_file(), f"driver not found: {driver}")
    test_missing_assets_fail(driver)
    test_zero_checks_fail(driver)
    test_transient_gate_uses_source_attack(driver)
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

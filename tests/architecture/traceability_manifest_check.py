#!/usr/bin/env python3
"""Validate the ATS JUCE-removal architecture/DSP traceability manifest.

This intentionally has no third-party dependencies so it can run before the
new engine targets exist. It validates the ledger's structure and the claims
that are meaningful during the migration; later CI can add source-symbol and
evidence-report checks without changing the manifest format.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


REQUIRED_RECORD_FIELDS = {
    "trace_id",
    "family",
    "legacy_surface",
    "legacy_owner",
    "replacement_owner",
    "replacement_api",
    "algorithm_or_dsp",
    "data_flow",
    "thread_domain",
    "invariants",
    "state_format_impact",
    "source_refs",
    "replacement_refs",
    "tests",
    "fixtures_or_goldens",
    "evidence",
    "status",
    "deviation_id",
    "blocked_by",
    "last_reviewed",
}


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def path_from_test_entry(entry: dict[str, object]) -> Path | None:
    raw_path = entry.get("path")
    if not isinstance(raw_path, str):
        return None
    if raw_path.startswith("required: "):
        return None
    return Path(raw_path)


def validate(manifest_path: Path) -> list[str]:
    errors: list[str] = []
    try:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"cannot read manifest {manifest_path}: {exc}"]

    if document.get("schema_version") != 1:
        fail(errors, "schema_version must be 1")
    if document.get("manifest_kind") != "ats_juce_replacement_traceability":
        fail(errors, "manifest_kind is incorrect")
    if not isinstance(document.get("records"), list) or not document["records"]:
        fail(errors, "records must be a non-empty array")
        return errors

    allowed_statuses = set(document.get("allowed_statuses", []))
    seen_ids: set[str] = set()
    repo_root = manifest_path.resolve().parents[2]
    baseline_commit = document.get("baseline_commit")
    backend_revision = document.get("target_backend_revision")
    has_promoted_record = any(
        isinstance(record, dict)
        and record.get("status") in {"implemented", "verified", "deviated", "retired"}
        for record in document["records"]
    )
    if has_promoted_record and baseline_commit in {"TO_BE_FROZEN", "", None}:
        fail(errors, "promoted records require a frozen baseline_commit")
    if has_promoted_record and backend_revision in {"TO_BE_PINNED", "", None}:
        fail(errors, "promoted records require a pinned target_backend_revision")

    for index, record in enumerate(document["records"]):
        label = f"record {index}"
        if not isinstance(record, dict):
            fail(errors, f"{label} must be an object")
            continue

        missing = REQUIRED_RECORD_FIELDS - record.keys()
        if missing:
            fail(errors, f"{label} missing fields: {', '.join(sorted(missing))}")

        trace_id = record.get("trace_id")
        if not isinstance(trace_id, str):
            fail(errors, f"{label} trace_id must be a string")
            continue
        if trace_id in seen_ids:
            fail(errors, f"duplicate trace_id: {trace_id}")
        seen_ids.add(trace_id)
        if not (trace_id.startswith("ARCH-") or trace_id.startswith("DSP-")):
            fail(errors, f"{trace_id}: architecture manifest IDs must start with ARCH- or DSP-")

        status = record.get("status")
        if status not in allowed_statuses:
            fail(errors, f"{trace_id}: invalid status {status!r}")
        if status in {"implemented", "verified", "deviated", "retired"}:
            replacement_refs = record.get("replacement_refs")
            if not isinstance(replacement_refs, list) or not replacement_refs:
                fail(errors, f"{trace_id}: implemented-or-later rows need replacement_refs")
        if status in {"verified", "deviated", "retired"}:
            evidence = record.get("evidence")
            if not isinstance(evidence, list) or not evidence:
                fail(errors, f"{trace_id}: {status} rows need evidence")
            if record.get("last_reviewed") == "TO_BE_REVIEWED":
                fail(errors, f"{trace_id}: {status} rows need a reviewed reference")
        if status == "deviated" and not record.get("deviation_id"):
            fail(errors, f"{trace_id}: deviated rows need deviation_id")
        if status == "blocked" and not record.get("blocked_by"):
            fail(errors, f"{trace_id}: blocked rows need blocked_by")
        if status != "blocked" and record.get("blocked_by") != []:
            fail(errors, f"{trace_id}: non-blocked rows must have an empty blocked_by array")

        for field in ("invariants", "source_refs", "replacement_refs", "tests", "fixtures_or_goldens", "evidence"):
            value = record.get(field)
            if not isinstance(value, list):
                fail(errors, f"{trace_id}: {field} must be an array")
            elif field in {"invariants", "source_refs", "replacement_refs", "tests"} and not value:
                fail(errors, f"{trace_id}: {field} must not be empty")

        tests = record.get("tests", [])
        if isinstance(tests, list):
            for test_index, test in enumerate(tests):
                if not isinstance(test, dict):
                    fail(errors, f"{trace_id}: test {test_index} must be an object")
                    continue
                if test.get("state") not in {"existing", "required"}:
                    fail(errors, f"{trace_id}: test {test_index} state must be existing or required")
                if not isinstance(test.get("proves"), str) or not test["proves"].strip():
                    fail(errors, f"{trace_id}: test {test_index} needs a proves description")
                if test.get("state") == "existing":
                    test_path = path_from_test_entry(test)
                    if test_path is None:
                        fail(errors, f"{trace_id}: existing test path must be a repository path")
                    elif not (repo_root / test_path).exists():
                        fail(errors, f"{trace_id}: existing test does not exist: {test_path}")

    ui_manifest = document.get("ui_manifest")
    if not isinstance(ui_manifest, str) or not (repo_root / ui_manifest).exists():
        fail(errors, f"ui_manifest does not exist: {ui_manifest!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "manifest",
        nargs="?",
        default="tests/architecture/ats_juce_replacement_traceability.json",
    )
    args = parser.parse_args()
    errors = validate(Path(args.manifest))
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"OK: traceability manifest is valid ({len(json.loads(Path(args.manifest).read_text(encoding='utf-8'))['records'])} records)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

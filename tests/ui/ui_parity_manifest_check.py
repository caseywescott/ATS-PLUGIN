#!/usr/bin/env python3
"""Validate the ATS UI parity inventory and its link to the specification.

This is intentionally small and dependency-free. It catches the specific
failure mode where a surface is described in the spec but never receives a
record, or where a documented-only surface is accidentally presented as a
current-source parity claim.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


RECORD_FIELDS = {
    "surface_id",
    "source_files",
    "event_owner",
    "evidence_class",
    "test_ids",
    "status",
    "deviation",
}
ALLOWED_PREFIXES = ("UI-", "SP-")
ALLOWED_STATUSES = {"planned", "mapped", "implemented", "verified", "deviated", "blocked"}
ALLOWED_SCOPES = {"source_current", "documented_only", "promoted_target", "design_reference"}


def spec_surface_ids(spec_path: Path) -> set[str]:
    ids: set[str] = set()
    for line in spec_path.read_text(encoding="utf-8").splitlines():
        if "|" not in line:
            continue
        match = re.search(r"\|\s*((?:UI|SP)-[A-Z0-9-]+)\s*\|", line)
        if match:
            ids.add(match.group(1))
    return ids


def validate(manifest_path: Path) -> list[str]:
    errors: list[str] = []
    try:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"cannot read manifest {manifest_path}: {exc}"]

    if document.get("schema_version") != 2:
        errors.append("schema_version must be 2")
    spec_raw = document.get("specification")
    spec_path = manifest_path.parents[2] / spec_raw if isinstance(spec_raw, str) else None
    if spec_path is None or not spec_path.exists():
        errors.append(f"specification does not exist: {spec_raw!r}")
    records = document.get("records")
    if not isinstance(records, list) or not records:
        errors.append("records must be a non-empty array")
        return errors

    scopes = set(document.get("allowed_baseline_scopes", []))
    if scopes != ALLOWED_SCOPES:
        errors.append("allowed_baseline_scopes must enumerate the four supported scopes")
    contract_directory_raw = document.get("surface_contract_directory")
    contract_directory = (
        manifest_path.parents[2] / contract_directory_raw
        if isinstance(contract_directory_raw, str)
        else None
    )
    if contract_directory is None:
        errors.append("surface_contract_directory must be declared")

    seen: set[str] = set()
    for index, record in enumerate(records):
        label = f"record {index}"
        if not isinstance(record, dict):
            errors.append(f"{label} must be an object")
            continue
        missing = RECORD_FIELDS - record.keys()
        if missing:
            errors.append(f"{label} missing fields: {', '.join(sorted(missing))}")
        surface_id = record.get("surface_id")
        if not isinstance(surface_id, str):
            errors.append(f"{label} surface_id must be a string")
            continue
        if surface_id in seen:
            errors.append(f"duplicate surface_id: {surface_id}")
        seen.add(surface_id)
        if not surface_id.startswith(ALLOWED_PREFIXES):
            errors.append(f"{surface_id}: surface IDs must start with UI- or SP-")
        if record.get("status") not in ALLOWED_STATUSES:
            errors.append(f"{surface_id}: invalid status {record.get('status')!r}")
        for field in ("source_files", "test_ids"):
            value = record.get(field)
            if not isinstance(value, list) or not value or not all(isinstance(item, str) and item for item in value):
                errors.append(f"{surface_id}: {field} must be a non-empty string array")
        for field in ("event_owner", "evidence_class"):
            if not isinstance(record.get(field), str) or not record[field].strip():
                errors.append(f"{surface_id}: {field} must be non-empty text")
        scope = record.get("baseline_scope", "source_current")
        if scope not in ALLOWED_SCOPES:
            errors.append(f"{surface_id}: invalid baseline_scope {scope!r}")
        if scope in {"documented_only", "promoted_target"} and not record.get("deviation"):
            errors.append(f"{surface_id}: {scope} surfaces require a deviation/decision ID")
        if record.get("status") == "deviated" and not record.get("deviation"):
            errors.append(f"{surface_id}: deviated status requires deviation")
        if (
            contract_directory is not None
            and record.get("status") in {"mapped", "implemented", "verified", "deviated"}
            and not (contract_directory / f"{surface_id}.md").exists()
        ):
            errors.append(
                f"{surface_id}: status {record.get('status')} requires "
                f"{contract_directory_raw}/{surface_id}.md"
            )

    if spec_path is not None and spec_path.exists():
        required = spec_surface_ids(spec_path)
        missing = sorted(required - seen)
        if missing:
            errors.append("spec surfaces missing from manifest: " + ", ".join(missing))

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", nargs="?", default="tests/ui/ats_ui_parity_manifest.json")
    args = parser.parse_args()
    manifest_path = Path(args.manifest)
    errors = validate(manifest_path)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    records = json.loads(manifest_path.read_text(encoding="utf-8"))["records"]
    print(f"OK: UI parity manifest is valid ({len(records)} records)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

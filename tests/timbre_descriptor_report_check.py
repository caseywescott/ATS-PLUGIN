#!/usr/bin/env python3
"""Validate timbre descriptor regression reports.

A descriptor report is the JSON export form of TimbreDescriptorFrame and
TimbreDescriptorSeries values produced by the C++ core. This checker enforces:

* every report carries the schema fields required by
  ``tests/fixtures/timbre_descriptor_report_schema.json``,
* every descriptor id appearing in a report is registered in
  ``tests/fixtures/timbre_descriptor_manifest.json``,
* every descriptor declares status, confidence in [0, 1], formula_version,
  domain, and (for ``available`` status) a finite numeric value,
* formula_version matches the manifest's recorded version, so a silent formula
  change in the C++ core trips the checker,
* the report-level weighting/comparison axis values come from the schema's
  allowed sets.

The checker is intended to run two ways:

1. As a CLI tool: ``timbre_descriptor_report_check.py <report.json>``. Each
   positional argument is a path to a report (or a directory of reports).
2. As a unit-test self-test: with no arguments it constructs a synthetic
   well-formed report and several intentionally-broken reports and exits
   non-zero if any pass/fail check is wrong.

The self-test path is what gets wired into CTest for Commit 4.
"""

from __future__ import annotations

import json
import math
import pathlib
import sys
from typing import Any


HERE = pathlib.Path(__file__).resolve().parent
SCHEMA_PATH = HERE / "fixtures" / "timbre_descriptor_report_schema.json"
MANIFEST_PATH = HERE / "fixtures" / "timbre_descriptor_manifest.json"


def _load_json(path: pathlib.Path) -> dict:
    if not path.is_file():
        raise SystemExit(f"required JSON not found: {path}")
    with path.open("r", encoding="utf-8") as handle:
        try:
            return json.load(handle)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"JSON parse error for {path}: {exc}") from exc


def _is_finite_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(float(value))


def _check_descriptor_value(
    descriptor_id: str,
    value_entry: dict,
    schema: dict,
    manifest_index: dict[str, dict],
    failures: list[str],
    where: str,
) -> None:
    if descriptor_id not in manifest_index:
        failures.append(
            f"{where}: descriptor '{descriptor_id}' is not registered in the "
            f"manifest"
        )
        return

    manifest_entry = manifest_index[descriptor_id]
    dv_schema = schema.get("descriptor_value") or {}
    required = dv_schema.get("required") or []
    for required_field in required:
        if required_field not in value_entry:
            failures.append(
                f"{where}: descriptor '{descriptor_id}' missing required "
                f"field '{required_field}'"
            )

    status = value_entry.get("status")
    statuses = set(dv_schema.get("status_allowed_values") or ())
    if status not in statuses:
        failures.append(
            f"{where}: descriptor '{descriptor_id}' has unknown status "
            f"'{status}'"
        )

    domain = value_entry.get("domain")
    domains = set(dv_schema.get("domain_allowed_values") or ())
    if domain not in domains:
        failures.append(
            f"{where}: descriptor '{descriptor_id}' has unknown domain "
            f"'{domain}'"
        )
    elif domain != manifest_entry.get("domain"):
        failures.append(
            f"{where}: descriptor '{descriptor_id}' reports domain "
            f"'{domain}' but the manifest says '{manifest_entry.get('domain')}'"
        )

    confidence = value_entry.get("confidence")
    confidence_range = dv_schema.get("value_rules", {}).get("confidence_range")
    if confidence_range is not None:
        lo, hi = confidence_range
        if not _is_finite_number(confidence):
            failures.append(
                f"{where}: descriptor '{descriptor_id}' has non-finite "
                f"confidence"
            )
        elif not (lo <= float(confidence) <= hi):
            failures.append(
                f"{where}: descriptor '{descriptor_id}' confidence "
                f"{confidence} outside [{lo}, {hi}]"
            )

    formula_version = value_entry.get("formula_version")
    if not isinstance(formula_version, int) or formula_version < 1:
        failures.append(
            f"{where}: descriptor '{descriptor_id}' formula_version must be a "
            f"positive integer"
        )
    elif formula_version != manifest_entry.get("formula_version"):
        failures.append(
            f"{where}: descriptor '{descriptor_id}' formula_version "
            f"{formula_version} disagrees with manifest "
            f"{manifest_entry.get('formula_version')} (formula change must be "
            f"recorded explicitly)"
        )

    value = value_entry.get("value")
    if status == "available":
        if value is None:
            failures.append(
                f"{where}: descriptor '{descriptor_id}' has status 'available'"
                f" but value is null"
            )
        elif descriptor_id == "resonance_peaks":
            # resonance_peaks is a list of objects (frequency_hz,
            # axis_position, prominence_db, bandwidth_axis), not a flat
            # numeric vector.
            if not isinstance(value, list):
                failures.append(
                    f"{where}: descriptor '{descriptor_id}' must be a list"
                )
            else:
                for index, peak in enumerate(value):
                    if not isinstance(peak, dict):
                        failures.append(
                            f"{where}: descriptor '{descriptor_id}' peak "
                            f"{index} must be an object"
                        )
                        continue
                    for required_field in (
                        "frequency_hz",
                        "axis_position",
                        "prominence_db",
                        "bandwidth_axis",
                    ):
                        if not _is_finite_number(peak.get(required_field)):
                            failures.append(
                                f"{where}: descriptor '{descriptor_id}' peak "
                                f"{index} field '{required_field}' is non-finite"
                            )
        elif isinstance(value, list):
            for index, sub in enumerate(value):
                if not _is_finite_number(sub):
                    failures.append(
                        f"{where}: descriptor '{descriptor_id}' has non-finite "
                        f"vector element {index}"
                    )
        else:
            if not _is_finite_number(value):
                failures.append(
                    f"{where}: descriptor '{descriptor_id}' has non-finite "
                    f"value {value!r}"
                )
    else:
        if value is not None and isinstance(value, float) and math.isnan(value):
            failures.append(
                f"{where}: descriptor '{descriptor_id}' has NaN value under "
                f"non-available status"
            )


def _check_frame(
    frame_entry: dict,
    schema: dict,
    manifest_index: dict[str, dict],
    failures: list[str],
    where_root: str,
) -> None:
    frame_schema = schema.get("frame") or {}
    required = frame_schema.get("required") or []
    for required_field in required:
        if required_field not in frame_entry:
            failures.append(
                f"{where_root}: frame missing required field "
                f"'{required_field}'"
            )

    frame_index = frame_entry.get("frame_index", "?")
    where = f"{where_root}.frame[{frame_index}]"

    descriptors = frame_entry.get("descriptors")
    if not isinstance(descriptors, dict) or not descriptors:
        failures.append(f"{where}: descriptors must be a non-empty object")
        return

    for descriptor_id, value_entry in descriptors.items():
        if not isinstance(value_entry, dict):
            failures.append(
                f"{where}: descriptor '{descriptor_id}' entry must be an object"
            )
            continue
        _check_descriptor_value(
            descriptor_id,
            value_entry,
            schema,
            manifest_index,
            failures,
            where,
        )


def check_report(
    report: dict,
    schema: dict,
    manifest_index: dict[str, dict],
    report_label: str,
) -> list[str]:
    failures: list[str] = []

    report_schema = schema.get("report") or {}
    required_top = report_schema.get("required_top_level") or []
    for required_field in required_top:
        if required_field not in report:
            failures.append(
                f"{report_label}: report missing required top-level field "
                f"'{required_field}'"
            )

    if report.get("schema_version") != 1:
        failures.append(
            f"{report_label}: schema_version must equal 1 "
            f"(report uses {report.get('schema_version')!r})"
        )

    weighting_values = set(report_schema.get("weighting_allowed_values") or ())
    if report.get("weighting") not in weighting_values:
        failures.append(
            f"{report_label}: weighting '{report.get('weighting')}' is not in "
            f"allowed values"
        )

    axis_values = set(report_schema.get("comparison_axis_allowed_values") or ())
    if report.get("comparison_axis") not in axis_values:
        failures.append(
            f"{report_label}: comparison_axis "
            f"'{report.get('comparison_axis')}' is not in allowed values"
        )

    grid_size = report.get("comparison_grid_size")
    if not isinstance(grid_size, int) or grid_size < 2:
        failures.append(
            f"{report_label}: comparison_grid_size must be an integer >= 2"
        )

    frames = report.get("frames")
    if not isinstance(frames, list):
        failures.append(f"{report_label}: frames must be a list")
        frames = []

    for frame_entry in frames:
        if not isinstance(frame_entry, dict):
            failures.append(f"{report_label}: frame entries must be objects")
            continue
        _check_frame(
            frame_entry,
            schema,
            manifest_index,
            failures,
            report_label,
        )

    return failures


def _manifest_index(manifest: dict) -> dict[str, dict]:
    return {
        entry["id"]: entry for entry in manifest.get("descriptors", [])
    }


def _make_well_formed_report(manifest_index: dict[str, dict]) -> dict:
    centroid_entry = manifest_index["combined_centroid_hz"]
    noisiness_entry = manifest_index["noisiness"]
    return {
        "schema_version": 1,
        "source_model_id": "synthetic-self-test",
        "weighting": "energy",
        "comparison_axis": "bark",
        "comparison_grid_size": 64,
        "frames": [
            {
                "frame_index": 0,
                "descriptors": {
                    "combined_centroid_hz": {
                        "value": 2412.5,
                        "domain": centroid_entry["domain"],
                        "status": "available",
                        "confidence": 0.95,
                        "formula_version": centroid_entry["formula_version"],
                        "units": centroid_entry["units"],
                    },
                    "noisiness": {
                        "value": 0.18,
                        "domain": noisiness_entry["domain"],
                        "status": "available",
                        "confidence": 1.0,
                        "formula_version": noisiness_entry["formula_version"],
                        "units": noisiness_entry["units"],
                    },
                },
            }
        ],
    }


def _self_test() -> int:
    schema = _load_json(SCHEMA_PATH)
    manifest = _load_json(MANIFEST_PATH)
    manifest_index = _manifest_index(manifest)

    failures = check_report(
        _make_well_formed_report(manifest_index),
        schema,
        manifest_index,
        report_label="self_test_well_formed",
    )
    if failures:
        sys.stderr.write(
            "self-test failure: a well-formed report should not produce "
            "validation errors:\n"
        )
        for failure in failures:
            sys.stderr.write(f"  - {failure}\n")
        return 1

    broken_reports: list[tuple[str, dict, str]] = []

    bad = _make_well_formed_report(manifest_index)
    bad["frames"][0]["descriptors"]["combined_centroid_hz"]["value"] = float("nan")
    broken_reports.append(("non_finite_value", bad, "non-finite value"))

    bad = _make_well_formed_report(manifest_index)
    bad["frames"][0]["descriptors"]["unknown_id"] = {
        "value": 1.0,
        "domain": "combined",
        "status": "available",
        "confidence": 1.0,
        "formula_version": 1,
    }
    broken_reports.append(("unknown_id", bad, "unknown descriptor id"))

    bad = _make_well_formed_report(manifest_index)
    bad["frames"][0]["descriptors"]["combined_centroid_hz"].pop("status")
    broken_reports.append(("missing_status", bad, "missing status"))

    bad = _make_well_formed_report(manifest_index)
    bad["frames"][0]["descriptors"]["noisiness"].pop("confidence")
    broken_reports.append(("missing_confidence", bad, "missing confidence"))

    bad = _make_well_formed_report(manifest_index)
    centroid = bad["frames"][0]["descriptors"]["combined_centroid_hz"]
    centroid["formula_version"] = (
        manifest_index["combined_centroid_hz"]["formula_version"] + 1
    )
    broken_reports.append(("changed_formula_version", bad, "formula version drift"))

    bad = _make_well_formed_report(manifest_index)
    bad["weighting"] = "made_up_weighting"
    broken_reports.append(("bad_weighting", bad, "invalid weighting"))

    bad = _make_well_formed_report(manifest_index)
    bad["comparison_axis"] = "made_up_axis"
    broken_reports.append(("bad_axis", bad, "invalid axis"))

    bad = _make_well_formed_report(manifest_index)
    bad["frames"][0]["descriptors"]["combined_centroid_hz"]["confidence"] = 2.5
    broken_reports.append(("confidence_out_of_range", bad, "confidence > 1"))

    for label, report, description in broken_reports:
        failures = check_report(report, schema, manifest_index, label)
        if not failures:
            sys.stderr.write(
                f"self-test failure: broken report '{label}' "
                f"({description}) was accepted\n"
            )
            return 1

    sys.stdout.write(
        f"timbre descriptor report schema ok: "
        f"{len(broken_reports)} negative cases caught\n"
    )
    return 0


def _check_cli(paths: list[str]) -> int:
    schema = _load_json(SCHEMA_PATH)
    manifest = _load_json(MANIFEST_PATH)
    manifest_index = _manifest_index(manifest)

    failure_count = 0
    for raw in paths:
        path = pathlib.Path(raw)
        candidates: list[pathlib.Path]
        if path.is_dir():
            candidates = sorted(path.rglob("*.json"))
        else:
            candidates = [path]
        for candidate in candidates:
            report = _load_json(candidate)
            failures = check_report(
                report, schema, manifest_index, report_label=str(candidate)
            )
            if failures:
                failure_count += len(failures)
                sys.stderr.write(f"{candidate}: failures:\n")
                for failure in failures:
                    sys.stderr.write(f"  - {failure}\n")
    if failure_count:
        return 1
    sys.stdout.write("all reports valid\n")
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    if args:
        raise SystemExit(_check_cli(args))
    raise SystemExit(_self_test())

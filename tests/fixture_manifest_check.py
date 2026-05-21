#!/usr/bin/env python3
"""Validate the Modern ATSH Phase 0 compatibility fixture manifest."""

from __future__ import annotations

import json
import sys
from pathlib import Path


REQUIRED_CATEGORIES = {
    "sustained",
    "vibrato",
    "close_crossing_partials",
    "plucked_percussive",
    "noisy_voice_flute",
    "dense_polyphony",
    "percussive_hit",
}

REQUIRED_METRICS = {
    "reconstruction_error",
    "spectral_distance",
    "track_continuity",
    "transient_flux",
    "residual_energy_preservation",
}


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fixture_manifest_check.py <ats_compat_manifest.json>", file=sys.stderr)
        return 2

    manifest_path = Path(sys.argv[1])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    if manifest.get("schema_version") != 1:
        return fail("manifest schema_version must be 1")
    if manifest.get("default_analysis_mode") != "classic_ats":
        return fail("Phase 0 default_analysis_mode must be classic_ats")
    if manifest.get("default_tracker_mode") != "classic_recursive_frequency_smr":
        return fail("Phase 0 default_tracker_mode must be classic_recursive_frequency_smr")

    expected_metrics = set(manifest.get("expected_metrics", {}).keys())
    missing_metrics = REQUIRED_METRICS - expected_metrics
    if missing_metrics:
        return fail(f"missing expected metrics: {sorted(missing_metrics)}")

    fixtures = manifest.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        return fail("fixtures must be a non-empty list")

    seen_categories: set[str] = set()
    seen_ids: set[str] = set()
    for fixture in fixtures:
        fixture_id = fixture.get("id")
        if not fixture_id or fixture_id in seen_ids:
            return fail(f"fixture id is missing or duplicated: {fixture_id!r}")
        seen_ids.add(fixture_id)

        category = fixture.get("category")
        seen_categories.add(category)
        if category not in REQUIRED_CATEGORIES:
            return fail(f"{fixture_id}: unexpected category {category!r}")

        for field in ("purpose", "provenance", "license"):
            value = fixture.get(field)
            if not isinstance(value, str) or not value.strip():
                return fail(f"{fixture_id}: missing {field}")
            if "tbd" in value.lower():
                return fail(f"{fixture_id}: {field} still contains TBD")

        source = fixture.get("source", {})
        if source.get("kind") != "generated":
            return fail(f"{fixture_id}: Phase 0 fixtures must be deterministic/generated")
        if not source.get("generator"):
            return fail(f"{fixture_id}: missing source.generator")
        if source.get("sample_rate") != 48000:
            return fail(f"{fixture_id}: expected 48 kHz fixture sample rate")
        if float(source.get("duration_seconds", 0.0)) <= 0.0:
            return fail(f"{fixture_id}: duration_seconds must be positive")

        metrics = fixture.get("metrics")
        if not isinstance(metrics, list) or not metrics:
            return fail(f"{fixture_id}: metrics must be a non-empty list")
        unknown = set(metrics) - REQUIRED_METRICS
        if unknown:
            return fail(f"{fixture_id}: unknown metrics {sorted(unknown)}")

    missing_categories = REQUIRED_CATEGORIES - seen_categories
    if missing_categories:
        return fail(f"missing fixture categories: {sorted(missing_categories)}")

    print(f"ok: {len(fixtures)} compatibility fixture categories validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

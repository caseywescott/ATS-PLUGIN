#!/usr/bin/env python3
"""Validate the morph-to-ATS fixture manifest contract."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


REQUIRED_CATEGORIES = {
    "sustained_harmonic",
    "vibrato",
    "voice_vowel",
    "inharmonic_metallic",
    "plucked_percussive",
    "noisy_breathy",
    "dense_polyphonic",
}

REQUIRED_ANALYSIS_MODE = "classic_ats"
REQUIRED_SAMPLE_RATE = 48000


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def require_text(value: Any, field: str, owner: str) -> str | None:
    if not isinstance(value, str) or not value.strip():
        return f"{owner}: missing {field}"
    lowered = value.lower()
    if "tbd" in lowered or "todo" in lowered:
        return f"{owner}: {field} still contains placeholder wording"
    return None


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: morph_fixture_manifest_check.py <morph_fixture_manifest.json>", file=sys.stderr)
        return 2

    manifest_path = Path(sys.argv[1])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    if manifest.get("schema_version") != 1:
        return fail("manifest schema_version must be 1")
    if manifest.get("scope") != "morph-to-ats-phase-0":
        return fail("manifest scope must be morph-to-ats-phase-0")
    if manifest.get("default_sample_rate") != REQUIRED_SAMPLE_RATE:
        return fail(f"default_sample_rate must be {REQUIRED_SAMPLE_RATE}")
    if manifest.get("default_expected_analysis_mode") != REQUIRED_ANALYSIS_MODE:
        return fail(f"default_expected_analysis_mode must be {REQUIRED_ANALYSIS_MODE}")

    for field in ("description", "license_policy"):
        problem = require_text(manifest.get(field), field, "manifest")
        if problem:
            return fail(problem)

    fixtures = manifest.get("fixtures")
    if not isinstance(fixtures, list) or not fixtures:
        return fail("fixtures must be a non-empty list")

    seen_ids: set[str] = set()
    seen_categories: set[str] = set()
    for fixture in fixtures:
        if not isinstance(fixture, dict):
            return fail("each fixture must be an object")

        fixture_id = fixture.get("id")
        if not isinstance(fixture_id, str) or not fixture_id.strip():
            return fail("fixture id is missing")
        if fixture_id in seen_ids:
            return fail(f"duplicated fixture id: {fixture_id}")
        seen_ids.add(fixture_id)

        category = fixture.get("category")
        if category not in REQUIRED_CATEGORIES:
            return fail(f"{fixture_id}: unexpected category {category!r}")
        seen_categories.add(category)

        if fixture.get("asset_status") != "placeholder":
            return fail(f"{fixture_id}: asset_status must be placeholder until assets are committed")
        if fixture.get("sample_rate") != REQUIRED_SAMPLE_RATE:
            return fail(f"{fixture_id}: sample_rate must be {REQUIRED_SAMPLE_RATE}")
        if fixture.get("expected_analysis_mode") != REQUIRED_ANALYSIS_MODE:
            return fail(f"{fixture_id}: expected_analysis_mode must be {REQUIRED_ANALYSIS_MODE}")
        if int(fixture.get("channels", 0)) <= 0:
            return fail(f"{fixture_id}: channels must be positive")
        if float(fixture.get("duration_seconds", 0.0)) <= 0.0:
            return fail(f"{fixture_id}: duration_seconds must be positive")

        for field in ("purpose", "license"):
            problem = require_text(fixture.get(field), field, fixture_id)
            if problem:
                return fail(problem)

        provenance = fixture.get("provenance")
        if not isinstance(provenance, dict):
            return fail(f"{fixture_id}: provenance must be an object")
        for field in ("description", "source_url", "copyright_holder"):
            problem = require_text(provenance.get(field), f"provenance.{field}", fixture_id)
            if problem:
                return fail(problem)

        outputs = fixture.get("expected_analysis_outputs")
        if not isinstance(outputs, list) or not all(isinstance(item, str) and item for item in outputs):
            return fail(f"{fixture_id}: expected_analysis_outputs must be a non-empty string list")

    missing_categories = REQUIRED_CATEGORIES - seen_categories
    if missing_categories:
        return fail(f"missing fixture categories: {sorted(missing_categories)}")

    pairings = manifest.get("source_pairings")
    if not isinstance(pairings, list) or not pairings:
        return fail("source_pairings must be a non-empty list")

    paired_fixture_ids: set[str] = set()
    seen_pairing_ids: set[str] = set()
    for pairing in pairings:
        if not isinstance(pairing, dict):
            return fail("each source pairing must be an object")

        pairing_id = pairing.get("id")
        if not isinstance(pairing_id, str) or not pairing_id.strip():
            return fail("source pairing id is missing")
        if pairing_id in seen_pairing_ids:
            return fail(f"duplicated source pairing id: {pairing_id}")
        seen_pairing_ids.add(pairing_id)

        if pairing.get("status") != "placeholder":
            return fail(f"{pairing_id}: status must be placeholder until fixture assets exist")
        for field in ("purpose",):
            problem = require_text(pairing.get(field), field, pairing_id)
            if problem:
                return fail(problem)

        source_a = pairing.get("source_a_fixture_id")
        source_b = pairing.get("source_b_fixture_id")
        if source_a not in seen_ids:
            return fail(f"{pairing_id}: unknown source_a_fixture_id {source_a!r}")
        if source_b not in seen_ids:
            return fail(f"{pairing_id}: unknown source_b_fixture_id {source_b!r}")
        if source_a == source_b:
            return fail(f"{pairing_id}: source A and source B must be different fixtures")
        paired_fixture_ids.update({source_a, source_b})

        assertions = pairing.get("primary_assertions")
        if not isinstance(assertions, list) or not all(isinstance(item, str) and item for item in assertions):
            return fail(f"{pairing_id}: primary_assertions must be a non-empty string list")

    unpaired_fixture_ids = seen_ids - paired_fixture_ids
    if unpaired_fixture_ids:
        return fail(f"fixtures not covered by source_pairings: {sorted(unpaired_fixture_ids)}")

    print(f"ok: {len(fixtures)} morph fixtures and {len(pairings)} source pairings validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

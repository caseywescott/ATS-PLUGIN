#!/usr/bin/env python3
"""Validate the timbre descriptor coverage manifest.

This checker enforces the contract documented in
``docs/TIMBRE_DESCRIPTOR_BUNDLE_MASTER_SPEC.md`` and the descriptor task list.
Every entry in ``tests/fixtures/timbre_descriptor_manifest.json`` must declare:

* a stable id (unique within the manifest),
* a positive integer ``formula_version``,
* a recognized ``domain``,
* a non-empty ``units`` field,
* a recognized ``weighting`` policy,
* a ``prerequisites`` list (may be empty for unconditional descriptors),
* a recognized implementation ``phase``,
* a recognized ``category``,
* an ``expected_status`` mapping covering every fixture category required for
  that descriptor's domain, with values restricted to the enumerated statuses.

The script exits with a non-zero status and prints every failure when any
descriptor is non-compliant.
"""

from __future__ import annotations

import json
import pathlib
import sys
from typing import Iterable


MANIFEST_PATH = (
    pathlib.Path(__file__).resolve().parent
    / "fixtures"
    / "timbre_descriptor_manifest.json"
)


REQUIRED_FRAME_FIXTURE_CATEGORIES = (
    "single_partial",
    "two_partials",
    "flat_bands",
    "dominant_band",
    "partial_residual_split",
    "known_tilt",
    "known_deviation",
    "resonance_peaks",
    "harmonic_frame",
    "stretched_harmonic_frame",
    "inharmonic_frame",
    "low_confidence_f0_frame",
    "waveform_spectrum_frame",
)

REQUIRED_SERIES_FIXTURE_CATEGORIES = (
    "moving_bump_series",
    "attack_series",
    "decay_series",
    "modulation_series",
    "track_birth_death_series",
    "residual_color_motion_series",
)

ALL_FIXTURE_CATEGORIES = (
    REQUIRED_FRAME_FIXTURE_CATEGORIES + REQUIRED_SERIES_FIXTURE_CATEGORIES
)


def load_manifest() -> dict:
    if not MANIFEST_PATH.is_file():
        raise SystemExit(f"manifest not found: {MANIFEST_PATH}")
    with MANIFEST_PATH.open("r", encoding="utf-8") as handle:
        try:
            return json.load(handle)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"manifest JSON parse error: {exc}") from exc


def fixture_categories_for_domain(domain: str) -> Iterable[str]:
    if domain == "temporal_series":
        return REQUIRED_SERIES_FIXTURE_CATEGORIES
    return REQUIRED_FRAME_FIXTURE_CATEGORIES


def main() -> int:
    manifest = load_manifest()
    failures: list[str] = []

    if manifest.get("$schema_version") != 1:
        failures.append("manifest $schema_version must be 1")

    enum_values = manifest.get("$enum_values") or {}
    allowed_domains = set(enum_values.get("domains") or ())
    allowed_statuses = set(enum_values.get("statuses") or ())
    allowed_categories = set(enum_values.get("categories") or ())
    allowed_phases = set(enum_values.get("phases") or ())
    allowed_weightings = set(enum_values.get("weightings") or ())

    if not allowed_domains:
        failures.append("manifest $enum_values.domains missing or empty")
    if not allowed_statuses:
        failures.append("manifest $enum_values.statuses missing or empty")
    if not allowed_categories:
        failures.append("manifest $enum_values.categories missing or empty")
    if not allowed_phases:
        failures.append("manifest $enum_values.phases missing or empty")
    if not allowed_weightings:
        failures.append("manifest $enum_values.weightings missing or empty")

    declared_categories = set(manifest.get("$fixture_categories") or ())
    for category in ALL_FIXTURE_CATEGORIES:
        if category not in declared_categories:
            failures.append(
                f"manifest $fixture_categories must include '{category}'"
            )

    descriptors = manifest.get("descriptors")
    if not isinstance(descriptors, list) or not descriptors:
        failures.append("manifest descriptors must be a non-empty list")
        descriptors = []

    seen_ids: set[str] = set()
    for entry in descriptors:
        identifier = entry.get("id") or "<unnamed>"
        prefix = f"descriptor '{identifier}'"

        if not isinstance(identifier, str) or not identifier:
            failures.append(f"{prefix}: missing string id")
            continue
        if identifier in seen_ids:
            failures.append(f"{prefix}: duplicate id")
        seen_ids.add(identifier)

        formula_version = entry.get("formula_version")
        if not isinstance(formula_version, int) or formula_version < 1:
            failures.append(
                f"{prefix}: formula_version must be a positive integer"
            )

        domain = entry.get("domain")
        if domain not in allowed_domains:
            failures.append(
                f"{prefix}: domain '{domain}' is not in $enum_values.domains"
            )

        units = entry.get("units")
        if not isinstance(units, str) or not units:
            failures.append(f"{prefix}: units must be a non-empty string")

        weighting = entry.get("weighting")
        if weighting not in allowed_weightings:
            failures.append(
                f"{prefix}: weighting '{weighting}' is not in $enum_values.weightings"
            )

        prerequisites = entry.get("prerequisites")
        if not isinstance(prerequisites, list):
            failures.append(
                f"{prefix}: prerequisites must be a list (may be empty)"
            )
        else:
            for prerequisite in prerequisites:
                if not isinstance(prerequisite, str) or not prerequisite:
                    failures.append(
                        f"{prefix}: prerequisite entries must be non-empty strings"
                    )

        phase = entry.get("phase")
        if phase not in allowed_phases:
            failures.append(
                f"{prefix}: phase '{phase}' is not in $enum_values.phases"
            )

        category = entry.get("category")
        if category not in allowed_categories:
            failures.append(
                f"{prefix}: category '{category}' is not in $enum_values.categories"
            )

        expected_status = entry.get("expected_status")
        if not isinstance(expected_status, dict) or not expected_status:
            failures.append(
                f"{prefix}: expected_status must be a non-empty object"
            )
            continue

        required_categories = list(fixture_categories_for_domain(domain or ""))
        for required_category in required_categories:
            if required_category not in expected_status:
                failures.append(
                    f"{prefix}: expected_status is missing fixture category "
                    f"'{required_category}'"
                )

        for fixture_category, status in expected_status.items():
            if fixture_category not in ALL_FIXTURE_CATEGORIES:
                failures.append(
                    f"{prefix}: expected_status uses unknown fixture category "
                    f"'{fixture_category}'"
                )
            if status not in allowed_statuses:
                failures.append(
                    f"{prefix}: expected_status['{fixture_category}'] = "
                    f"'{status}' is not in $enum_values.statuses"
                )

        if category == "conditional_harmonic" and "reliable_f0" not in (
            prerequisites or []
        ):
            failures.append(
                f"{prefix}: conditional_harmonic descriptors must require "
                f"'reliable_f0' in prerequisites"
            )
        if category == "conditional_waveform" and "waveform_spectrum_present" not in (
            prerequisites or []
        ):
            failures.append(
                f"{prefix}: conditional_waveform descriptors must require "
                f"'waveform_spectrum_present' in prerequisites"
            )

    if failures:
        sys.stderr.write("timbre descriptor manifest check failed:\n")
        for failure in failures:
            sys.stderr.write(f"  - {failure}\n")
        return 1

    sys.stdout.write(
        f"timbre descriptor manifest ok: {len(descriptors)} descriptors\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Validate morph-to-ATS metric reports against shared tolerances."""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from typing import Any


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def load_json(path: str) -> dict[str, Any]:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected a JSON object")
    return data


def as_finite_float(value: Any, field: str, owner: str) -> float:
    if not isinstance(value, (int, float)):
        raise ValueError(f"{owner}: {field} must be numeric")
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{owner}: {field} must be finite")
    return number


def require_list(report: dict[str, Any], field: str) -> list[Any]:
    value = report.get(field)
    if not isinstance(value, list) or not value:
        raise ValueError(f"{field} must be a non-empty list")
    return value


# Endpoint fidelity is the contract the optimal-transport morph (Phase 7) must
# never regress: at amount 0 the render must equal source A, at amount 1 source
# B. To stop that contract being quietly loosened, the harness caps how large
# the endpoint tolerances are allowed to be, independent of the JSON file.
ENDPOINT_TOLERANCE_CEILING = 1.0e-3


def check_tolerances(tolerances: dict[str, Any]) -> None:
    if tolerances.get("schema_version") != 1:
        raise ValueError("tolerances schema_version must be 1")
    if tolerances.get("scope") != "morph-to-ats-metric-tolerances":
        raise ValueError("tolerances scope must be morph-to-ats-metric-tolerances")

    for group, fields in {
        "endpoint_fidelity": ("max_rms_error", "max_peak_error"),
        "per_band_power_delta": ("max_abs_delta_db",),
        "correspondence_churn": ("max_churn_fraction",),
        "trajectory_smoothness": ("max_frequency_step_cents", "max_amplitude_step_db"),
        "transport_cost": ("max_normalized_cost", "max_monotonicity_violation"),
        "count_mismatch_continuity": ("max_amplitude_jump_db", "max_frequency_jump_cents"),
    }.items():
        config = tolerances.get(group)
        if not isinstance(config, dict):
            raise ValueError(f"missing tolerance group: {group}")
        for field in fields:
            if as_finite_float(config.get(field), field, group) < 0.0:
                raise ValueError(f"{group}: {field} must be non-negative")

    # Pin the endpoint contract: tolerances may be tightened but not loosened
    # past the ceiling, so optimal-transport work cannot mask an endpoint drift.
    endpoint = tolerances["endpoint_fidelity"]
    for field in ("max_rms_error", "max_peak_error"):
        if float(endpoint[field]) > ENDPOINT_TOLERANCE_CEILING:
            raise ValueError(
                f"endpoint_fidelity.{field} {endpoint[field]} exceeds the pinned "
                f"ceiling {ENDPOINT_TOLERANCE_CEILING}"
            )

    required_amounts = tolerances["per_band_power_delta"].get("required_morph_amounts")
    if not isinstance(required_amounts, list) or not required_amounts:
        raise ValueError("per_band_power_delta.required_morph_amounts must be a non-empty list")
    for index, amount in enumerate(required_amounts):
        value = as_finite_float(amount, f"required_morph_amounts[{index}]", "per_band_power_delta")
        if value < 0.0 or value > 1.0:
            raise ValueError("required morph amounts must be between 0 and 1")


def check_report_shape(report: dict[str, Any]) -> None:
    if report.get("schema_version") != 1:
        raise ValueError("report schema_version must be 1")
    if report.get("scope") != "morph-to-ats-metric-smoke":
        raise ValueError("report scope must be morph-to-ats-metric-smoke")
    for field in ("pairing_id", "description"):
        value = report.get(field)
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"report {field} must be non-empty text")


def check_endpoint_fidelity(report: dict[str, Any], tolerances: dict[str, Any]) -> int:
    endpoint_tolerances = tolerances["endpoint_fidelity"]
    max_rms = float(endpoint_tolerances["max_rms_error"])
    max_peak = float(endpoint_tolerances["max_peak_error"])
    rows = require_list(report, "endpoint_fidelity")

    seen_targets: set[str] = set()
    failures = 0
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"endpoint_fidelity[{index}] must be an object")
        owner = f"endpoint_fidelity[{index}]"
        amount = as_finite_float(row.get("morph_amount"), "morph_amount", owner)
        target = row.get("target")
        rms = as_finite_float(row.get("rms_error"), "rms_error", owner)
        peak = as_finite_float(row.get("peak_error"), "peak_error", owner)

        if amount not in (0.0, 1.0):
            raise ValueError(f"{owner}: morph_amount must be 0 or 1")
        if target not in ("source_a", "source_b"):
            raise ValueError(f"{owner}: target must be source_a or source_b")
        seen_targets.add(target)

        if rms > max_rms:
            print(f"{owner}: rms_error {rms} exceeds {max_rms}", file=sys.stderr)
            failures += 1
        if peak > max_peak:
            print(f"{owner}: peak_error {peak} exceeds {max_peak}", file=sys.stderr)
            failures += 1

    if seen_targets != {"source_a", "source_b"}:
        raise ValueError("endpoint_fidelity must include source_a and source_b endpoints")
    return failures


def check_per_band_power_delta(report: dict[str, Any], tolerances: dict[str, Any]) -> int:
    power_tolerances = tolerances["per_band_power_delta"]
    max_delta = float(power_tolerances["max_abs_delta_db"])
    required_amounts = {round(float(amount), 6) for amount in power_tolerances["required_morph_amounts"]}
    rows = require_list(report, "per_band_power_delta")

    seen_amounts: set[float] = set()
    failures = 0
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"per_band_power_delta[{index}] must be an object")
        owner = f"per_band_power_delta[{index}]"
        amount = as_finite_float(row.get("morph_amount"), "morph_amount", owner)
        band_index = row.get("band_index")
        delta = as_finite_float(row.get("delta_db"), "delta_db", owner)

        if amount < 0.0 or amount > 1.0:
            raise ValueError(f"{owner}: morph_amount must be between 0 and 1")
        if not isinstance(band_index, int) or band_index < 0:
            raise ValueError(f"{owner}: band_index must be a non-negative integer")
        seen_amounts.add(round(amount, 6))

        if abs(delta) > max_delta:
            print(f"{owner}: abs(delta_db) {abs(delta)} exceeds {max_delta}", file=sys.stderr)
            failures += 1

    missing_amounts = required_amounts - seen_amounts
    if missing_amounts:
        raise ValueError(f"per_band_power_delta missing morph amounts: {sorted(missing_amounts)}")
    return failures


def check_correspondence_churn(report: dict[str, Any], tolerances: dict[str, Any]) -> int:
    max_churn = float(tolerances["correspondence_churn"]["max_churn_fraction"])
    rows = require_list(report, "correspondence_churn")

    failures = 0
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"correspondence_churn[{index}] must be an object")
        owner = f"correspondence_churn[{index}]"
        frame_index = row.get("frame_index")
        band_index = row.get("band_index")
        changed_pairs = row.get("changed_pairs")
        total_pairs = row.get("total_pairs")
        churn = as_finite_float(row.get("churn_fraction"), "churn_fraction", owner)

        for field, value in (
            ("frame_index", frame_index),
            ("band_index", band_index),
            ("changed_pairs", changed_pairs),
            ("total_pairs", total_pairs),
        ):
            if not isinstance(value, int) or value < 0:
                raise ValueError(f"{owner}: {field} must be a non-negative integer")
        if total_pairs == 0:
            raise ValueError(f"{owner}: total_pairs must be positive")
        expected = changed_pairs / total_pairs
        if abs(churn - expected) > 1e-9:
            raise ValueError(f"{owner}: churn_fraction must equal changed_pairs / total_pairs")
        if churn > max_churn:
            print(f"{owner}: churn_fraction {churn} exceeds {max_churn}", file=sys.stderr)
            failures += 1
    return failures


def check_transport_cost(report: dict[str, Any], tolerances: dict[str, Any]) -> int:
    """Validate the realized optimal-transport cost across the morph sweep.

    Cost is the perceptual-frequency mass movement of the partial
    correspondence at a given morph amount, normalized so that 1.0 is the full
    A->B plan. The gate enforces the continuity properties the OT morph (Phase
    7) must satisfy: endpoints are anchored (amount 0 moves no mass), cost is
    bounded, and it rises monotonically with morph amount (no sudden re-pairing
    that would pop). A small per-step monotonicity tolerance absorbs numerical
    noise without permitting a real backward jump.
    """
    transport_tolerances = tolerances["transport_cost"]
    max_cost = float(transport_tolerances["max_normalized_cost"])
    max_violation = float(transport_tolerances["max_monotonicity_violation"])
    rows = require_list(report, "transport_cost")

    points: list[tuple[float, float]] = []
    failures = 0
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"transport_cost[{index}] must be an object")
        owner = f"transport_cost[{index}]"
        amount = as_finite_float(row.get("morph_amount"), "morph_amount", owner)
        cost = as_finite_float(row.get("normalized_cost"), "normalized_cost", owner)
        if amount < 0.0 or amount > 1.0:
            raise ValueError(f"{owner}: morph_amount must be between 0 and 1")
        if cost < 0.0:
            raise ValueError(f"{owner}: normalized_cost must be non-negative")
        points.append((amount, cost))
        if cost > max_cost:
            print(f"{owner}: normalized_cost {cost} exceeds {max_cost}", file=sys.stderr)
            failures += 1

    points.sort(key=lambda item: item[0])
    if points[0][0] != 0.0:
        raise ValueError("transport_cost must include the amount 0 endpoint")
    if abs(points[0][1]) > max_violation:
        print(
            f"transport_cost: amount 0 normalized_cost {points[0][1]} is not anchored to 0",
            file=sys.stderr,
        )
        failures += 1
    for (prev_amount, prev_cost), (amount, cost) in zip(points, points[1:]):
        if cost < prev_cost - max_violation:
            print(
                f"transport_cost: cost drops from {prev_cost} (amount {prev_amount}) "
                f"to {cost} (amount {amount})",
                file=sys.stderr,
            )
            failures += 1
    return failures


def check_count_mismatch_continuity(report: dict[str, Any], tolerances: dict[str, Any]) -> int:
    """Validate pop-free continuity for count-mismatched source pairs.

    When A and B have different partial counts the correspondence must split or
    merge mass rather than abruptly birthing/killing partials; the largest
    single-step amplitude and frequency jump introduced by that handling is the
    audible pop risk. Each row must describe an actually count-mismatched pair.
    """
    continuity_tolerances = tolerances["count_mismatch_continuity"]
    max_amplitude_jump = float(continuity_tolerances["max_amplitude_jump_db"])
    max_frequency_jump = float(continuity_tolerances["max_frequency_jump_cents"])
    rows = require_list(report, "count_mismatch_continuity")

    failures = 0
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"count_mismatch_continuity[{index}] must be an object")
        owner = f"count_mismatch_continuity[{index}]"
        amount = as_finite_float(row.get("morph_amount"), "morph_amount", owner)
        source_a_partials = row.get("source_a_partials")
        source_b_partials = row.get("source_b_partials")
        amplitude_jump = as_finite_float(row.get("max_amplitude_jump_db"), "max_amplitude_jump_db", owner)
        frequency_jump = as_finite_float(row.get("max_frequency_jump_cents"), "max_frequency_jump_cents", owner)

        if amount < 0.0 or amount > 1.0:
            raise ValueError(f"{owner}: morph_amount must be between 0 and 1")
        for field, value in (
            ("source_a_partials", source_a_partials),
            ("source_b_partials", source_b_partials),
        ):
            if not isinstance(value, int) or value <= 0:
                raise ValueError(f"{owner}: {field} must be a positive integer")
        if source_a_partials == source_b_partials:
            raise ValueError(
                f"{owner}: source partial counts must differ (this gate is for count-mismatched pairs)"
            )
        if amplitude_jump < 0.0 or frequency_jump < 0.0:
            raise ValueError(f"{owner}: jump magnitudes must be non-negative")

        if amplitude_jump > max_amplitude_jump:
            print(
                f"{owner}: max_amplitude_jump_db {amplitude_jump} exceeds {max_amplitude_jump}",
                file=sys.stderr,
            )
            failures += 1
        if frequency_jump > max_frequency_jump:
            print(
                f"{owner}: max_frequency_jump_cents {frequency_jump} exceeds {max_frequency_jump}",
                file=sys.stderr,
            )
            failures += 1
    return failures


def check_trajectory_smoothness(report: dict[str, Any], tolerances: dict[str, Any]) -> int:
    trajectory_tolerances = tolerances["trajectory_smoothness"]
    max_frequency_step = float(trajectory_tolerances["max_frequency_step_cents"])
    max_amplitude_step = float(trajectory_tolerances["max_amplitude_step_db"])
    rows = require_list(report, "trajectory_smoothness")

    failures = 0
    for index, row in enumerate(rows):
        if not isinstance(row, dict):
            raise ValueError(f"trajectory_smoothness[{index}] must be an object")
        owner = f"trajectory_smoothness[{index}]"
        partial_id = row.get("partial_id")
        frequency_step = as_finite_float(row.get("max_frequency_step_cents"), "max_frequency_step_cents", owner)
        amplitude_step = as_finite_float(row.get("max_amplitude_step_db"), "max_amplitude_step_db", owner)

        if not isinstance(partial_id, str) or not partial_id.strip():
            raise ValueError(f"{owner}: partial_id must be non-empty text")
        if frequency_step > max_frequency_step:
            print(
                f"{owner}: max_frequency_step_cents {frequency_step} exceeds {max_frequency_step}",
                file=sys.stderr,
            )
            failures += 1
        if amplitude_step > max_amplitude_step:
            print(
                f"{owner}: max_amplitude_step_db {amplitude_step} exceeds {max_amplitude_step}",
                file=sys.stderr,
            )
            failures += 1
    return failures


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: morph_metric_harness.py <metric-tolerances.json> <metric-report.json>", file=sys.stderr)
        return 2

    try:
        tolerances = load_json(sys.argv[1])
        report = load_json(sys.argv[2])
        check_tolerances(tolerances)
        check_report_shape(report)
        failures = (
            check_endpoint_fidelity(report, tolerances)
            + check_per_band_power_delta(report, tolerances)
            + check_correspondence_churn(report, tolerances)
            + check_transport_cost(report, tolerances)
            + check_count_mismatch_continuity(report, tolerances)
            + check_trajectory_smoothness(report, tolerances)
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return fail(str(exc))

    if failures:
        return fail(f"morph metric report failed {failures} tolerance check(s)")

    print(
        "ok: morph metrics validated "
        f"({len(report['endpoint_fidelity'])} endpoints, "
        f"{len(report['per_band_power_delta'])} band-power rows, "
        f"{len(report['correspondence_churn'])} churn rows, "
        f"{len(report['transport_cost'])} transport-cost rows, "
        f"{len(report['count_mismatch_continuity'])} count-mismatch rows, "
        f"{len(report['trajectory_smoothness'])} trajectories)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

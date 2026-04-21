#!/usr/bin/env python3
"""
Phase 0 oracle checks for spectral-envelope transfer.

This does not estimate envelopes. It uses known source/target envelopes over an
inharmonic ATS-like partial frame to prove the baseline transfer math and the
double-coloring risk of target-only scaling.
"""

from __future__ import annotations

import math
import sys


PARTIAL_FREQS_HZ = [
    97.3,
    211.7,
    438.2,
    713.9,
    1184.4,
    2077.6,
    3419.3,
    5901.8,
]

# Fine source detail that should survive envelope transfer.
FINE_DETAIL_DB = [
    -1.2,
    0.8,
    -0.4,
    1.1,
    -0.7,
    0.3,
    -0.9,
    0.5,
]


def rms(values: list[float]) -> float:
    return math.sqrt(sum(v * v for v in values) / max(1, len(values)))


def source_env_db(freq_hz: float) -> float:
    """A broad, source-specific color curve in dB."""
    x = math.log(freq_hz / 80.0) / math.log(8000.0 / 80.0)
    return 7.0 * math.sin(1.45 * math.pi * x) - 5.5 * x


def target_env_db(freq_hz: float) -> float:
    """A different broad target color curve in dB."""
    x = math.log(freq_hz / 80.0) / math.log(8000.0 / 80.0)
    return -4.0 * math.cos(1.8 * math.pi * x) + 3.0 * x - 1.0


def assert_inharmonic_grid(freqs_hz: list[float]) -> None:
    base = freqs_hz[0]
    deviations = [abs((freq / base) - round(freq / base)) for freq in freqs_hz[1:]]
    if max(deviations) < 0.08:
        raise AssertionError("test frequencies are too close to a harmonic grid")


def main() -> int:
    assert_inharmonic_grid(PARTIAL_FREQS_HZ)

    source_env = [source_env_db(freq) for freq in PARTIAL_FREQS_HZ]
    target_env = [target_env_db(freq) for freq in PARTIAL_FREQS_HZ]

    source_frame = [
        detail + env for detail, env in zip(FINE_DETAIL_DB, source_env)
    ]
    expected = [
        detail + env for detail, env in zip(FINE_DETAIL_DB, target_env)
    ]

    # Creative but unsafe as the default: source color remains, target color is
    # added, so the result is double-colored.
    target_only = [
        amp + env for amp, env in zip(source_frame, target_env)
    ]

    # Safe default: remove the source envelope, preserve fine detail, apply the
    # target envelope.
    decolor_transfer = [
        amp - src_env + tgt_env
        for amp, src_env, tgt_env in zip(source_frame, source_env, target_env)
    ]

    target_only_error = rms([
        actual - want for actual, want in zip(target_only, expected)
    ])
    decolor_transfer_error = rms([
        actual - want for actual, want in zip(decolor_transfer, expected)
    ])

    if decolor_transfer_error > 1e-10:
        print(
            f"decolor transfer failed oracle: rms={decolor_transfer_error:.6g}",
            file=sys.stderr,
        )
        return 1

    if target_only_error < 2.0:
        print(
            f"target-only baseline did not expose double-coloring: "
            f"rms={target_only_error:.6g}",
            file=sys.stderr,
        )
        return 1

    if decolor_transfer_error * 1e8 > target_only_error:
        print(
            "decolor transfer is not materially better than target-only "
            f"(transfer={decolor_transfer_error:.6g}, "
            f"target_only={target_only_error:.6g})",
            file=sys.stderr,
        )
        return 1

    print(
        "ok: inharmonic oracle "
        f"target_only_rms={target_only_error:.6g} "
        f"decolor_transfer_rms={decolor_transfer_error:.6g}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

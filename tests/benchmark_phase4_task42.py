#!/usr/bin/env python3
"""Task 4.2 benchmark: temporal state-space smoothing on sequential fixtures."""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from visualize_envelopes import (
    TemporalEnvelopeFrame,
    TemporalEnvelopeSmoothingOptions,
    _frequency_for_bark_position,
    _normalized_position_bark,
    fit_new_model_sequence,
)

SR = 48000.0
LO = 80.0
HI = 8000.0
ORD = 16
REG = 1.0e-3
GRID_SIZE = 48


def base_env_db(freq_hz: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    return (
        -15.0
        + 8.0 * math.cos(math.pi * x)
        - 5.0 * math.cos(2.0 * math.pi * x)
        + 2.2 * math.cos(3.0 * math.pi * x)
    )


def onset_env_db(freq_hz: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    onset_bump = 5.5 * math.exp(-0.5 * ((x - 0.56) / 0.12) ** 2)
    return base_env_db(freq_hz) + onset_bump


def drifting_env_db(freq_hz: float, phase: float) -> float:
    x = _normalized_position_bark(freq_hz, LO, HI)
    center = 0.48 + 0.055 * math.sin(phase)
    width = 0.09 + 0.01 * math.cos(phase)
    bump = 4.2 * math.exp(-0.5 * ((x - center) / width) ** 2)
    tilt = 1.2 * math.sin(phase) * (x - 0.5)
    return base_env_db(freq_hz) + bump + tilt


def make_partials(
    freqs_hz: list[float],
    db_fn,
    noise_db: float = 0.0,
    seed: int = 0,
) -> list:
    import random

    rng = random.Random(seed)
    partials = []
    for freq_hz in freqs_hz:
        db = db_fn(freq_hz)
        if noise_db:
            db += rng.gauss(0.0, noise_db)
        partials.append([freq_hz, 10.0 ** (db / 20.0), 10.0])
    return partials


def interpolate_truth(truth_hz: list[float], truth_db: list[float], query_hz: list[float]) -> list[float]:
    def interp_one(freq_hz: float) -> float:
        if freq_hz <= truth_hz[0]:
            return truth_db[0]
        if freq_hz >= truth_hz[-1]:
            return truth_db[-1]
        lo, hi = 0, len(truth_hz) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if truth_hz[mid] <= freq_hz:
                lo = mid
            else:
                hi = mid
        blend = (freq_hz - truth_hz[lo]) / max(truth_hz[hi] - truth_hz[lo], 1.0e-12)
        return truth_db[lo] + blend * (truth_db[hi] - truth_db[lo])

    return [interp_one(freq_hz) for freq_hz in query_hz]


def bark_grid() -> list[float]:
    return [
        _frequency_for_bark_position(i / float(GRID_SIZE - 1), LO, HI)
        for i in range(GRID_SIZE)
    ]


TRUTH_GRID_HZ = bark_grid()


def truth_frame_grid(db_fn) -> list[float]:
    return [db_fn(freq_hz) for freq_hz in TRUTH_GRID_HZ]


def rms_error(values_a: list[float], values_b: list[float]) -> float:
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(values_a, values_b)) / max(len(values_a), 1))


def mean_frame_rms(sequence: list[tuple[list[float], list[float], dict, object]], truth_grids: list[list[float]]) -> float:
    per_frame = []
    for (grid_hz, grid_db, _, _), truth_db in zip(sequence, truth_grids):
        truth_on_grid = interpolate_truth(TRUTH_GRID_HZ, truth_db, grid_hz)
        per_frame.append(rms_error(grid_db, truth_on_grid))
    return sum(per_frame) / max(len(per_frame), 1)


def temporal_jitter(
    sequence: list[tuple[list[float], list[float], dict, object]],
    truth_grids: list[list[float]] | None = None,
    transient_frames: set[int] | None = None,
    band_hz: tuple[float, float] | None = None,
) -> float:
    transient_frames = transient_frames or set()
    diffs = []
    for idx in range(1, len(sequence)):
        if idx in transient_frames:
            continue
        prev_grid_hz, prev_grid_db, _, _ = sequence[idx - 1]
        curr_grid_hz, curr_grid_db, _, _ = sequence[idx]
        for point_idx, freq_hz in enumerate(curr_grid_hz):
            if band_hz and not (band_hz[0] <= freq_hz <= band_hz[1]):
                continue
            truth_delta = 0.0
            if truth_grids is not None:
                truth_prev = interpolate_truth(TRUTH_GRID_HZ, truth_grids[idx - 1], prev_grid_hz)
                truth_curr = interpolate_truth(TRUTH_GRID_HZ, truth_grids[idx], curr_grid_hz)
                truth_delta = truth_curr[point_idx] - truth_prev[point_idx]
            diffs.append((curr_grid_db[point_idx] - prev_grid_db[point_idx]) - truth_delta)
    return math.sqrt(sum(diff * diff for diff in diffs) / max(len(diffs), 1))


def motion_rms(
    sequence: list[tuple[list[float], list[float], dict, object]],
    band_hz: tuple[float, float],
) -> float:
    diffs = []
    for idx in range(1, len(sequence)):
        prev_grid_hz, prev_grid_db, _, _ = sequence[idx - 1]
        curr_grid_hz, curr_grid_db, _, _ = sequence[idx]
        for point_idx, freq_hz in enumerate(curr_grid_hz):
            if band_hz[0] <= freq_hz <= band_hz[1]:
                diffs.append(curr_grid_db[point_idx] - prev_grid_db[point_idx])
    return math.sqrt(sum(diff * diff for diff in diffs) / max(len(diffs), 1))


def band_values(grid_hz: list[float], values: list[float], band_hz: tuple[float, float]) -> list[float]:
    return [
        value
        for freq_hz, value in zip(grid_hz, values)
        if band_hz[0] <= freq_hz <= band_hz[1]
    ]


def fit_sequence(frames: list[TemporalEnvelopeFrame], smoothing_enabled: bool):
    options = TemporalEnvelopeSmoothingOptions(enabled=smoothing_enabled, bark_grid_size=GRID_SIZE)
    return fit_new_model_sequence(
        frames,
        SR,
        LO,
        HI,
        order=ORD,
        regularization=REG,
        temporal_options=options,
        true_envelope_release_mode="heuristic",
    )


def fixture_t1() -> tuple[list[TemporalEnvelopeFrame], list[list[float]]]:
    freqs_per_frame = [
        [220.0, 440.0, 660.0, 1100.0, 1540.0, 2860.0],
        [220.0, 660.0, 880.0, 1320.0, 1760.0, 3300.0],
        [220.0, 440.0, 880.0, 1100.0, 1980.0, 3520.0],
        [220.0, 440.0, 660.0, 1320.0, 1760.0, 3080.0],
        [220.0, 660.0, 880.0, 1540.0, 2200.0, 3960.0],
        [220.0, 440.0, 880.0, 1320.0, 1980.0, 3740.0],
    ]
    frames = [
        TemporalEnvelopeFrame(
            partials=make_partials(freqs, base_env_db, noise_db=0.9, seed=100 + idx),
            label=f"T1_{idx}",
        )
        for idx, freqs in enumerate(freqs_per_frame)
    ]
    truth_grids = [truth_frame_grid(base_env_db) for _ in frames]
    return frames, truth_grids


def fixture_t2() -> tuple[list[TemporalEnvelopeFrame], list[list[float]]]:
    freqs = [210.0, 420.0, 630.0, 840.0, 1260.0, 1680.0, 2520.0, 3360.0]
    envs = [base_env_db, onset_env_db, base_env_db, base_env_db, base_env_db]
    frames = []
    for idx, env_fn in enumerate(envs):
        frames.append(TemporalEnvelopeFrame(
            partials=make_partials(freqs, env_fn, noise_db=0.55, seed=200 + idx),
            transient_boundary=(idx == 1),
            label=f"T2_{idx}",
        ))
    truth_grids = [truth_frame_grid(env_fn) for env_fn in envs]
    return frames, truth_grids


def fixture_t3() -> tuple[list[TemporalEnvelopeFrame], list[list[float]]]:
    phases = [0.0, 0.55, 1.10, 0.65, 0.10, -0.45]
    fundamentals = [210.0, 214.0, 219.0, 216.0, 212.0, 208.0]
    frames = []
    truth_grids = []
    previous_fund = fundamentals[0]
    for idx, (phase, fundamental) in enumerate(zip(phases, fundamentals)):
        freqs = [fundamental * k for k in range(1, 9) if fundamental * k <= HI]
        env_fn = lambda freq_hz, ph=phase: drifting_env_db(freq_hz, ph)
        motion_scale = 1.0 + abs(fundamental - previous_fund) / 2.0
        previous_fund = fundamental
        frames.append(TemporalEnvelopeFrame(
            partials=make_partials(freqs, env_fn, noise_db=0.65, seed=300 + idx),
            motion_scale=motion_scale,
            label=f"T3_{idx}",
        ))
        truth_grids.append(truth_frame_grid(env_fn))
    return frames, truth_grids


def fixture_regression_s1() -> tuple[list[TemporalEnvelopeFrame], list[list[float]]]:
    fundamentals = [200.0, 201.0, 199.0, 200.5]
    frames = []
    truth_grids = []
    for idx, fundamental in enumerate(fundamentals):
        freqs = [fundamental * k for k in range(1, 41) if fundamental * k <= HI]
        frames.append(TemporalEnvelopeFrame(
            partials=make_partials(freqs, base_env_db, noise_db=1.4, seed=400 + idx),
            label=f"S1_{idx}",
        ))
        truth_grids.append(truth_frame_grid(base_env_db))
    return frames, truth_grids


def fixture_regression_s3() -> tuple[list[TemporalEnvelopeFrame], list[list[float]]]:
    frames = []
    truth_grids = []
    for idx, fundamental in enumerate([200.0, 202.0, 198.0, 201.0]):
        freqs = [fundamental * k for k in range(1, 20) if fundamental * k <= HI]

        def outlier_env(freq_hz: float, bad={round(freqs[4], 1), round(freqs[11], 1)}):
            db = base_env_db(freq_hz)
            return db + (15.0 if round(freq_hz, 1) in bad else 0.0)

        frames.append(TemporalEnvelopeFrame(
            partials=make_partials(freqs, outlier_env, noise_db=0.45, seed=500 + idx),
            label=f"S3_{idx}",
        ))
        truth_grids.append(truth_frame_grid(base_env_db))
    return frames, truth_grids


def fixture_regression_s5() -> tuple[list[TemporalEnvelopeFrame], list[list[float]]]:
    ratios = [1.0, 2.76, 5.41, 8.93, 11.9, 15.4]
    fundamentals = [420.0, 424.0, 418.0, 421.0]
    frames = []
    truth_grids = []
    for idx, fundamental in enumerate(fundamentals):
        freqs = [fundamental * ratio for ratio in ratios if fundamental * ratio <= HI]
        frames.append(TemporalEnvelopeFrame(
            partials=make_partials(freqs, base_env_db, noise_db=0.25, seed=600 + idx),
            label=f"S5_{idx}",
        ))
        truth_grids.append(truth_frame_grid(base_env_db))
    return frames, truth_grids


def evaluate_task42():
    t1_frames, t1_truth = fixture_t1()
    t1_raw = fit_sequence(t1_frames, smoothing_enabled=False)
    t1_smooth = fit_sequence(t1_frames, smoothing_enabled=True)
    t1_raw_jitter = temporal_jitter(t1_raw, truth_grids=t1_truth)
    t1_smooth_jitter = temporal_jitter(t1_smooth, truth_grids=t1_truth)

    t2_frames, t2_truth = fixture_t2()
    t2_raw = fit_sequence(t2_frames, smoothing_enabled=False)
    t2_smooth = fit_sequence(t2_frames, smoothing_enabled=True)
    onset_band = (900.0, 2600.0)
    t2_truth_onset = band_values(TRUTH_GRID_HZ, t2_truth[1], onset_band)
    t2_raw_onset = rms_error(band_values(t2_raw[1][0], t2_raw[1][1], onset_band), t2_truth_onset)
    t2_smooth_onset = rms_error(band_values(t2_smooth[1][0], t2_smooth[1][1], onset_band), t2_truth_onset)

    t3_frames, t3_truth = fixture_t3()
    t3_raw = fit_sequence(t3_frames, smoothing_enabled=False)
    t3_smooth = fit_sequence(t3_frames, smoothing_enabled=True)
    drift_band = (900.0, 2600.0)
    truth_motion = motion_rms([
        (TRUTH_GRID_HZ, truth_grid, {}, None) for truth_grid in t3_truth
    ], drift_band)
    raw_motion = motion_rms(t3_raw, drift_band)
    smooth_motion = motion_rms(t3_smooth, drift_band)
    raw_tracking_error = temporal_jitter(t3_raw, truth_grids=t3_truth, band_hz=drift_band)
    smooth_tracking_error = temporal_jitter(t3_smooth, truth_grids=t3_truth, band_hz=drift_band)

    s1_frames, s1_truth = fixture_regression_s1()
    s1_raw = fit_sequence(s1_frames, smoothing_enabled=False)
    s1_smooth = fit_sequence(s1_frames, smoothing_enabled=True)
    s3_frames, s3_truth = fixture_regression_s3()
    s3_raw = fit_sequence(s3_frames, smoothing_enabled=False)
    s3_smooth = fit_sequence(s3_frames, smoothing_enabled=True)
    s5_frames, s5_truth = fixture_regression_s5()
    s5_raw = fit_sequence(s5_frames, smoothing_enabled=False)
    s5_smooth = fit_sequence(s5_frames, smoothing_enabled=True)

    return {
        "T1": {
            "raw_jitter_db": t1_raw_jitter,
            "smoothed_jitter_db": t1_smooth_jitter,
            "improvement_pct": ((t1_raw_jitter - t1_smooth_jitter) / t1_raw_jitter) * 100.0,
        },
        "T2": {
            "raw_onset_rms_db": t2_raw_onset,
            "smoothed_onset_rms_db": t2_smooth_onset,
            "worsening_pct": ((t2_smooth_onset - t2_raw_onset) / max(t2_raw_onset, 1.0e-9)) * 100.0,
        },
        "T3": {
            "truth_motion_db": truth_motion,
            "raw_motion_db": raw_motion,
            "smoothed_motion_db": smooth_motion,
            "motion_ratio": smooth_motion / max(truth_motion, 1.0e-9),
            "raw_tracking_error_db": raw_tracking_error,
            "smoothed_tracking_error_db": smooth_tracking_error,
        },
        "S1": {
            "raw_rms_db": mean_frame_rms(s1_raw, s1_truth),
            "smoothed_rms_db": mean_frame_rms(s1_smooth, s1_truth),
        },
        "S3": {
            "raw_rms_db": mean_frame_rms(s3_raw, s3_truth),
            "smoothed_rms_db": mean_frame_rms(s3_smooth, s3_truth),
        },
        "S5": {
            "raw_rms_db": mean_frame_rms(s5_raw, s5_truth),
            "smoothed_rms_db": mean_frame_rms(s5_smooth, s5_truth),
        },
    }


def main() -> int:
    results = evaluate_task42()
    print("Task 4.2 temporal state-space smoothing benchmark")
    print("")
    print("Primary gates")
    print(
        f"  T1 jitter: raw={results['T1']['raw_jitter_db']:.6f} dB "
        f"smoothed={results['T1']['smoothed_jitter_db']:.6f} dB "
        f"improvement={results['T1']['improvement_pct']:.2f}%"
    )
    print(
        f"  T2 onset RMS: raw={results['T2']['raw_onset_rms_db']:.6f} dB "
        f"smoothed={results['T2']['smoothed_onset_rms_db']:.6f} dB "
        f"worsening={results['T2']['worsening_pct']:.2f}%"
    )
    print(
        f"  T3 motion: truth={results['T3']['truth_motion_db']:.6f} dB "
        f"raw={results['T3']['raw_motion_db']:.6f} dB "
        f"smoothed={results['T3']['smoothed_motion_db']:.6f} dB "
        f"ratio={results['T3']['motion_ratio']:.3f} "
        f"tracking raw={results['T3']['raw_tracking_error_db']:.6f} dB "
        f"tracking smoothed={results['T3']['smoothed_tracking_error_db']:.6f} dB"
    )
    print("")
    print("No-regression checks")
    for key in ("S1", "S3", "S5"):
        raw_rms = results[key]["raw_rms_db"]
        smoothed_rms = results[key]["smoothed_rms_db"]
        print(
            f"  {key}: raw={raw_rms:.6f} dB smoothed={smoothed_rms:.6f} dB "
            f"delta={smoothed_rms - raw_rms:+.6f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

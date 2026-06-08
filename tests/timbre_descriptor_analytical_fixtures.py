"""Deterministic synthetic frame and series builders for descriptor fixtures.

This module reads ``tests/fixtures/timbre_descriptor_analytical_fixtures.json``
and exposes builder helpers that materialize:

* per-frame descriptor inputs (partials, residual bands, fitted envelope grids,
  optional waveform spectra),
* series descriptor inputs (lists of frames with a documented frame hop),
* track-style series inputs (per-track amplitude curves derived into per-frame
  partials).

Every builder is deterministic given the JSON file and the listed parameters.
This is the source of truth for the analytical test suite added in a later
commit; the C++ tests use the same JSON to load the same expectations.
"""

from __future__ import annotations

import json
import math
import pathlib
from dataclasses import dataclass, field
from typing import Iterable


FIXTURE_PATH = (
    pathlib.Path(__file__).resolve().parent
    / "fixtures"
    / "timbre_descriptor_analytical_fixtures.json"
)


@dataclass(frozen=True)
class Partial:
    frequency_hz: float
    amplitude: float
    confidence: float = 1.0
    selected: bool = True


@dataclass(frozen=True)
class ResidualBand:
    frequency_hz: float
    energy: float
    orphan: bool = False
    confidence: float = 1.0


@dataclass(frozen=True)
class EnvelopeSample:
    frequency_hz: float
    value_db: float


@dataclass(frozen=True)
class WaveformBin:
    frequency_hz: float
    amplitude: float


@dataclass
class Frame:
    fixture_id: str
    category: str
    partials: list[Partial] = field(default_factory=list)
    residual_bands: list[ResidualBand] = field(default_factory=list)
    fitted_envelope: list[EnvelopeSample] = field(default_factory=list)
    waveform_spectrum: list[WaveformBin] = field(default_factory=list)
    fundamental_frequency_hz: float = 0.0
    f0_confidence: float = 0.0
    waveform_available: bool = False


@dataclass
class SeriesFrame:
    partials: list[Partial] = field(default_factory=list)
    residual_bands: list[ResidualBand] = field(default_factory=list)


@dataclass
class Series:
    fixture_id: str
    category: str
    frame_hop_seconds: float
    frames: list[SeriesFrame]


def _load_fixtures() -> dict:
    if not FIXTURE_PATH.is_file():
        raise FileNotFoundError(
            f"analytical fixture file not found: {FIXTURE_PATH}"
        )
    with FIXTURE_PATH.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _amplitude_from_db(db_value: float) -> float:
    return 10.0 ** (db_value / 20.0)


def _make_partial(entry: dict) -> Partial:
    if "amplitude_db" in entry:
        amplitude = _amplitude_from_db(float(entry["amplitude_db"]))
    else:
        amplitude = float(entry.get("amplitude", 0.0))
    return Partial(
        frequency_hz=float(entry["frequency_hz"]),
        amplitude=amplitude,
        confidence=float(entry.get("confidence", 1.0)),
        selected=bool(entry.get("selected", True)),
    )


def _make_residual(entry: dict) -> ResidualBand:
    return ResidualBand(
        frequency_hz=float(entry["frequency_hz"]),
        energy=float(entry["energy"]),
        orphan=bool(entry.get("orphan", False)),
        confidence=float(entry.get("confidence", 1.0)),
    )


def _grid_hz(min_hz: float, max_hz: float, size: int) -> list[float]:
    if size < 2 or max_hz <= min_hz:
        return [min_hz] * max(size, 0)
    step = (max_hz - min_hz) / float(size - 1)
    return [min_hz + step * float(index) for index in range(size)]


def _fitted_envelope_from_spec(spec: dict) -> list[EnvelopeSample]:
    if not spec:
        return []
    kind = spec.get("kind")
    grid = _grid_hz(
        float(spec["grid_min_hz"]),
        float(spec["grid_max_hz"]),
        int(spec["grid_size"]),
    )
    if kind == "constant_db":
        value_db = float(spec["value_db"])
        return [EnvelopeSample(frequency_hz=hz, value_db=value_db) for hz in grid]
    if kind == "log_log_line":
        intercept_db = float(spec["intercept_db"])
        slope = float(spec["slope_db_per_octave"])
        reference_hz = float(spec["reference_hz"])
        samples: list[EnvelopeSample] = []
        for hz in grid:
            if hz <= 0.0:
                samples.append(EnvelopeSample(frequency_hz=hz, value_db=intercept_db))
                continue
            value_db = intercept_db + slope * math.log2(hz / reference_hz)
            samples.append(EnvelopeSample(frequency_hz=hz, value_db=value_db))
        return samples
    if kind == "two_raised_cosine_peaks":
        baseline_db = float(spec["baseline_db"])
        peaks = spec.get("peaks") or []
        samples = []
        for hz in grid:
            value_db = baseline_db
            for peak in peaks:
                center_hz = float(peak["center_hz"])
                bandwidth_hz = max(float(peak["bandwidth_hz"]), 1.0e-6)
                prominence_db = float(peak["prominence_db"])
                offset = hz - center_hz
                if abs(offset) >= bandwidth_hz:
                    continue
                shape = 0.5 * (1.0 + math.cos(math.pi * offset / bandwidth_hz))
                value_db += prominence_db * shape
            samples.append(EnvelopeSample(frequency_hz=hz, value_db=value_db))
        return samples
    raise ValueError(f"unknown fitted_envelope kind: {kind}")


def _waveform_spectrum_from_spec(spec: dict) -> list[WaveformBin]:
    if not spec:
        return []
    kind = spec.get("kind")
    grid = _grid_hz(
        float(spec["grid_min_hz"]),
        float(spec["grid_max_hz"]),
        int(spec["grid_size"]),
    )
    if kind == "synthetic_harmonic_plus_floor":
        fundamental = float(spec["fundamental_frequency_hz"])
        harmonic_count = int(spec["harmonic_count"])
        first_amp_db = float(spec["harmonic_amplitude_db_first"])
        rolloff_db = float(spec["harmonic_rolloff_db_per_doubling"])
        floor_db = float(spec["noise_floor_db"])
        floor_amp = _amplitude_from_db(floor_db)
        harmonic_amps_db: dict[int, float] = {}
        for harmonic_index in range(1, harmonic_count + 1):
            doublings = math.log2(harmonic_index) if harmonic_index > 1 else 0.0
            harmonic_amps_db[harmonic_index] = first_amp_db + rolloff_db * doublings
        spectrum: list[WaveformBin] = []
        for hz in grid:
            amplitude = floor_amp
            if hz > 0.0 and fundamental > 0.0:
                nearest = round(hz / fundamental)
                if 1 <= nearest <= harmonic_count:
                    target_hz = fundamental * float(nearest)
                    if abs(hz - target_hz) <= max(fundamental * 0.1, 1.0):
                        amplitude = max(
                            amplitude,
                            _amplitude_from_db(harmonic_amps_db[nearest]),
                        )
            spectrum.append(WaveformBin(frequency_hz=hz, amplitude=amplitude))
        return spectrum
    raise ValueError(f"unknown waveform_spectrum kind: {kind}")


def _harmonic_partials_stretched(
    fundamental_hz: float,
    stretch_coefficient_b: float,
    harmonic_count: int,
    partial_amplitude: float,
) -> list[Partial]:
    partials: list[Partial] = []
    for harmonic_index in range(1, harmonic_count + 1):
        frequency = (
            fundamental_hz
            * float(harmonic_index)
            * math.sqrt(1.0 + stretch_coefficient_b * float(harmonic_index) ** 2)
        )
        partials.append(
            Partial(frequency_hz=frequency, amplitude=partial_amplitude)
        )
    return partials


def build_frame_fixture(entry: dict) -> Frame:
    category = entry["category"]
    frame = Frame(
        fixture_id=entry["id"],
        category=category,
        fundamental_frequency_hz=float(entry.get("fundamental_frequency_hz", 0.0)),
        f0_confidence=float(entry.get("f0_confidence", 0.0)),
    )

    if category == "stretched_harmonic_frame":
        frame.partials = _harmonic_partials_stretched(
            fundamental_hz=frame.fundamental_frequency_hz,
            stretch_coefficient_b=float(entry["stretch_coefficient_b"]),
            harmonic_count=int(entry["harmonic_count"]),
            partial_amplitude=float(entry["partial_amplitude"]),
        )
    else:
        frame.partials = [_make_partial(p) for p in entry.get("partials", [])]

    frame.residual_bands = [
        _make_residual(b) for b in entry.get("residual_bands", [])
    ]

    fitted_spec = entry.get("fitted_envelope")
    if fitted_spec:
        frame.fitted_envelope = _fitted_envelope_from_spec(fitted_spec)

    waveform_spec = entry.get("waveform_spectrum")
    if waveform_spec:
        frame.waveform_spectrum = _waveform_spectrum_from_spec(waveform_spec)
        frame.waveform_available = True

    return frame


def _series_frames_from_generator(spec: dict) -> list[SeriesFrame]:
    kind = spec.get("kind")
    if kind == "single_partial_am":
        carrier_amp = float(spec["carrier_amplitude"])
        modulator_hz = float(spec["modulator_hz"])
        depth = float(spec["modulator_depth"])
        frame_count = int(spec["frame_count"])
        frequency_hz = float(spec["frequency_hz"])
        # We do not know frame_hop_seconds here without the outer dict; the
        # caller has already validated it, so we accept a callback by writing
        # the frames as a function of a per-frame phase increment derived from
        # the modulator and the surrounding frame hop. To keep this helper
        # decoupled, we recompute the phase via the modulator's period only.
        # The actual hop is applied by build_series_fixture.
        return [
            SeriesFrame(
                partials=[
                    Partial(
                        frequency_hz=frequency_hz,
                        amplitude=carrier_amp,  # placeholder; rewritten below
                    )
                ]
            )
            for _ in range(frame_count)
        ]
    raise ValueError(f"unknown series generator kind: {kind}")


def _apply_am_modulation(
    frames: list[SeriesFrame],
    frame_hop_seconds: float,
    spec: dict,
) -> None:
    kind = spec.get("kind")
    if kind != "single_partial_am":
        return
    carrier_amp = float(spec["carrier_amplitude"])
    modulator_hz = float(spec["modulator_hz"])
    depth = float(spec["modulator_depth"])
    for index, frame in enumerate(frames):
        time_s = float(index) * frame_hop_seconds
        amplitude = carrier_amp * (
            1.0 + depth * math.sin(2.0 * math.pi * modulator_hz * time_s)
        )
        amplitude = max(amplitude, 0.0)
        frame.partials = [
            Partial(
                frequency_hz=frame.partials[0].frequency_hz,
                amplitude=amplitude,
            )
        ]


def _series_frames_from_tracks(
    tracks: Iterable[dict],
    frame_count: int,
) -> list[SeriesFrame]:
    frames = [SeriesFrame() for _ in range(frame_count)]
    for track in tracks:
        start_frame = int(track["start_frame"])
        end_frame = int(track["end_frame"])
        amplitude_curve = list(track["amplitude_curve"])
        frequency = float(track["frequency_hz"])
        for offset, amplitude in enumerate(amplitude_curve):
            frame_index = start_frame + offset
            if frame_index < 0 or frame_index >= frame_count:
                continue
            if frame_index > end_frame:
                continue
            if amplitude <= 0.0:
                continue
            frames[frame_index].partials.append(
                Partial(frequency_hz=frequency, amplitude=float(amplitude))
            )
    return frames


def build_series_fixture(entry: dict) -> Series:
    frame_hop_seconds = float(entry["frame_hop_seconds"])

    if "tracks" in entry:
        tracks = entry["tracks"]
        frame_count = 0
        for track in tracks:
            frame_count = max(frame_count, int(track["end_frame"]) + 1)
        frames = _series_frames_from_tracks(tracks, frame_count)
    elif "generator" in entry:
        generator_spec = entry["generator"]
        frames = _series_frames_from_generator(generator_spec)
        _apply_am_modulation(frames, frame_hop_seconds, generator_spec)
    else:
        raw_frames = entry.get("frames") or []
        frames = []
        for raw in raw_frames:
            frames.append(
                SeriesFrame(
                    partials=[_make_partial(p) for p in raw.get("partials", [])],
                    residual_bands=[
                        _make_residual(b) for b in raw.get("residual_bands", [])
                    ],
                )
            )

    return Series(
        fixture_id=entry["id"],
        category=entry["category"],
        frame_hop_seconds=frame_hop_seconds,
        frames=frames,
    )


def load_frame_fixtures() -> list[Frame]:
    data = _load_fixtures()
    return [build_frame_fixture(entry) for entry in data.get("frame_fixtures", [])]


def load_series_fixtures() -> list[Series]:
    data = _load_fixtures()
    return [
        build_series_fixture(entry) for entry in data.get("series_fixtures", [])
    ]


def load_tolerances() -> dict:
    data = _load_fixtures()
    return data.get("tolerances") or {}


def _self_test() -> int:
    failures: list[str] = []

    frames = load_frame_fixtures()
    if not frames:
        failures.append("no frame fixtures loaded")

    seen_ids: set[str] = set()
    for frame in frames:
        if frame.fixture_id in seen_ids:
            failures.append(f"duplicate fixture id: {frame.fixture_id}")
        seen_ids.add(frame.fixture_id)
        # determinism: build it twice and check every field
        data = _load_fixtures()
        original = next(
            entry
            for entry in data["frame_fixtures"]
            if entry["id"] == frame.fixture_id
        )
        rebuilt = build_frame_fixture(original)
        if rebuilt.partials != frame.partials:
            failures.append(
                f"frame {frame.fixture_id}: partials are not deterministic"
            )
        if rebuilt.residual_bands != frame.residual_bands:
            failures.append(
                f"frame {frame.fixture_id}: residual_bands are not deterministic"
            )
        if rebuilt.fitted_envelope != frame.fitted_envelope:
            failures.append(
                f"frame {frame.fixture_id}: fitted_envelope is not deterministic"
            )
        if rebuilt.waveform_spectrum != frame.waveform_spectrum:
            failures.append(
                f"frame {frame.fixture_id}: waveform_spectrum is not deterministic"
            )

    # Spot-check a few analytical expectations
    two_partial = next(
        f for f in frames if f.fixture_id == "two_equal_partials_1k_3k"
    )
    centroid = sum(p.frequency_hz * p.amplitude * p.amplitude for p in two_partial.partials)
    total_energy = sum(p.amplitude * p.amplitude for p in two_partial.partials)
    if total_energy <= 0.0 or abs(centroid / total_energy - 2000.0) > 1.0e-9:
        failures.append("two_equal_partials_1k_3k centroid is not 2000 Hz")

    split = next(
        f for f in frames if f.fixture_id == "partial_residual_split_75_25"
    )
    partial_energy = sum(p.amplitude * p.amplitude for p in split.partials)
    residual_energy = sum(b.energy for b in split.residual_bands)
    if abs(partial_energy - 0.75) > 1.0e-9:
        failures.append(
            f"partial_residual_split_75_25 partial energy is {partial_energy}, expected 0.75"
        )
    if abs(residual_energy - 0.25) > 1.0e-9:
        failures.append(
            f"partial_residual_split_75_25 residual energy is {residual_energy}, expected 0.25"
        )

    orphan_fixture = next(
        f
        for f in frames
        if f.fixture_id == "partial_residual_split_with_orphan"
    )
    orphan_energy = sum(
        b.energy for b in orphan_fixture.residual_bands if b.orphan
    )
    total_residual = sum(b.energy for b in orphan_fixture.residual_bands)
    if total_residual <= 0.0 or abs(orphan_energy / total_residual - 0.5) > 1.0e-9:
        failures.append("orphan_residual_ratio fixture is not exactly 0.5")

    series = load_series_fixtures()
    if not series:
        failures.append("no series fixtures loaded")
    am_series = next(
        s for s in series if s.fixture_id == "modulation_series_5hz_tremolo"
    )
    if len(am_series.frames) != 32:
        failures.append(
            f"modulation_series_5hz_tremolo expected 32 frames, got {len(am_series.frames)}"
        )
    # The carrier with depth 0.5 has min 0.5 and max 1.5 over time.
    amplitudes = [
        frame.partials[0].amplitude for frame in am_series.frames if frame.partials
    ]
    if amplitudes:
        if min(amplitudes) < 0.0:
            failures.append("AM amplitude must remain non-negative")
        if max(amplitudes) > 1.6:
            failures.append(
                f"AM peak amplitude {max(amplitudes)} exceeds expected 1.5"
            )

    tolerances = load_tolerances()
    if "absolute" not in tolerances or "relative" not in tolerances:
        failures.append("tolerances must include absolute and relative groups")

    if failures:
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(
        f"timbre descriptor analytical fixtures ok: {len(frames)} frames, "
        f"{len(series)} series"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(_self_test())

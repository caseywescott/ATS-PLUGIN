"""
Pure-Python reader for the ATS binary file format.

Binary layout (all float64, native-endian; byte-swap auto-detected via magic):
  Header (10 × f64 = 80 bytes):
    mag  – magic number (must be 123.0)
    sr   – sample rate
    fs   – frame size (samples)
    ws   – window size (samples)
    par  – number of partials per frame
    fra  – number of frames
    ma   – max amplitude
    mf   – max frequency (Hz)
    dur  – duration (seconds)
    typ  – file type (1-4)

  Per-frame loop (fra iterations):
    time:  1 × f64
    per partial (par iterations):
      amp:  1 × f64
      freq: 1 × f64
      pha:  1 × f64   (only if FILE_HAS_PHASE: type 2 or 4)
    per Bark band (25 iterations):
      band_energy: 1 × f64  (only if FILE_HAS_NOISE: type 3 or 4)

FILE_HAS_PHASE: type in {2, 4}
FILE_HAS_NOISE: type in {3, 4}
"""

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

ATS_MAGIC = 123.0
CRITICAL_BANDS = 25


def _has_phase(typ: int) -> bool:
    return typ in (2, 4)


def _has_noise(typ: int) -> bool:
    return typ in (3, 4)


@dataclass
class AtsHeader:
    mag: float
    sr: float
    frame_size: int
    window_size: int
    partials: int
    frames: int
    max_amp: float
    max_freq: float
    duration: float
    type: int

    def has_phase(self) -> bool:
        return _has_phase(self.type)

    def has_noise(self) -> bool:
        return _has_noise(self.type)

    def type_description(self) -> str:
        desc = "amp+freq"
        if self.has_phase():
            desc += "+phase"
        if self.has_noise():
            desc += "+noise"
        return desc


@dataclass
class AtsSummary:
    header: AtsHeader
    file_path: str
    file_size_bytes: int
    partial_mean_freqs: list       # mean Hz per partial (0 if never active)
    partial_mean_amps: list        # mean linear amplitude per partial
    partial_active_frames: list    # frames where amp > 0, per partial
    mean_band_energies: Optional[list]  # 25 Bark-band mean energies, or None

    def to_dict(self) -> dict:
        h = self.header
        n = h.partials
        active_idx = [i for i in range(n) if self.partial_active_frames[i] > 0]
        active_freqs = [self.partial_mean_freqs[i] for i in active_idx]
        return {
            "file_path": self.file_path,
            "file_size_bytes": self.file_size_bytes,
            "type": h.type,
            "type_description": h.type_description(),
            "sample_rate": int(h.sr),
            "frame_size": h.frame_size,
            "window_size": h.window_size,
            "partials": n,
            "active_partials": len(active_idx),
            "frames": h.frames,
            "duration_s": round(h.duration, 6),
            "max_amp": h.max_amp,
            "max_freq_hz": h.max_freq,
            "freq_range_hz": (
                [round(min(active_freqs), 2), round(max(active_freqs), 2)]
                if active_freqs else [0.0, 0.0]
            ),
            "has_phase": h.has_phase(),
            "has_noise": h.has_noise(),
            "mean_band_energies": self.mean_band_energies,
        }


def _theoretical_file_size(n_par: int, n_fra: int, has_phase: bool, has_noise: bool) -> int:
    """Return the expected binary file size for the given model dimensions."""
    vals_per_partial = 3 if has_phase else 2  # amp + freq [+ pha]
    return (
        80  # header: 10 × f64
        + n_fra * (
            8  # time
            + n_par * vals_per_partial * 8
            + (CRITICAL_BANDS * 8 if has_noise else 0)
        )
    )


def load_ats_frames(path: str, frame_start: int = 0, frame_end: int = None) -> list:
    """Return per-frame (time_s, amps[], freqs[]) for descriptor computation.

    frame_end is exclusive; None means all frames.
    Only returns amp and freq — phase is skipped to keep this lightweight.
    """
    p = Path(path)
    data = p.read_bytes()

    raw_le = struct.unpack_from("<10d", data, 0)
    if abs(raw_le[0] - ATS_MAGIC) < 0.5:
        endian = "<"
        raw = raw_le
    else:
        raw_be = struct.unpack_from(">10d", data, 0)
        if abs(raw_be[0] - ATS_MAGIC) < 0.5:
            endian = ">"
            raw = raw_be
        else:
            raise ValueError(f"ATS magic number not found in '{path}'")

    fmt_d = endian + "d"
    n_par = int(raw[4])
    n_fra = int(raw[5])
    has_pha = _has_phase(int(raw[9]))
    has_nse = _has_noise(int(raw[9]))

    frame_end = n_fra if frame_end is None else min(frame_end, n_fra)
    frame_start = max(0, frame_start)

    # Byte size of one frame so we can skip to frame_start
    frame_bytes = 8 + n_par * ((3 if has_pha else 2) * 8) + (CRITICAL_BANDS * 8 if has_nse else 0)

    offset = 80 + frame_start * frame_bytes
    frames = []
    for _ in range(frame_start, frame_end):
        time_s = struct.unpack_from(fmt_d, data, offset)[0]
        offset += 8
        amps = []
        freqs = []
        for _ in range(n_par):
            amp = struct.unpack_from(fmt_d, data, offset)[0]; offset += 8
            frq = struct.unpack_from(fmt_d, data, offset)[0]; offset += 8
            if has_pha:
                offset += 8
            amps.append(amp)
            freqs.append(frq)
        if has_nse:
            offset += CRITICAL_BANDS * 8
        frames.append({"time_s": time_s, "amps": amps, "freqs": freqs})
    return frames


def load_ats_frames_full(path: str) -> tuple:
    """Return (AtsHeader, frames) with all per-frame data needed for write-back.

    Each frame dict contains:
        time_s        float
        amps          list[float]   — one per partial
        freqs         list[float]   — one per partial
        phases        list[float] | None   — only if has_phase
        band_energies list[float] | None   — 25 values, only if has_noise
    """
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"ATS file not found: '{path}'")

    data = p.read_bytes()

    raw_le = struct.unpack_from("<10d", data, 0)
    if abs(raw_le[0] - ATS_MAGIC) < 0.5:
        endian = "<"
        raw = raw_le
    else:
        raw_be = struct.unpack_from(">10d", data, 0)
        if abs(raw_be[0] - ATS_MAGIC) < 0.5:
            endian = ">"
            raw = raw_be
        else:
            raise ValueError(f"ATS magic number not found in '{path}'")

    header = AtsHeader(
        mag=raw[0], sr=raw[1], frame_size=int(raw[2]), window_size=int(raw[3]),
        partials=int(raw[4]), frames=int(raw[5]),
        max_amp=raw[6], max_freq=raw[7], duration=raw[8], type=int(raw[9]),
    )

    fmt_d = endian + "d"
    n_par = header.partials
    n_fra = header.frames
    has_pha = header.has_phase()
    has_nse = header.has_noise()

    offset = 80
    frames = []
    for _ in range(n_fra):
        time_s = struct.unpack_from(fmt_d, data, offset)[0]; offset += 8
        amps, freqs, phases = [], [], [] if has_pha else None
        for _ in range(n_par):
            amps.append(struct.unpack_from(fmt_d, data, offset)[0]); offset += 8
            freqs.append(struct.unpack_from(fmt_d, data, offset)[0]); offset += 8
            if has_pha:
                phases.append(struct.unpack_from(fmt_d, data, offset)[0]); offset += 8
        band_energies = None
        if has_nse:
            band_energies = list(struct.unpack_from(endian + f"{CRITICAL_BANDS}d", data, offset))
            offset += CRITICAL_BANDS * 8
        frames.append({
            "time_s": time_s,
            "amps": amps,
            "freqs": freqs,
            "phases": phases,
            "band_energies": band_energies,
        })
    return header, frames


def write_ats(header: AtsHeader, frames: list, output_path: str) -> None:
    """Write a modified ATS model back to disk (always little-endian).

    Recomputes max_amp and max_freq from the frame data before writing.
    All other header fields (sr, frame_size, window_size, partials, frames,
    duration, type) are taken from header unchanged.
    """
    # Recompute max_amp and max_freq from the actual frame data
    new_max_amp = 0.0
    new_max_freq = 0.0
    for frm in frames:
        for a in frm["amps"]:
            if a > new_max_amp:
                new_max_amp = a
        for f in frm["freqs"]:
            if f > new_max_freq:
                new_max_freq = f

    buf = bytearray()

    # Header (10 × f64, little-endian)
    buf += struct.pack(
        "<10d",
        header.mag,
        header.sr,
        float(header.frame_size),
        float(header.window_size),
        float(header.partials),
        float(header.frames),
        new_max_amp,
        new_max_freq,
        header.duration,
        float(header.type),
    )

    has_pha = header.has_phase()
    has_nse = header.has_noise()

    for frm in frames:
        buf += struct.pack("<d", frm["time_s"])
        for i in range(header.partials):
            buf += struct.pack("<d", frm["amps"][i])
            buf += struct.pack("<d", frm["freqs"][i])
            if has_pha:
                buf += struct.pack("<d", frm["phases"][i])
        if has_nse:
            buf += struct.pack(f"<{CRITICAL_BANDS}d", *frm["band_energies"])

    Path(output_path).write_bytes(bytes(buf))


def load_ats(path: str) -> AtsSummary:
    """Parse an ATS binary file and return a summary with per-partial statistics."""
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"ATS file not found: '{path}'")

    data = p.read_bytes()
    file_size = len(data)

    if file_size < 80:
        raise ValueError(f"File too small to be a valid ATS file: {file_size} bytes")

    # Auto-detect endianness via magic number
    raw_le = struct.unpack_from("<10d", data, 0)
    if abs(raw_le[0] - ATS_MAGIC) < 0.5:
        endian = "<"
        raw = raw_le
    else:
        raw_be = struct.unpack_from(">10d", data, 0)
        if abs(raw_be[0] - ATS_MAGIC) < 0.5:
            endian = ">"
            raw = raw_be
        else:
            raise ValueError(
                f"ATS magic number not found in '{path}' "
                f"(got {raw_le[0]:.3f} LE / {raw_be[0]:.3f} BE, expected {ATS_MAGIC})"
            )

    fmt_d = endian + "d"

    header = AtsHeader(
        mag=raw[0],
        sr=raw[1],
        frame_size=int(raw[2]),
        window_size=int(raw[3]),
        partials=int(raw[4]),
        frames=int(raw[5]),
        max_amp=raw[6],
        max_freq=raw[7],
        duration=raw[8],
        type=int(raw[9]),
    )

    n_par = header.partials
    n_fra = header.frames
    has_pha = header.has_phase()
    has_nse = header.has_noise()

    sum_freqs = [0.0] * n_par
    sum_amps = [0.0] * n_par
    active_cnt = [0] * n_par
    sum_bands = [0.0] * CRITICAL_BANDS if has_nse else None

    offset = 80  # past 10-double header
    for _ in range(n_fra):
        offset += 8  # time value
        for p_idx in range(n_par):
            amp = struct.unpack_from(fmt_d, data, offset)[0]
            offset += 8
            frq = struct.unpack_from(fmt_d, data, offset)[0]
            offset += 8
            if has_pha:
                offset += 8  # skip phase
            if amp > 0.0:
                sum_amps[p_idx] += amp
                sum_freqs[p_idx] += frq
                active_cnt[p_idx] += 1
        if has_nse:
            for b in range(CRITICAL_BANDS):
                sum_bands[b] += struct.unpack_from(fmt_d, data, offset)[0]
                offset += 8

    mean_freqs = [
        sum_freqs[i] / active_cnt[i] if active_cnt[i] > 0 else 0.0
        for i in range(n_par)
    ]
    mean_amps = [
        sum_amps[i] / active_cnt[i] if active_cnt[i] > 0 else 0.0
        for i in range(n_par)
    ]
    mean_bands = (
        [round(sum_bands[b] / max(n_fra, 1), 8) for b in range(CRITICAL_BANDS)]
        if has_nse else None
    )

    return AtsSummary(
        header=header,
        file_path=str(p.resolve()),
        file_size_bytes=file_size,
        partial_mean_freqs=mean_freqs,
        partial_mean_amps=mean_amps,
        partial_active_frames=active_cnt,
        mean_band_energies=mean_bands,
    )

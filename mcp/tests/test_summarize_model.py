"""
Tests for summarize_model and compare_models (pure Python, no CLI required).

Fixtures: uses crotales.ats from the repo root, which is a real type-4 ATS file.
"""

import sys
from pathlib import Path

import pytest

# Allow importing from mcp/
sys.path.insert(0, str(Path(__file__).parent.parent))

from ats_parser import load_ats, ATS_MAGIC, CRITICAL_BANDS

CROTALES = Path(__file__).parent.parent.parent / "crotales.ats"


@pytest.fixture
def crotales_summary():
    if not CROTALES.exists():
        pytest.skip(f"Fixture not found: {CROTALES}")
    return load_ats(str(CROTALES))


# ---------------------------------------------------------------------------
# Header parsing
# ---------------------------------------------------------------------------

def test_magic_number(crotales_summary):
    assert abs(crotales_summary.header.mag - ATS_MAGIC) < 0.5


def test_header_sample_rate(crotales_summary):
    assert crotales_summary.header.sr > 0


def test_header_partials_positive(crotales_summary):
    assert crotales_summary.header.partials > 0


def test_header_frames_positive(crotales_summary):
    assert crotales_summary.header.frames > 0


def test_header_duration_positive(crotales_summary):
    assert crotales_summary.header.duration > 0.0


def test_header_type_valid(crotales_summary):
    assert crotales_summary.header.type in (1, 2, 3, 4)


# ---------------------------------------------------------------------------
# Type-4 specific (crotales.ats is type 4 = amp+freq+phase+noise)
# ---------------------------------------------------------------------------

def test_type4_has_phase_and_noise(crotales_summary):
    h = crotales_summary.header
    assert h.has_phase(), "crotales.ats should have phase (type 4)"
    assert h.has_noise(), "crotales.ats should have noise (type 4)"


def test_type4_band_energies_present(crotales_summary):
    assert crotales_summary.mean_band_energies is not None
    assert len(crotales_summary.mean_band_energies) == CRITICAL_BANDS


def test_type4_band_energies_non_negative(crotales_summary):
    assert all(e >= 0.0 for e in crotales_summary.mean_band_energies)


# ---------------------------------------------------------------------------
# Partial statistics
# ---------------------------------------------------------------------------

def test_partial_lists_length(crotales_summary):
    n = crotales_summary.header.partials
    assert len(crotales_summary.partial_mean_freqs) == n
    assert len(crotales_summary.partial_mean_amps) == n
    assert len(crotales_summary.partial_active_frames) == n


def test_active_partials_have_positive_freq(crotales_summary):
    for i, cnt in enumerate(crotales_summary.partial_active_frames):
        if cnt > 0:
            assert crotales_summary.partial_mean_freqs[i] > 0.0


def test_active_partials_have_positive_amp(crotales_summary):
    for i, cnt in enumerate(crotales_summary.partial_active_frames):
        if cnt > 0:
            assert crotales_summary.partial_mean_amps[i] > 0.0


# ---------------------------------------------------------------------------
# to_dict round-trip
# ---------------------------------------------------------------------------

def test_to_dict_keys(crotales_summary):
    d = crotales_summary.to_dict()
    required = {
        "file_path", "file_size_bytes", "type", "type_description",
        "sample_rate", "frame_size", "window_size", "partials",
        "active_partials", "frames", "duration_s", "max_amp",
        "max_freq_hz", "freq_range_hz", "has_phase", "has_noise",
        "mean_band_energies",
    }
    assert required.issubset(d.keys())


def test_to_dict_freq_range_ordered(crotales_summary):
    d = crotales_summary.to_dict()
    lo, hi = d["freq_range_hz"]
    assert lo <= hi


def test_to_dict_active_leq_total(crotales_summary):
    d = crotales_summary.to_dict()
    assert d["active_partials"] <= d["partials"]


def test_file_size_matches_disk(crotales_summary):
    assert crotales_summary.file_size_bytes == CROTALES.stat().st_size


# ---------------------------------------------------------------------------
# File error handling
# ---------------------------------------------------------------------------

def test_missing_file_raises():
    with pytest.raises(FileNotFoundError):
        load_ats("/nonexistent/path/file.ats")


def test_bad_magic_raises(tmp_path):
    bad = tmp_path / "bad.ats"
    import struct
    # Write 10 doubles with wrong magic
    bad.write_bytes(struct.pack("<10d", 0.0, 44100.0, 512.0, 1024.0,
                                10.0, 100.0, 1.0, 4000.0, 1.0, 1.0))
    with pytest.raises(ValueError, match="magic"):
        load_ats(str(bad))

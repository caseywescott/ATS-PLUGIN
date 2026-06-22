"""
Tests for all pure-Python MCP tools (no CLI build required).

Covers: validate_model, measure_descriptors, check_build, list_fixtures,
        create_recipe, validate_recipe, estimate_analysis_time.
"""

import math
import struct
import sys
import wave
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from ats_parser import CRITICAL_BANDS, load_ats
from server import (
    check_build,
    create_recipe,
    estimate_analysis_time,
    list_fixtures,
    measure_descriptors,
    validate_model,
    validate_recipe,
)

REPO = Path(__file__).parent.parent.parent
CROTALES = REPO / "crotales.ats"


@pytest.fixture
def crotales(tmp_path):
    if not CROTALES.exists():
        pytest.skip(f"Fixture not found: {CROTALES}")
    return str(CROTALES)


# =============================================================================
# validate_model
# =============================================================================

def test_validate_model_valid(crotales):
    r = validate_model(crotales)
    assert r["valid"] is True
    assert r["errors"] == []


def test_validate_model_missing_file():
    r = validate_model("/nonexistent/path/file.ats")
    assert r["valid"] is False
    assert any("not found" in e.lower() for e in r["errors"])


def test_validate_model_bad_magic(tmp_path):
    bad = tmp_path / "bad.ats"
    bad.write_bytes(struct.pack("<10d", 0.0, 44100.0, 512.0, 1024.0,
                                10.0, 100.0, 1.0, 4000.0, 1.0, 1.0))
    r = validate_model(str(bad))
    assert r["valid"] is False


def test_validate_model_truncated(tmp_path, crotales):
    data = Path(crotales).read_bytes()
    bad = tmp_path / "truncated.ats"
    bad.write_bytes(data[:-8])  # truncate one double
    r = validate_model(str(bad))
    assert r["valid"] is False
    assert any("size" in e.lower() for e in r["errors"])


# =============================================================================
# measure_descriptors
# =============================================================================

def test_measure_descriptors_centroid_positive(crotales):
    r = measure_descriptors(crotales)
    assert r["centroid_hz"] > 0.0


def test_measure_descriptors_brightness_in_range(crotales):
    r = measure_descriptors(crotales)
    assert 0.0 <= r["brightness"] <= 1.0


def test_measure_descriptors_tilt_finite(crotales):
    r = measure_descriptors(crotales)
    assert math.isfinite(r["tilt_db_oct"])


def test_measure_descriptors_rms_envelope_length(crotales):
    s = load_ats(crotales)
    r = measure_descriptors(crotales)
    # rms_envelope has one entry per frame (0.0 for silent frames)
    assert len(r["rms_envelope"]) == s.header.frames


def test_measure_descriptors_rms_non_negative(crotales):
    r = measure_descriptors(crotales)
    assert all(v >= 0.0 for v in r["rms_envelope"])


def test_measure_descriptors_stride_reduces_frames(crotales):
    s = load_ats(crotales)
    r_full = measure_descriptors(crotales, frame_stride=1)
    r_half = measure_descriptors(crotales, frame_stride=2)
    assert r_half["frames_analyzed"] <= r_full["frames_analyzed"]


# =============================================================================
# check_build
# =============================================================================

def test_check_build_returns_dict():
    r = check_build()
    assert "tools" in r
    assert "built" in r
    assert "total" in r


def test_check_build_tool_entries_have_expected_keys():
    r = check_build()
    for name, info in r["tools"].items():
        assert "found" in info
        assert "path" in info
        assert "help_ok" in info


def test_check_build_analyze_and_synth_found():
    r = check_build()
    # These two were built in v0 and should always be present
    assert r["tools"].get("ats_analyze", {}).get("found") is True
    assert r["tools"].get("ats_synth", {}).get("found") is True


# =============================================================================
# list_fixtures
# =============================================================================

def test_list_fixtures_returns_dict():
    r = list_fixtures()
    assert "available" in r


def test_list_fixtures_manifest_path_present():
    r = list_fixtures()
    assert "manifest_path" in r


def test_list_fixtures_content_when_available():
    r = list_fixtures()
    if r["available"]:
        assert "manifest" in r
        assert isinstance(r["manifest"], dict)


# =============================================================================
# create_recipe
# =============================================================================

def test_create_recipe_produces_text(crotales):
    r = create_recipe(crotales, crotales)
    assert "recipe_text" in r
    assert len(r["recipe_text"]) > 0


def test_create_recipe_has_required_keys(crotales):
    r = create_recipe(crotales, crotales)
    text = r["recipe_text"]
    for field in ("engine_version", "schema_version", "source_a.id", "source_b.id"):
        assert field + "=" in text, f"Missing field {field!r} in recipe_text"


def test_create_recipe_kv_parseable(crotales):
    r = create_recipe(crotales, crotales)
    kv = {}
    for line in r["recipe_text"].splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            kv[k] = v
    assert kv.get("schema_version") == "1"
    assert kv.get("engine_version") == "morph-recipe-v1"


def test_create_recipe_missing_file_raises():
    with pytest.raises(FileNotFoundError):
        create_recipe("/nonexistent/a.ats", "/nonexistent/b.ats")


# =============================================================================
# validate_recipe
# =============================================================================

def test_validate_recipe_valid_recipe(crotales):
    recipe = create_recipe(crotales, crotales)
    r = validate_recipe(recipe["recipe_text"])
    assert r["valid"] is True
    assert r["errors"] == []


def test_validate_recipe_empty_fails():
    r = validate_recipe("")
    assert r["valid"] is False
    assert len(r["errors"]) > 0


def test_validate_recipe_missing_required_field(crotales):
    recipe = create_recipe(crotales, crotales)
    # Remove engine_version line
    text = "\n".join(
        line for line in recipe["recipe_text"].splitlines()
        if not line.startswith("engine_version=")
    )
    r = validate_recipe(text)
    assert r["valid"] is False
    assert any("engine_version" in e for e in r["errors"])


def test_validate_recipe_parsed_fields_count(crotales):
    recipe = create_recipe(crotales, crotales)
    r = validate_recipe(recipe["recipe_text"])
    assert r["parsed_fields"] > 10


# =============================================================================
# estimate_analysis_time
# =============================================================================

@pytest.fixture
def short_wav(tmp_path):
    """Create a 1-second mono PCM-16 WAV at 44100 Hz."""
    p = tmp_path / "sine.wav"
    n = 44100
    with wave.open(str(p), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(44100)
        wf.writeframes(b"\x00\x00" * n)
    return str(p)


def test_estimate_analysis_time_positive(short_wav):
    r = estimate_analysis_time(short_wav)
    assert r["estimated_seconds"] > 0.0


def test_estimate_analysis_time_wav_duration(short_wav):
    r = estimate_analysis_time(short_wav)
    assert abs(r["wav_duration_s"] - 1.0) < 0.05


def test_estimate_analysis_time_multi_res_slower(short_wav):
    normal = estimate_analysis_time(short_wav, multi_res=False)
    multi  = estimate_analysis_time(short_wav, multi_res=True)
    assert multi["estimated_seconds"] > normal["estimated_seconds"]


def test_estimate_analysis_time_type4_slower_than_type1(short_wav):
    t1 = estimate_analysis_time(short_wav, analysis_type=1)
    t4 = estimate_analysis_time(short_wav, analysis_type=4)
    assert t4["estimated_seconds"] > t1["estimated_seconds"]


def test_estimate_analysis_time_missing_file():
    with pytest.raises(FileNotFoundError):
        estimate_analysis_time("/nonexistent/file.wav")

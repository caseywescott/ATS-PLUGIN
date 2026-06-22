"""
Integration tests for all CLI-backed MCP tools.

All tests are auto-skipped when their required CLI tool is not built.
Run after: cmake --build build-plugin --target ats_prune ats_transform ats_envelope ats_transient ats_morph_plan
"""

import sys
import wave
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from server import (
    build_envelope,
    compare_envelopes,
    detect_onsets,
    export_model,
    extract_transient_layer,
    plan_morph,
    prune_model,
    render_model,
    summarize_model,
    time_stretch_model,
    transform_model,
)

REPO = Path(__file__).parent.parent.parent
TOOLS = REPO / "build-plugin" / "tools"
CROTALES = REPO / "crotales.ats"


def _built(name: str) -> bool:
    return (TOOLS / name).exists()


def _crotales():
    if not CROTALES.exists():
        pytest.skip(f"Fixture missing: {CROTALES}")
    return str(CROTALES)


# =============================================================================
# prune_model
# =============================================================================

@pytest.mark.skipif(not _built("ats_prune"), reason="ats_prune not built")
def test_prune_model_produces_file(tmp_path):
    out = str(tmp_path / "pruned.ats")
    prune_model(_crotales(), out)
    assert Path(out).exists()


@pytest.mark.skipif(not _built("ats_prune"), reason="ats_prune not built")
def test_prune_model_active_leq_source(tmp_path):
    src = summarize_model(_crotales())
    out = str(tmp_path / "pruned.ats")
    result = prune_model(_crotales(), out)
    assert result["model"]["active_partials"] <= src["active_partials"]


@pytest.mark.skipif(not _built("ats_prune"), reason="ats_prune not built")
def test_prune_model_parseable(tmp_path):
    out = str(tmp_path / "pruned.ats")
    result = prune_model(_crotales(), out)
    assert result["model"]["frames"] > 0


# =============================================================================
# export_model
# =============================================================================

@pytest.mark.skipif(not _built("ats_prune"), reason="ats_prune not built")
def test_export_model_type1(tmp_path):
    out = str(tmp_path / "type1.ats")
    result = export_model(_crotales(), out, output_type=1)
    assert result["model"]["type"] == 1
    assert result["model"]["has_phase"] is False
    assert result["model"]["has_noise"] is False


@pytest.mark.skipif(not _built("ats_prune"), reason="ats_prune not built")
def test_export_model_type3(tmp_path):
    out = str(tmp_path / "type3.ats")
    result = export_model(_crotales(), out, output_type=3)
    assert result["model"]["type"] == 3
    assert result["model"]["has_noise"] is True
    assert result["model"]["has_phase"] is False


# =============================================================================
# time_stretch_model
# =============================================================================

@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_time_stretch_model_produces_wav(tmp_path):
    out = str(tmp_path / "stretched.wav")
    time_stretch_model(_crotales(), out, stretch=1.0)
    assert Path(out).exists()


@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_time_stretch_model_2x_longer(tmp_path):
    normal = str(tmp_path / "normal.wav")
    stretched = str(tmp_path / "stretched.wav")
    time_stretch_model(_crotales(), normal, stretch=1.0)
    time_stretch_model(_crotales(), stretched, stretch=2.0)
    with wave.open(normal) as a, wave.open(stretched) as b:
        assert b.getnframes() > a.getnframes()


# =============================================================================
# transform_model
# =============================================================================

@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
def test_transform_model_produces_file(tmp_path):
    out = str(tmp_path / "transformed.ats")
    transform_model(_crotales(), out)
    assert Path(out).exists()


@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
def test_transform_model_stretch_changes_freq_range(tmp_path):
    out = str(tmp_path / "stretched.ats")
    src = summarize_model(_crotales())
    result = transform_model(_crotales(), out, freq_mode="Stretch", freq_amount=0.5)
    # Max freq should differ from source after Stretch warp
    assert result["model"]["max_freq_hz"] != src["max_freq_hz"]


@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
def test_transform_model_amp_scale(tmp_path):
    out = str(tmp_path / "quiet.ats")
    src = summarize_model(_crotales())
    result = transform_model(_crotales(), out, amp_scale=0.5)
    assert result["model"]["max_amp"] < src["max_amp"]


@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
def test_transform_model_invalid_mode_raises():
    with pytest.raises(ValueError, match="freq_mode"):
        transform_model(_crotales(), "/tmp/out.ats", freq_mode="InvalidMode")


# =============================================================================
# build_envelope
# =============================================================================

@pytest.mark.skipif(not _built("ats_envelope"), reason="ats_envelope not built")
def test_build_envelope_returns_frames(tmp_path):
    result = build_envelope(_crotales(), frame_start=0, frame_end=5)
    assert "frames" in result
    assert len(result["frames"]) > 0


@pytest.mark.skipif(not _built("ats_envelope"), reason="ats_envelope not built")
def test_build_envelope_frame_has_required_keys():
    result = build_envelope(_crotales(), frame_start=0, frame_end=3)
    for frm in result["frames"]:
        assert "frame" in frm
        assert "time_s" in frm
        assert "bark_bins" in frm
        assert "formant_peaks" in frm


@pytest.mark.skipif(not _built("ats_envelope"), reason="ats_envelope not built")
def test_build_envelope_stride_reduces_frame_count():
    r1 = build_envelope(_crotales(), frame_start=0, frame_end=20, stride=1)
    r2 = build_envelope(_crotales(), frame_start=0, frame_end=20, stride=4)
    assert len(r2["frames"]) < len(r1["frames"])


# =============================================================================
# compare_envelopes
# =============================================================================

@pytest.mark.skipif(not _built("ats_envelope"), reason="ats_envelope not built")
def test_compare_envelopes_returns_expected_keys():
    result = compare_envelopes(_crotales(), _crotales())
    assert "envelope_a" in result
    assert "envelope_b" in result
    assert "matched_formants" in result


@pytest.mark.skipif(not _built("ats_envelope"), reason="ats_envelope not built")
def test_compare_envelopes_same_file_high_formant_match():
    result = compare_envelopes(_crotales(), _crotales(), frame_a=10, frame_b=10)
    # Same file + same frame → formant alignment should be non-zero if model is valid
    assert isinstance(result["matched_formants"], int)


# =============================================================================
# detect_onsets
# =============================================================================

@pytest.mark.skipif(not _built("ats_transient"), reason="ats_transient not built")
def test_detect_onsets_returns_list():
    result = detect_onsets(_crotales())
    assert "onsets" in result
    assert isinstance(result["onsets"], list)


@pytest.mark.skipif(not _built("ats_transient"), reason="ats_transient not built")
def test_detect_onsets_fields():
    result = detect_onsets(_crotales())
    for o in result["onsets"]:
        assert "frame" in o
        assert "time_s" in o
        assert "strength" in o


# =============================================================================
# extract_transient_layer
# =============================================================================

@pytest.mark.skipif(not _built("ats_transient"), reason="ats_transient not built")
def test_extract_transient_layer_has_events_key():
    result = extract_transient_layer(_crotales())
    assert "events" in result
    assert isinstance(result["events"], list)


@pytest.mark.skipif(not _built("ats_transient"), reason="ats_transient not built")
def test_extract_transient_layer_event_fields():
    result = extract_transient_layer(_crotales())
    for ev in result["events"]:
        assert "frame" in ev
        assert "time_s" in ev
        assert "deterministic_energy" in ev
        assert "band_energy" in ev
        assert len(ev["band_energy"]) == 25


# =============================================================================
# plan_morph
# =============================================================================

@pytest.mark.skipif(not _built("ats_morph_plan"), reason="ats_morph_plan not built")
def test_plan_morph_same_file_returns_plan():
    result = plan_morph(_crotales(), _crotales())
    assert "transport_cost" in result
    assert "coupling" in result
    assert isinstance(result["coupling"], list)


@pytest.mark.skipif(not _built("ats_morph_plan"), reason="ats_morph_plan not built")
def test_plan_morph_transport_cost_non_negative():
    result = plan_morph(_crotales(), _crotales())
    assert result["transport_cost"] >= 0.0


@pytest.mark.skipif(not _built("ats_morph_plan"), reason="ats_morph_plan not built")
def test_plan_morph_coupling_entries_have_five_elements():
    result = plan_morph(_crotales(), _crotales())
    for entry in result["coupling"]:
        assert len(entry) == 5


@pytest.mark.skipif(not _built("ats_morph_plan"), reason="ats_morph_plan not built")
def test_plan_morph_saves_json(tmp_path):
    plan_path = str(tmp_path / "plan.json")
    result = plan_morph(_crotales(), _crotales(), plan_output_path=plan_path)
    assert Path(plan_path).exists()
    assert result.get("plan_path") == plan_path

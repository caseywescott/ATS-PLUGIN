"""
Tests for Smearnov onset tools:
  detect_onsets (full param surface), smear_attacks, suppress_onsets.
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from ats_parser import load_ats_frames_full
from server import detect_onsets, smear_attacks, suppress_onsets

REPO = Path(__file__).parent.parent.parent
TOOLS = REPO / "build-plugin" / "tools"
CROTALES = REPO / "crotales.ats"


def _crotales():
    if not CROTALES.exists():
        pytest.skip(f"Fixture missing: {CROTALES}")
    return str(CROTALES)


def _built(name):
    return (TOOLS / name).exists()


_skip_transient = pytest.mark.skipif(
    not _built("ats_transient"), reason="ats_transient not built"
)

# =============================================================================
# detect_onsets — return shape
# =============================================================================

@_skip_transient
def test_detect_onsets_returns_list():
    r = detect_onsets(_crotales())
    assert "onsets" in r
    assert isinstance(r["onsets"], list)
    assert len(r["onsets"]) > 0


@_skip_transient
def test_detect_onsets_onset_keys():
    r = detect_onsets(_crotales())
    for o in r["onsets"]:
        assert "frame" in o
        assert "pre_frame" in o
        assert "peak_frame" in o
        assert "time_s" in o
        assert "strength" in o


@_skip_transient
def test_detect_onsets_pre_le_frame_le_peak():
    r = detect_onsets(_crotales())
    for o in r["onsets"]:
        assert o["pre_frame"] <= o["frame"]
        assert o["frame"] <= o["peak_frame"]


# =============================================================================
# detect_onsets — tunable params
# =============================================================================

@_skip_transient
def test_detect_onsets_tighter_min_gap_gives_more_or_equal():
    """Smaller min_gap allows more onsets to survive the refractory period."""
    r_default = detect_onsets(_crotales())
    r_tight   = detect_onsets(_crotales(), min_gap_seconds=0.01)
    assert len(r_tight["onsets"]) >= len(r_default["onsets"])


@_skip_transient
def test_detect_onsets_higher_threshold_scale_gives_fewer():
    """Higher threshold_auto_scale raises the floor → fewer accepted onsets."""
    r_low  = detect_onsets(_crotales(), threshold_auto_scale=1.0)
    r_high = detect_onsets(_crotales(), threshold_auto_scale=5.0)
    assert len(r_high["onsets"]) <= len(r_low["onsets"])


@_skip_transient
def test_detect_onsets_onset_floor_high_gives_fewer():
    """Raising onset_floor rejects weaker candidates."""
    r_low  = detect_onsets(_crotales(), onset_floor=1e-9)
    r_high = detect_onsets(_crotales(), onset_floor=1e10)
    assert len(r_high["onsets"]) <= len(r_low["onsets"])


@_skip_transient
def test_detect_onsets_zero_weights_gives_no_onsets():
    """All-zero weights → combined signal is 0 → no onsets above floor."""
    r = detect_onsets(_crotales(),
                      weight_det_flux=0.0, weight_res_flux=0.0,
                      weight_hfc_flux=0.0, weight_birth=0.0,
                      weight_phase_dev=0.0, onset_floor=1e-9)
    assert len(r["onsets"]) == 0


# =============================================================================
# smear_attacks — file output and JSON return
# =============================================================================

@_skip_transient
def test_smear_attacks_produces_file(tmp_path):
    out = str(tmp_path / "smeared.ats")
    smear_attacks(_crotales(), out, blur_ms=20.0)
    assert Path(out).exists()


@_skip_transient
def test_smear_attacks_returns_expected_keys(tmp_path):
    out = str(tmp_path / "smeared.ats")
    r = smear_attacks(_crotales(), out, blur_ms=10.0)
    assert "onsets_processed" in r
    assert "changed" in r
    assert "blur_ms" in r
    assert "target" in r
    assert r["onsets_processed"] > 0


@_skip_transient
def test_smear_attacks_changed_true_for_nonzero_blur(tmp_path):
    out = str(tmp_path / "smeared.ats")
    r = smear_attacks(_crotales(), out, blur_ms=30.0)
    assert r["changed"] is True


@_skip_transient
def test_smear_attacks_zero_mix_is_identity(tmp_path):
    """mix=0 → dry passthrough → output amps should equal source."""
    out = str(tmp_path / "dry.ats")
    smear_attacks(_crotales(), out, blur_ms=50.0, mix=0.0)
    _, src = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    for sf, of in zip(src, out_frames):
        for a, b in zip(sf["amps"], of["amps"]):
            assert abs(a - b) < 1e-9


@_skip_transient
def test_smear_attacks_exaggerate_increases_peak_amp(tmp_path):
    """Positive exaggerate_db should raise peak amplitude above source."""
    out = str(tmp_path / "exag.ats")
    smear_attacks(_crotales(), out, exaggerate_db=6.0, target="partials")
    _, src = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    src_peak = max(max(f["amps"]) for f in src)
    out_peak = max(max(f["amps"]) for f in out_frames)
    assert out_peak >= src_peak - 1e-9


@_skip_transient
def test_smear_attacks_negative_exaggerate_softens(tmp_path):
    """Negative exaggerate_db should reduce peak amplitude."""
    out = str(tmp_path / "soften.ats")
    smear_attacks(_crotales(), out, exaggerate_db=-6.0, target="partials")
    _, src = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    src_peak = max(max(f["amps"]) for f in src)
    out_peak = max(max(f["amps"]) for f in out_frames)
    assert out_peak <= src_peak + 1e-9


@_skip_transient
def test_smear_attacks_residual_target_only(tmp_path):
    """target='residual' must not change partial amps."""
    out = str(tmp_path / "res_only.ats")
    smear_attacks(_crotales(), out, blur_ms=20.0, target="residual")
    _, src = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    for sf, of in zip(src, out_frames):
        for a, b in zip(sf["amps"], of["amps"]):
            assert abs(a - b) < 1e-9


@_skip_transient
def test_smear_attacks_invalid_target_raises(tmp_path):
    with pytest.raises(ValueError, match="target"):
        smear_attacks(_crotales(), str(tmp_path / "x.ats"), blur_ms=10.0, target="bogus")


@_skip_transient
def test_smear_attacks_no_effect_params_raises(tmp_path):
    with pytest.raises(ValueError, match="non-zero"):
        smear_attacks(_crotales(), str(tmp_path / "x.ats"),
                      blur_ms=0.0, sharpen_amount=0.0, exaggerate_db=0.0)


# =============================================================================
# suppress_onsets — zero mode
# =============================================================================

@_skip_transient
def test_suppress_onsets_produces_file(tmp_path):
    out = str(tmp_path / "suppressed.ats")
    suppress_onsets(_crotales(), out, onset_indices=[0])
    assert Path(out).exists()


@_skip_transient
def test_suppress_onsets_zero_zeroes_attack_window(tmp_path):
    """Suppressed onset's [pre_frame, peak_frame] should be all-zero amps."""
    out = str(tmp_path / "suppressed.ats")
    onsets = detect_onsets(_crotales())["onsets"]
    idx = 0
    suppress_onsets(_crotales(), out, onset_indices=[idx], onsets=onsets, mode="zero")
    _, out_frames = load_ats_frames_full(out)
    start = onsets[idx]["pre_frame"]
    end   = onsets[idx]["peak_frame"]
    for fi in range(start, end + 1):
        assert all(a == 0.0 for a in out_frames[fi]["amps"])


@_skip_transient
def test_suppress_onsets_zero_leaves_other_frames_unchanged(tmp_path):
    out = str(tmp_path / "suppressed.ats")
    onsets = detect_onsets(_crotales())["onsets"]
    idx = 1
    suppress_onsets(_crotales(), out, onset_indices=[idx], onsets=onsets, mode="zero")
    _, src = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    start = onsets[idx]["pre_frame"]
    end   = onsets[idx]["peak_frame"]
    for fi in range(len(src)):
        if start <= fi <= end:
            continue
        for a, b in zip(src[fi]["amps"], out_frames[fi]["amps"]):
            assert abs(a - b) < 1e-9


@_skip_transient
def test_suppress_onsets_returns_expected_keys(tmp_path):
    out = str(tmp_path / "suppressed.ats")
    r = suppress_onsets(_crotales(), out, onset_indices=[0])
    assert "onsets_suppressed" in r
    assert "onset_indices" in r
    assert "mode" in r
    assert "frames_modified" in r
    assert r["onsets_suppressed"] == 1


@_skip_transient
def test_suppress_onsets_multiple_indices(tmp_path):
    out = str(tmp_path / "suppressed.ats")
    r = suppress_onsets(_crotales(), out, onset_indices=[0, 2])
    assert r["onsets_suppressed"] == 2


# =============================================================================
# suppress_onsets — fade_out mode
# =============================================================================

@_skip_transient
def test_suppress_onsets_fadeout_end_frame_zero(tmp_path):
    """fade_out reaches gain=0 at peak_frame."""
    out = str(tmp_path / "faded.ats")
    onsets = detect_onsets(_crotales())["onsets"]
    idx = 0
    peak_fi = onsets[idx]["peak_frame"]
    suppress_onsets(_crotales(), out, onset_indices=[idx], onsets=onsets, mode="fade_out")
    _, out_frames = load_ats_frames_full(out)
    assert all(a == 0.0 for a in out_frames[peak_fi]["amps"])


@_skip_transient
def test_suppress_onsets_fadeout_pre_frame_unchanged(tmp_path):
    """fade_out pre_frame should match source (gain=1 at start)."""
    out = str(tmp_path / "faded.ats")
    onsets = detect_onsets(_crotales())["onsets"]
    idx = 0
    pre_fi = onsets[idx]["pre_frame"]
    # Only zero if pre != peak (otherwise span=0 and gain=0 at both)
    if onsets[idx]["pre_frame"] == onsets[idx]["peak_frame"]:
        pytest.skip("pre_frame == peak_frame: no ramp to test")
    suppress_onsets(_crotales(), out, onset_indices=[idx], onsets=onsets, mode="fade_out")
    _, src = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    for a, b in zip(src[pre_fi]["amps"], out_frames[pre_fi]["amps"]):
        assert abs(a - b) < 1e-9


# =============================================================================
# suppress_onsets — error handling
# =============================================================================

@_skip_transient
def test_suppress_onsets_bad_index_raises(tmp_path):
    with pytest.raises(ValueError, match="out of range"):
        suppress_onsets(_crotales(), str(tmp_path / "x.ats"), onset_indices=[9999])


@_skip_transient
def test_suppress_onsets_residual_requires_noise(tmp_path):
    from server import export_model
    type1 = str(tmp_path / "type1.ats")
    export_model(_crotales(), type1, output_type=1)
    with pytest.raises(ValueError, match="noise bands"):
        suppress_onsets(type1, str(tmp_path / "x.ats"),
                        onset_indices=[0], target="residual")

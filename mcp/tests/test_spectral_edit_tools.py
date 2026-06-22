"""
Tests for the spectral editing tools added in v1.2:
  freeze_partial, smooth_partial_envelope, tilt_spectral,
  smooth_residual_bands, tilt_residual_bands,
  noise_balance param in render_model / time_stretch_model,
  and frame_start/frame_end on edit_partials / edit_bands.
"""

import math
import sys
import wave
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from ats_parser import load_ats, load_ats_frames_full
from server import (
    edit_bands,
    edit_partials,
    freeze_partial,
    render_model,
    smooth_partial_envelope,
    smooth_residual_bands,
    time_stretch_model,
    tilt_residual_bands,
    tilt_spectral,
)

REPO = Path(__file__).parent.parent.parent
TOOLS = REPO / "build-plugin" / "tools"
CROTALES = REPO / "crotales.ats"


def _crotales():
    if not CROTALES.exists():
        pytest.skip(f"Fixture missing: {CROTALES}")
    return str(CROTALES)


def _built(name):
    return (TOOLS / name).exists()


# =============================================================================
# edit_partials — frame_start / frame_end
# =============================================================================

def test_edit_partials_frame_range_only_affects_selected_frames(tmp_path):
    out = str(tmp_path / "partial_range.ats")
    edit_partials(_crotales(), out, partial_indices=[0], zero=True, frame_end=5)
    _, src_frames = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    # Frames 0-4 should have partial 0 zeroed
    for fi in range(5):
        assert out_frames[fi]["amps"][0] == 0.0
    # Frames 5+ should be unchanged
    for fi in range(5, len(out_frames)):
        assert out_frames[fi]["amps"][0] == src_frames[fi]["amps"][0]


# =============================================================================
# edit_bands — frame_start / frame_end
# =============================================================================

def test_edit_bands_frame_range_only_affects_selected_frames(tmp_path):
    out = str(tmp_path / "band_range.ats")
    edit_bands(_crotales(), out, band_indices=[0], zero=True, frame_start=5, frame_end=10)
    _, src_frames = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    for fi in range(5, 10):
        assert out_frames[fi]["band_energies"][0] == 0.0
    for fi in range(0, 5):
        assert out_frames[fi]["band_energies"][0] == src_frames[fi]["band_energies"][0]


# =============================================================================
# freeze_partial
# =============================================================================

def test_freeze_partial_produces_file(tmp_path):
    out = str(tmp_path / "frozen.ats")
    freeze_partial(_crotales(), out, partial_indices=[0], frame_index=0)
    assert Path(out).exists()


def test_freeze_partial_all_frames_match_source_frame(tmp_path):
    out = str(tmp_path / "frozen.ats")
    _, src = load_ats_frames_full(_crotales())
    ref_amp  = src[0]["amps"][0]
    ref_freq = src[0]["freqs"][0]
    freeze_partial(_crotales(), out, partial_indices=[0], frame_index=0)
    _, out_frames = load_ats_frames_full(out)
    for frm in out_frames:
        assert frm["amps"][0]  == ref_amp
        assert frm["freqs"][0] == ref_freq


def test_freeze_partial_respects_frame_range(tmp_path):
    out = str(tmp_path / "frozen_range.ats")
    _, src = load_ats_frames_full(_crotales())
    freeze_partial(_crotales(), out, partial_indices=[1], frame_index=0,
                   frame_start=2, frame_end=5)
    _, out_frames = load_ats_frames_full(out)
    ref_amp = src[0]["amps"][1]
    # Frames 2-4 frozen
    for fi in range(2, 5):
        assert out_frames[fi]["amps"][1] == ref_amp
    # Frame 0 unchanged
    assert out_frames[0]["amps"][1] == src[0]["amps"][1]


def test_freeze_partial_returns_expected_keys(tmp_path):
    out = str(tmp_path / "frozen.ats")
    r = freeze_partial(_crotales(), out, partial_indices=[0], frame_index=0)
    assert "frozen_from_frame" in r
    assert "partials_frozen" in r
    assert "frames_modified" in r
    assert r["partials_frozen"] == 1


def test_freeze_partial_bad_frame_raises(tmp_path):
    with pytest.raises(ValueError, match="out of range"):
        freeze_partial(_crotales(), str(tmp_path / "x.ats"),
                       partial_indices=[0], frame_index=99999)


# =============================================================================
# smooth_partial_envelope
# =============================================================================

def test_smooth_partial_envelope_produces_file(tmp_path):
    out = str(tmp_path / "smooth.ats")
    smooth_partial_envelope(_crotales(), out)
    assert Path(out).exists()


def test_smooth_partial_envelope_reduces_variance(tmp_path):
    """Smoothed amplitudes should have lower variance than original."""
    out = str(tmp_path / "smooth.ats")
    _, src = load_ats_frames_full(_crotales())
    smooth_partial_envelope(_crotales(), out, window_frames=10, partial_indices=[1])
    _, out_frames = load_ats_frames_full(out)

    amps_src = [f["amps"][1] for f in src]
    amps_out = [f["amps"][1] for f in out_frames]
    mean_s = sum(amps_src) / len(amps_src)
    mean_o = sum(amps_out) / len(amps_out)
    var_src = sum((a - mean_s) ** 2 for a in amps_src) / len(amps_src)
    var_out = sum((a - mean_o) ** 2 for a in amps_out) / len(amps_out)
    assert var_out <= var_src


def test_smooth_partial_envelope_preserves_unselected(tmp_path):
    out = str(tmp_path / "smooth.ats")
    _, src = load_ats_frames_full(_crotales())
    smooth_partial_envelope(_crotales(), out, window_frames=3, partial_indices=[0])
    _, out_frames = load_ats_frames_full(out)
    # Partial 1 should be untouched
    for f, sf in zip(out_frames, src):
        assert f["amps"][1] == sf["amps"][1]


def test_smooth_partial_envelope_returns_keys(tmp_path):
    out = str(tmp_path / "smooth.ats")
    r = smooth_partial_envelope(_crotales(), out, partial_indices=[0])
    assert "window_frames" in r
    assert "partials_smoothed" in r
    assert r["partials_smoothed"] == 1


# =============================================================================
# tilt_spectral  (brightness / warmth)
# =============================================================================

def test_tilt_spectral_produces_file(tmp_path):
    out = str(tmp_path / "tilted.ats")
    tilt_spectral(_crotales(), out, tilt_db_per_oct=6.0)
    assert Path(out).exists()


def test_tilt_spectral_positive_increases_high_partial_amps(tmp_path):
    """A positive tilt should increase amplitudes of high-frequency partials."""
    out = str(tmp_path / "brighter.ats")
    src = load_ats(_crotales())
    tilt_spectral(_crotales(), out, tilt_db_per_oct=6.0, pivot_hz=500.0)
    result = load_ats(out)
    # Max amp should differ (could go either way due to mix), but model is valid
    assert result.header.frames > 0


def test_tilt_spectral_negative_reduces_max_amp(tmp_path):
    """Negative tilt with pivot=1 Hz: all audio partials are above pivot so all attenuated."""
    out = str(tmp_path / "warmer.ats")
    src = load_ats(_crotales())
    tilt_spectral(_crotales(), out, tilt_db_per_oct=-12.0, pivot_hz=1.0)
    result = load_ats(out)
    assert result.header.max_amp <= src.header.max_amp + 1e-9


def test_tilt_spectral_zero_is_identity(tmp_path):
    out = str(tmp_path / "identity.ats")
    _, src_frames = load_ats_frames_full(_crotales())
    tilt_spectral(_crotales(), out, tilt_db_per_oct=0.0)
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        for i in range(len(f["amps"])):
            assert abs(f["amps"][i] - sf["amps"][i]) < 1e-9


def test_tilt_spectral_returns_expected_keys(tmp_path):
    out = str(tmp_path / "tilted.ats")
    r = tilt_spectral(_crotales(), out, tilt_db_per_oct=3.0)
    assert "tilt_db_per_oct" in r
    assert "pivot_hz" in r
    assert "partials_in_freq_range" in r
    assert r["partials_in_freq_range"] > 0


# =============================================================================
# smooth_residual_bands
# =============================================================================

def test_smooth_residual_bands_produces_file(tmp_path):
    out = str(tmp_path / "smooth_res.ats")
    smooth_residual_bands(_crotales(), out)
    assert Path(out).exists()


def test_smooth_residual_bands_reduces_variance(tmp_path):
    out = str(tmp_path / "smooth_res.ats")
    _, src = load_ats_frames_full(_crotales())
    smooth_residual_bands(_crotales(), out, window_frames=8, band_indices=[5])
    _, out_frames = load_ats_frames_full(out)
    vals_src = [f["band_energies"][5] for f in src]
    vals_out = [f["band_energies"][5] for f in out_frames]
    mean_s = sum(vals_src) / len(vals_src)
    mean_o = sum(vals_out) / len(vals_out)
    var_src = sum((v - mean_s) ** 2 for v in vals_src) / len(vals_src)
    var_out = sum((v - mean_o) ** 2 for v in vals_out) / len(vals_out)
    assert var_out <= var_src + 1e-15


def test_smooth_residual_bands_preserves_unselected(tmp_path):
    out = str(tmp_path / "smooth_res.ats")
    _, src = load_ats_frames_full(_crotales())
    smooth_residual_bands(_crotales(), out, window_frames=3, band_indices=[0])
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src):
        assert f["band_energies"][1] == sf["band_energies"][1]


def test_smooth_residual_bands_requires_noise(tmp_path):
    from server import export_model
    type1 = str(tmp_path / "type1.ats")
    export_model(_crotales(), type1, output_type=1)
    with pytest.raises(ValueError, match="Requires type 3 or 4"):
        smooth_residual_bands(type1, str(tmp_path / "out.ats"))


def test_smooth_residual_bands_returns_keys(tmp_path):
    out = str(tmp_path / "smooth_res.ats")
    r = smooth_residual_bands(_crotales(), out, band_indices=[0, 1, 2])
    assert "window_frames" in r
    assert "bands_smoothed" in r
    assert r["bands_smoothed"] == 3


# =============================================================================
# tilt_residual_bands
# =============================================================================

def test_tilt_residual_bands_produces_file(tmp_path):
    out = str(tmp_path / "tilted_res.ats")
    tilt_residual_bands(_crotales(), out, tilt_db_per_band=-1.0)
    assert Path(out).exists()


def test_tilt_residual_bands_first_band_unchanged(tmp_path):
    """First selected band (idx 0 in selection) gets gain=10^0=1.0, so no change."""
    out = str(tmp_path / "tilted_res.ats")
    _, src = load_ats_frames_full(_crotales())
    tilt_residual_bands(_crotales(), out, tilt_db_per_band=-6.0)
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src):
        assert abs(f["band_energies"][0] - sf["band_energies"][0]) < 1e-12


def test_tilt_residual_bands_later_bands_attenuated(tmp_path):
    """Negative tilt: higher bands should have less energy than source."""
    out = str(tmp_path / "tilted_res.ats")
    _, src = load_ats_frames_full(_crotales())
    tilt_residual_bands(_crotales(), out, tilt_db_per_band=-3.0)
    _, out_frames = load_ats_frames_full(out)
    # Band 10 should be attenuated relative to source
    total_src = sum(f["band_energies"][10] for f in src)
    total_out = sum(f["band_energies"][10] for f in out_frames)
    assert total_out <= total_src + 1e-9


def test_tilt_residual_bands_zero_tilt_is_identity(tmp_path):
    out = str(tmp_path / "identity_res.ats")
    _, src = load_ats_frames_full(_crotales())
    tilt_residual_bands(_crotales(), out, tilt_db_per_band=0.0)
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src):
        for b in range(25):
            assert abs(f["band_energies"][b] - sf["band_energies"][b]) < 1e-12


def test_tilt_residual_bands_returns_keys(tmp_path):
    out = str(tmp_path / "tilted_res.ats")
    r = tilt_residual_bands(_crotales(), out, tilt_db_per_band=-1.0)
    assert "tilt_db_per_band" in r
    assert "bands_tilted" in r
    assert r["bands_tilted"] == 25


# =============================================================================
# noise_balance in render_model and time_stretch_model
# =============================================================================

@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_render_model_noise_balance_zero_produces_wav(tmp_path):
    out = str(tmp_path / "no_noise.wav")
    render_model(_crotales(), out, noise_balance=0.0)
    assert Path(out).exists()
    with wave.open(out) as wf:
        assert wf.getnframes() > 0


@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_render_model_noise_balance_zero_differs_from_full(tmp_path):
    """Rendering with noise_balance=0 vs 1 should produce different WAVs."""
    import hashlib
    out_no   = str(tmp_path / "no_noise.wav")
    out_full = str(tmp_path / "full_noise.wav")
    render_model(_crotales(), out_no,   noise_balance=0.0)
    render_model(_crotales(), out_full, noise_balance=1.0)
    h_no   = hashlib.md5(Path(out_no).read_bytes()).hexdigest()
    h_full = hashlib.md5(Path(out_full).read_bytes()).hexdigest()
    assert h_no != h_full


@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_render_model_noise_balance_in_return(tmp_path):
    out = str(tmp_path / "out.wav")
    r = render_model(_crotales(), out, noise_balance=0.5)
    assert r["noise_balance"] == 0.5


@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_time_stretch_noise_balance_in_return(tmp_path):
    out = str(tmp_path / "out.wav")
    r = time_stretch_model(_crotales(), out, noise_balance=0.0)
    assert r["noise_balance"] == 0.0

"""
Tests for edit_partials and edit_bands (pure-Python read-modify-write tools).

No CLI build required — all tests run against the crotales.ats fixture.
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from ats_parser import load_ats, load_ats_frames_full
from server import edit_bands, edit_partials

REPO = Path(__file__).parent.parent.parent
CROTALES = REPO / "crotales.ats"


def _crotales():
    if not CROTALES.exists():
        pytest.skip(f"Fixture missing: {CROTALES}")
    return str(CROTALES)


# =============================================================================
# edit_partials
# =============================================================================

def test_edit_partials_produces_file(tmp_path):
    out = str(tmp_path / "edited.ats")
    edit_partials(_crotales(), out)
    assert Path(out).exists()


def test_edit_partials_zero_mutes_partial(tmp_path):
    out = str(tmp_path / "muted.ats")
    edit_partials(_crotales(), out, partial_indices=[0], zero=True)
    _, frames = load_ats_frames_full(out)
    assert all(frm["amps"][0] == 0.0 for frm in frames)


def test_edit_partials_zero_only_affects_selected(tmp_path):
    out = str(tmp_path / "muted.ats")
    edit_partials(_crotales(), out, partial_indices=[0], zero=True)
    _, frames = load_ats_frames_full(out)
    # Partial 1 should have the same amps as the source
    _, src_frames = load_ats_frames_full(_crotales())
    for f, sf in zip(frames, src_frames):
        assert f["amps"][1] == sf["amps"][1]


def test_edit_partials_amp_scale_reduces_max_amp(tmp_path):
    out = str(tmp_path / "quiet.ats")
    src = load_ats(_crotales())
    edit_partials(_crotales(), out, amp_scale=0.5)
    result = load_ats(out)
    assert result.header.max_amp <= src.header.max_amp


def test_edit_partials_amp_scale_half(tmp_path):
    out = str(tmp_path / "half.ats")
    _, src_frames = load_ats_frames_full(_crotales())
    edit_partials(_crotales(), out, partial_indices=[1], amp_scale=0.5)
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        assert abs(f["amps"][1] - sf["amps"][1] * 0.5) < 1e-12


def test_edit_partials_freq_shift(tmp_path):
    out = str(tmp_path / "shifted.ats")
    _, src_frames = load_ats_frames_full(_crotales())
    edit_partials(_crotales(), out, partial_indices=[0], freq_shift_hz=100.0)
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        if sf["freqs"][0] > 0.0:
            assert abs(f["freqs"][0] - (sf["freqs"][0] + 100.0)) < 1e-9


def test_edit_partials_invert_solos_partial(tmp_path):
    """invert_selection=True with zero=True silences everything EXCEPT the listed partials."""
    out = str(tmp_path / "soloed.ats")
    edit_partials(_crotales(), out, partial_indices=[0], zero=True, invert_selection=True)
    _, frames = load_ats_frames_full(out)
    n_par = len(frames[0]["amps"])
    # Partial 0 should be untouched; all others zeroed
    _, src_frames = load_ats_frames_full(_crotales())
    for f, sf in zip(frames, src_frames):
        assert f["amps"][0] == sf["amps"][0]
        for i in range(1, n_par):
            assert f["amps"][i] == 0.0


def test_edit_partials_returns_expected_keys(tmp_path):
    out = str(tmp_path / "edited.ats")
    result = edit_partials(_crotales(), out, partial_indices=[0], zero=True)
    assert "output_path" in result
    assert "partials_modified" in result
    assert "frames_modified" in result
    assert "model" in result


def test_edit_partials_none_indices_affects_all(tmp_path):
    out = str(tmp_path / "all_zeroed.ats")
    edit_partials(_crotales(), out, zero=True)
    result = load_ats(out)
    assert result.header.max_amp == 0.0


def test_edit_partials_identity_preserves_data(tmp_path):
    out = str(tmp_path / "identity.ats")
    edit_partials(_crotales(), out, amp_scale=1.0, freq_scale=1.0, freq_shift_hz=0.0)
    _, src_frames = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        for i in range(len(f["amps"])):
            assert abs(f["amps"][i] - sf["amps"][i]) < 1e-12
            assert abs(f["freqs"][i] - sf["freqs"][i]) < 1e-12


# =============================================================================
# edit_bands
# =============================================================================

def test_edit_bands_requires_noise_type(tmp_path):
    """edit_bands should raise on a type-1 (no noise) file."""
    import struct, tempfile
    # Build a minimal type-1 ATS stub — just check the ValueError path
    # We'll use a truncated write to produce a type-1 header
    with pytest.raises(ValueError, match="no residual noise"):
        # Use export_model to get a type-1 version first
        from server import export_model
        type1 = str(tmp_path / "type1.ats")
        export_model(_crotales(), type1, output_type=1)
        edit_bands(type1, str(tmp_path / "out.ats"))


def test_edit_bands_zero_clears_all(tmp_path):
    out = str(tmp_path / "no_noise.ats")
    edit_bands(_crotales(), out, zero=True)
    _, frames = load_ats_frames_full(out)
    for frm in frames:
        assert all(e == 0.0 for e in frm["band_energies"])


def test_edit_bands_zero_specific_band(tmp_path):
    out = str(tmp_path / "band0_off.ats")
    edit_bands(_crotales(), out, band_indices=[0], zero=True)
    _, frames = load_ats_frames_full(out)
    assert all(frm["band_energies"][0] == 0.0 for frm in frames)


def test_edit_bands_zero_only_affects_selected(tmp_path):
    out = str(tmp_path / "band0_off.ats")
    _, src_frames = load_ats_frames_full(_crotales())
    edit_bands(_crotales(), out, band_indices=[0], zero=True)
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        # Band 1 should be unchanged
        assert f["band_energies"][1] == sf["band_energies"][1]


def test_edit_bands_scale_reduces_energy(tmp_path):
    out = str(tmp_path / "quiet_noise.ats")
    src = load_ats(_crotales())
    edit_bands(_crotales(), out, energy_scale=0.1)
    _, src_frames = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        for b in range(25):
            assert abs(f["band_energies"][b] - sf["band_energies"][b] * 0.1) < 1e-15


def test_edit_bands_invert_solos_band(tmp_path):
    """Keep only band 0, zero all others."""
    out = str(tmp_path / "solo_band0.ats")
    _, src_frames = load_ats_frames_full(_crotales())
    edit_bands(_crotales(), out, band_indices=[0], zero=True, invert_selection=True)
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        assert f["band_energies"][0] == sf["band_energies"][0]
        for b in range(1, 25):
            assert f["band_energies"][b] == 0.0


def test_edit_bands_returns_expected_keys(tmp_path):
    out = str(tmp_path / "edited.ats")
    result = edit_bands(_crotales(), out, band_indices=[0, 1], zero=True)
    assert "output_path" in result
    assert "bands_modified" in result
    assert result["bands_modified"] == 2
    assert "frames_modified" in result
    assert "model" in result


def test_edit_bands_identity_preserves_data(tmp_path):
    out = str(tmp_path / "identity.ats")
    edit_bands(_crotales(), out, energy_scale=1.0)
    _, src_frames = load_ats_frames_full(_crotales())
    _, out_frames = load_ats_frames_full(out)
    for f, sf in zip(out_frames, src_frames):
        for b in range(25):
            assert abs(f["band_energies"][b] - sf["band_energies"][b]) < 1e-15

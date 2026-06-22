"""
Tests for the full ANARGS parameter surface exposed via analyze_audio.

All tests require ats_analyze to be built.
"""

import sys
import wave
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from server import analyze_audio

REPO = Path(__file__).parent.parent.parent
TOOLS = REPO / "build-plugin" / "tools"


def _built(name):
    return (TOOLS / name).exists()


_skip = pytest.mark.skipif(not _built("ats_analyze"), reason="ats_analyze not built")


def _make_sine(path, freq=440.0, sr=44100, dur=0.5):
    import array, math, wave as _wave
    n = int(sr * dur)
    with _wave.open(str(path), "wb") as wf:
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
        wf.writeframes(array.array("h", (
            int(32767 * 0.4 * math.sin(2 * math.pi * freq * i / sr)) for i in range(n)
        )).tobytes())


def _make_multisine(path, freqs=(440.0, 5000.0), amp=0.2, sr=44100, dur=1.0):
    import array, math, wave as _wave
    n = int(sr * dur)
    with _wave.open(str(path), "wb") as wf:
        wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(sr)
        wf.writeframes(array.array("h", (
            int(32767 * amp * sum(math.sin(2 * math.pi * f * i / sr) for f in freqs))
            for i in range(n)
        )).tobytes())


# =============================================================================
# Defaults still work
# =============================================================================

@_skip
def test_default_params_produce_ats(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out = str(tmp_path / "out.ats")
    r = analyze_audio(str(wav), out)
    assert Path(out).exists()
    assert r["model"]["partials"] > 0


# =============================================================================
# Frequency range
# =============================================================================

@_skip
def test_narrow_freq_range_reduces_partials(tmp_path):
    """Restricting highest_freq to 1000 Hz on a 440+5000 Hz signal gives fewer partials.

    Both runs keep lowest_freq=20 so the ATSA adaptive window size stays identical
    (win_cycles × srate / lowest_freq).  Only highest_freq changes, acting as a
    post-tracking frequency ceiling that removes the 5000 Hz partial.
    """
    wav = tmp_path / "multi.wav"; _make_multisine(wav, freqs=(440.0, 5000.0), dur=1.0)
    out_full   = str(tmp_path / "full.ats")
    out_narrow = str(tmp_path / "narrow.ats")
    r_full   = analyze_audio(str(wav), out_full)
    r_narrow = analyze_audio(str(wav), out_narrow, highest_freq_hz=1000.0)
    assert r_narrow["model"]["active_partials"] <= r_full["model"]["active_partials"]


@_skip
def test_freq_range_above_content_raises(tmp_path):
    """Analyzing a range with no audio content causes ats_analyze to crash.

    When zero partials survive tracking, the ATSA residual-analysis stage
    segfaults (exit -11).  Document this as a RuntimeError so callers know
    to validate their frequency range before invoking analyze_audio.
    """
    wav = tmp_path / "sine.wav"; _make_sine(wav, freq=440.0)
    with pytest.raises(RuntimeError):
        analyze_audio(str(wav), str(tmp_path / "high.ats"),
                      lowest_freq_hz=10000.0, highest_freq_hz=20000.0,
                      window_cycles=0, window_size=1024)


# =============================================================================
# Window type
# =============================================================================

@_skip
def test_window_type_0_produces_ats(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out = str(tmp_path / "blackman.ats")
    r = analyze_audio(str(wav), out, window_type=0)
    assert Path(out).exists()
    assert r["model"]["partials"] > 0


@_skip
def test_window_type_2_produces_ats(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out = str(tmp_path / "hanning.ats")
    r = analyze_audio(str(wav), out, window_type=2)
    assert Path(out).exists()


@_skip
def test_invalid_window_type_raises(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    with pytest.raises(ValueError, match="window_type"):
        analyze_audio(str(wav), str(tmp_path / "bad.ats"), window_type=9)


# =============================================================================
# Magnitude threshold
# =============================================================================

@_skip
def test_high_mag_threshold_reduces_partials(tmp_path):
    """Raising the magnitude floor should detect fewer (or equal) partials."""
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out_lo = str(tmp_path / "lo.ats")
    out_hi = str(tmp_path / "hi.ats")
    r_lo = analyze_audio(str(wav), out_lo, lowest_mag_db=-80.0)
    r_hi = analyze_audio(str(wav), out_hi, lowest_mag_db=-20.0)
    assert r_hi["model"]["active_partials"] <= r_lo["model"]["active_partials"]


# =============================================================================
# SMR threshold
# =============================================================================

@_skip
def test_high_smr_threshold_reduces_active_partials(tmp_path):
    """Higher SMR threshold prunes more partials in ats_save."""
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out_lo = str(tmp_path / "smr_lo.ats")
    out_hi = str(tmp_path / "smr_hi.ats")
    r_lo = analyze_audio(str(wav), out_lo, smr_threshold_db=0.0)
    r_hi = analyze_audio(str(wav), out_hi, smr_threshold_db=80.0)
    assert r_hi["model"]["active_partials"] <= r_lo["model"]["active_partials"]


# =============================================================================
# Hop size
# =============================================================================

@_skip
def test_smaller_hop_more_frames(tmp_path):
    """Half the hop size → roughly twice as many frames."""
    wav = tmp_path / "sine.wav"; _make_sine(wav, dur=1.0)
    out_025 = str(tmp_path / "hop025.ats")
    out_05  = str(tmp_path / "hop050.ats")
    r025 = analyze_audio(str(wav), out_025, hop_size=0.25)
    r05  = analyze_audio(str(wav), out_05,  hop_size=0.5)
    assert r025["model"]["frames"] > r05["model"]["frames"]


# =============================================================================
# Multi-res params
# =============================================================================

@_skip
def test_multi_res_with_custom_crossover(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out = str(tmp_path / "mr.ats")
    r = analyze_audio(str(wav), out, multi_res=True, mr_crossover_hz=1000.0)
    assert Path(out).exists()
    assert r["model"]["partials"] > 0


# =============================================================================
# Analysis type
# =============================================================================

@_skip
def test_type1_has_no_phase_no_noise(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out = str(tmp_path / "type1.ats")
    r = analyze_audio(str(wav), out, analysis_type=1)
    assert r["model"]["has_phase"] is False
    assert r["model"]["has_noise"] is False


@_skip
def test_type3_has_noise(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    out = str(tmp_path / "type3.ats")
    r = analyze_audio(str(wav), out, analysis_type=3)
    assert r["model"]["has_noise"] is True
    assert r["model"]["has_phase"] is False


@_skip
def test_invalid_type_raises(tmp_path):
    wav = tmp_path / "sine.wav"; _make_sine(wav)
    with pytest.raises(ValueError):
        analyze_audio(str(wav), str(tmp_path / "bad.ats"), analysis_type=5)

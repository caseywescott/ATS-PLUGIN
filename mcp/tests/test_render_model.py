"""
Integration tests for analyze_audio and render_model.

These tests require ats_offline and ats_synth to be built.
They are skipped automatically when the tools are not present.

To build the tools:
  cmake --build build-plugin --target ats_offline ats_synth
"""

import sys
import wave
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))
from server import analyze_audio, render_model, summarize_model

REPO_ROOT = Path(__file__).parent.parent.parent
TOOLS_DIR = REPO_ROOT / "build-plugin" / "tools"
CROTALES = REPO_ROOT / "crotales.ats"


def _tools_present() -> bool:
    return (TOOLS_DIR / "ats_analyze").exists() and (TOOLS_DIR / "ats_synth").exists()


# ---------------------------------------------------------------------------
# render_model — synthesize from existing crotales.ats
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not _tools_present(), reason="ats_synth not built")
@pytest.mark.skipif(not CROTALES.exists(), reason="crotales.ats fixture missing")
def test_render_model_produces_wav(tmp_path):
    out_wav = str(tmp_path / "rendered.wav")
    result = render_model(str(CROTALES), out_wav, amp=1.0, frec=1.0)
    assert Path(out_wav).exists(), "render_model should produce a WAV file"
    assert result["wav_path"] == out_wav


@pytest.mark.skipif(not _tools_present(), reason="ats_synth not built")
@pytest.mark.skipif(not CROTALES.exists(), reason="crotales.ats fixture missing")
def test_render_model_wav_is_valid(tmp_path):
    out_wav = str(tmp_path / "rendered.wav")
    render_model(str(CROTALES), out_wav)
    with wave.open(out_wav, "rb") as wf:
        assert wf.getnchannels() == 1
        assert wf.getsampwidth() == 2  # PCM_16
        assert wf.getnframes() > 0


@pytest.mark.skipif(not _tools_present(), reason="ats_synth not built")
@pytest.mark.skipif(not CROTALES.exists(), reason="crotales.ats fixture missing")
def test_render_model_frec_changes_output(tmp_path):
    """Two renders with different frec should produce different files."""
    import hashlib
    wav_a = str(tmp_path / "pitch_normal.wav")
    wav_b = str(tmp_path / "pitch_up.wav")
    render_model(str(CROTALES), wav_a, frec=1.0)
    render_model(str(CROTALES), wav_b, frec=1.5)
    sha_a = hashlib.md5(Path(wav_a).read_bytes()).hexdigest()
    sha_b = hashlib.md5(Path(wav_b).read_bytes()).hexdigest()
    assert sha_a != sha_b, "Different frec values should produce different audio"


# ---------------------------------------------------------------------------
# analyze_audio — synthesize a reference tone, analyze it
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not _tools_present(), reason="ats_analyze not built")
def test_analyze_audio_produces_ats(tmp_path):
    """Generate a short 440 Hz sine WAV and analyze it."""
    import math
    import struct

    sr = 44100
    dur = 0.5
    n_frames = int(sr * dur)
    # Write a minimal mono PCM-16 WAV
    wav_path = tmp_path / "sine_440.wav"
    with wave.open(str(wav_path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        samples = b"".join(
            struct.pack("<h", int(32767 * 0.4 * math.sin(2 * math.pi * 440 * i / sr)))
            for i in range(n_frames)
        )
        wf.writeframes(samples)

    ats_path = str(tmp_path / "sine_440.ats")
    result = analyze_audio(str(wav_path), ats_path, analysis_type=1)
    assert Path(ats_path).exists(), "analyze_audio should produce an ATS file"
    assert result["model"]["partials"] > 0
    assert result["model"]["type"] == 1

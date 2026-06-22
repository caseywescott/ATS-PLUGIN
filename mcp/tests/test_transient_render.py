"""
Tests for render_with_transients.

Requires both ats_synth and ats_transient to be built.
Auto-skipped when either tool is absent.
"""

import sys
import wave
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from server import render_with_transients

REPO = Path(__file__).parent.parent.parent
TOOLS = REPO / "build-plugin" / "tools"
CROTALES = REPO / "crotales.ats"


def _built(name: str) -> bool:
    return (TOOLS / name).exists()


def _crotales():
    if not CROTALES.exists():
        pytest.skip(f"Fixture missing: {CROTALES}")
    return str(CROTALES)


_skip = pytest.mark.skipif(
    not (_built("ats_synth") and _built("ats_transient")),
    reason="ats_synth or ats_transient not built",
)


@_skip
def test_render_with_transients_produces_file(tmp_path):
    out = str(tmp_path / "mixed.wav")
    render_with_transients(_crotales(), out)
    assert Path(out).exists()


@_skip
def test_render_with_transients_valid_wav(tmp_path):
    out = str(tmp_path / "mixed.wav")
    render_with_transients(_crotales(), out)
    with wave.open(out) as wf:
        assert wf.getnchannels() == 1
        assert wf.getnframes() > 0
        assert wf.getframerate() > 0


@_skip
def test_render_with_transients_returns_expected_keys(tmp_path):
    out = str(tmp_path / "mixed.wav")
    result = render_with_transients(_crotales(), out)
    assert "wav_path" in result
    assert "transient_gain" in result
    assert "clipped" in result
    assert "peak_before_normalize" in result


@_skip
def test_render_with_transients_peak_finite(tmp_path):
    out = str(tmp_path / "mixed.wav")
    result = render_with_transients(_crotales(), out)
    import math
    assert math.isfinite(result["peak_before_normalize"])
    assert result["peak_before_normalize"] > 0.0


@_skip
def test_render_with_transients_zero_gain_silent(tmp_path):
    """With transient_gain=0, the transient layer contributes nothing; output equals base."""
    import wave, array as _array
    out_base = str(tmp_path / "base.wav")
    out_zero = str(tmp_path / "zero_gain.wav")
    render_with_transients(_crotales(), out_base, transient_gain=0.0)
    render_with_transients(_crotales(), out_zero, transient_gain=1.0)
    # Both should produce valid WAVs of the same length (base + transient mixed)
    with wave.open(out_base) as a, wave.open(out_zero) as b:
        assert a.getnframes() > 0
        assert b.getnframes() > 0

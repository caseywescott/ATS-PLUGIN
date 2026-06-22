"""
Tests for batch workflow and generate_variations tools.

Some tests require CLI tools to be built; they auto-skip when not present.
"""

import hashlib
import math
import struct
import sys
import wave
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from server import (
    batch_analyze,
    batch_render,
    generate_variations,
    sweep_frec,
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


def _make_sine_wav(path: Path, freq_hz: float = 440.0, sr: int = 44100, dur: float = 0.5):
    n = int(sr * dur)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        import math, array
        samples = array.array("h", (
            int(32767 * 0.4 * math.sin(2 * math.pi * freq_hz * i / sr))
            for i in range(n)
        ))
        wf.writeframes(samples.tobytes())


# =============================================================================
# batch_render
# =============================================================================

@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_batch_render_one_file(tmp_path):
    ats_dir = tmp_path / "ats"
    ats_dir.mkdir()
    import shutil
    shutil.copy(_crotales(), str(ats_dir / "crotales.ats"))
    out_dir = tmp_path / "wav"
    result = batch_render(str(ats_dir), str(out_dir))
    assert result["succeeded"] == 1
    assert len(result["files"]) == 1
    assert Path(result["files"][0]["wav"]).exists()


@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_batch_render_error_on_bad_file(tmp_path):
    ats_dir = tmp_path / "ats"
    ats_dir.mkdir()
    (ats_dir / "bad.ats").write_bytes(b"\x00" * 80)
    out_dir = tmp_path / "wav"
    result = batch_render(str(ats_dir), str(out_dir))
    assert result["total"] == 1
    assert len(result["errors"]) == 1


# =============================================================================
# sweep_frec
# =============================================================================

@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_sweep_frec_produces_n_wavs(tmp_path):
    frec_values = [0.5, 1.0, 2.0]
    result = sweep_frec(_crotales(), str(tmp_path), frec_values)
    assert len(result["renders"]) == len(frec_values)
    for r in result["renders"]:
        assert Path(r["wav"]).exists()


@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_sweep_frec_all_distinct(tmp_path):
    frec_values = [0.5, 1.0, 2.0]
    result = sweep_frec(_crotales(), str(tmp_path), frec_values)
    hashes = [hashlib.md5(Path(r["wav"]).read_bytes()).hexdigest() for r in result["renders"]]
    assert len(set(hashes)) == len(hashes), "Different frec values should produce distinct files"


# =============================================================================
# batch_analyze
# =============================================================================

@pytest.mark.skipif(not _built("ats_analyze"), reason="ats_analyze not built")
def test_batch_analyze_one_file(tmp_path):
    wav_dir = tmp_path / "wav"
    wav_dir.mkdir()
    _make_sine_wav(wav_dir / "sine.wav")
    out_dir = tmp_path / "ats"
    result = batch_analyze(str(wav_dir), str(out_dir), analysis_type=1)
    assert result["succeeded"] == 1
    assert result["files"][0]["model"]["partials"] > 0
    assert Path(result["files"][0]["ats"]).exists()


@pytest.mark.skipif(not _built("ats_analyze"), reason="ats_analyze not built")
def test_batch_analyze_glob_filter(tmp_path):
    wav_dir = tmp_path / "wav"
    wav_dir.mkdir()
    _make_sine_wav(wav_dir / "a.wav")
    _make_sine_wav(wav_dir / "b.wav")
    (wav_dir / "skip.mp3").write_bytes(b"dummy")  # should not be picked up
    out_dir = tmp_path / "ats"
    result = batch_analyze(str(wav_dir), str(out_dir), analysis_type=1, glob="*.wav")
    assert result["total"] == 2  # only the two WAVs


# =============================================================================
# generate_variations
# =============================================================================

@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_generate_variations_produces_n(tmp_path):
    result = generate_variations(_crotales(), str(tmp_path), n=3, seed=42)
    assert len(result["variations"]) == 3


@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_generate_variations_files_exist(tmp_path):
    result = generate_variations(_crotales(), str(tmp_path), n=3, seed=42)
    for v in result["variations"]:
        assert Path(v["ats_path"]).exists()
        assert Path(v["wav_path"]).exists()


@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_generate_variations_seed_reproducible(tmp_path):
    tmp1 = tmp_path / "run1"
    tmp2 = tmp_path / "run2"
    r1 = generate_variations(_crotales(), str(tmp1), n=3, seed=99)
    r2 = generate_variations(_crotales(), str(tmp2), n=3, seed=99)
    p1 = [v["params"] for v in r1["variations"]]
    p2 = [v["params"] for v in r2["variations"]]
    assert p1 == p2


@pytest.mark.skipif(not _built("ats_transform"), reason="ats_transform not built")
@pytest.mark.skipif(not _built("ats_synth"), reason="ats_synth not built")
def test_generate_variations_params_differ(tmp_path):
    result = generate_variations(_crotales(), str(tmp_path), n=4, seed=7)
    params_list = [tuple(sorted(v["params"].items())) for v in result["variations"]]
    # At least some params differ across variations
    assert len(set(params_list)) > 1

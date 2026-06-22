# ATS MCP Server — Spec

**Source:** [mcp/server.py](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/server.py)  
**Parser:** [mcp/ats_parser.py](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/ats_parser.py)  
**Tests:** [mcp/tests/](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/tests/)  
**README:** [mcp/README.md](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/README.md)  
**Status:** v1.3 complete — 27 tools, 151 tests, 151 passing

---

## What this is

A **Model Context Protocol (MCP) server** that exposes the ATS offline toolchain to Claude Desktop (or any MCP client). Claude can call any of the 27 tools below by name, pass typed arguments, and receive structured JSON results — enabling full spectral-analysis/synthesis/morphing workflows from a conversation.

The server is built on `FastMCP` (the `mcp` Python library, v1.0+, requires Python ≥ 3.10). Every tool that needs compilation delegates to a CLI subprocess; pure-Python tools work with no build required.

---

## Architecture

```
Claude Desktop
      │  stdio / MCP protocol
      ▼
mcp/server.py  (FastMCP, Python 3.10+)
      │
      ├── Pure Python tools
      │     └── ats_parser.py  ← reads .ats binary directly
      │           load_ats(), load_ats_frames(), load_ats_frames_full(), write_ats()
      │
      └── CLI-backed tools
            ├── ats_analyze    (C99)    WAV → ATS model (all ANARGS exposed)
            ├── ats_synth      (C99)    ATS → WAV (oscillator bank; --ramp, --stretch)
            ├── ats_prune      (C99)    prune partials / re-export type
            ├── ats_transform  (C++17)  frequency warp + amp/noise scale
            ├── ats_envelope   (C++17)  True Envelope (cepstral), formant compare
            ├── ats_transient  (C++17)  onset detect / transient event extract / render
            └── ats_morph_plan (C++17)  1D Wasserstein OT correspondence plan
```

All CLI tools live in `tools/` and are built into `build-plugin/tools/`. The server locates them via `$ATS_TOOLS_DIR` (default: `<repo>/build-plugin/tools/`).

### Key design decisions

**stdout JSON extraction.** `ats_load()` (ATSA library) writes diagnostic text to stdout — not stderr — before the tool's JSON output. The server uses `_parse_json_stdout()` which seeks to the first `{` before calling `json.loads`. This affects `ats_envelope`, `ats_transient`, and `ats_morph_plan`.

**Time-stretch via CURVE endpoint.** `SPARAMS.max_stretch` is overwritten by `do_synthesis()` from the internal CURVE breakpoints; setting it directly has no effect. Time-stretching by factor `S` requires setting `timenv.ctlpoint[1][0] = S` after `curve_init()`, which scales the output x-axis to `S × model_dur`.

**Transient mixing is Python-side.** `render_with_transients` runs `ats_synth` for the sinusoidal base and `ats_transient --mode render` for the transient layer WAV, then mixes them in Python (`wave` + `array`). This avoids a C++/C linker boundary in `ats_synth.c`.

**Surgical edits via pure Python write-back.** `load_ats_frames_full()` + `write_ats()` read the full per-frame data into dicts and write a valid ATS binary (recomputing `max_amp`/`max_freq` header fields from frame data). All surgical edit tools (`edit_partials`, `edit_bands`, `freeze_partial`, `smooth_partial_envelope`, `tilt_spectral`, `smooth_residual_bands`, `tilt_residual_bands`) use this pattern — no CLI round-trip.

**ATSA window size is always adaptive.** `ats_analyze` exposes `window_cycles` (1–8) and `window_size`, but the ATSA tracker enforces `window_cycles ∈ [1, 8]` (out-of-range → forced to 4) and **always** computes `win_size = window_cycles × srate / lowest_freq_hz`. The `window_size` param is thus effectively overridden. The real knob for window resolution is `window_cycles` and `lowest_freq_hz` together; changing `lowest_freq_hz` changes the window size as a side effect.

**Zero-partial crash in ats_analyze.** When the frequency range excludes all audio content (e.g. `lowest_freq_hz=10000` on a 440 Hz sine), zero partials survive tracking and ATSA segfaults (exit -11) in residual computation. `analyze_audio()` raises `RuntimeError`. Callers should validate their frequency range contains expected content before invoking.

---

## ATS binary format (reference)

All values IEEE 754 float64. Magic `123.0` at byte 0 determines endianness.

```
Header (80 bytes = 10 × f64):
  mag=123.0, sample_rate, frame_size, window_size,
  partials, frames, max_amp, max_freq, duration, type

Per-frame loop (frames iterations):
  time:           1 × f64
  per partial:
    amp:          1 × f64
    freq:         1 × f64
    pha:          1 × f64   ← only if type ∈ {2, 4}
  per Bark band (25):
    band_energy:  1 × f64   ← only if type ∈ {3, 4}
```

Types: **1**=amp+freq · **2**=+phase · **3**=+noise · **4**=+phase+noise

---

## Tool surface — 27 tools

### Model I/O (8 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `summarize_model` | Pure Python | Header + partial stats; no build needed |
| `validate_model` | Pure Python | Checks magic, size formula, type ∈ {1,2,3,4} |
| `analyze_audio` | `ats_analyze` | Mono WAV → ATSA `tracker()` → `.ats`; **all 24 ANARGS fields exposed** — see below |
| `render_model` | `ats_synth` | Oscillator-bank synthesis; `amp`, `frec`, **`noise_balance`** (→ `SPARAMS.ramp`) |
| `time_stretch_model` | `ats_synth` | `stretch` factor via CURVE x-endpoint; `beg`/`end` window; **`noise_balance`** |
| `compare_models` | Pure Python | Header scalar diffs + per-Bark-band energy delta (B−A) |
| `prune_model` | `ats_prune` | `optimize_sound()` + SMR/frame-count thresholds |
| `export_model` | `ats_prune` | Re-export as different ATS type (strip/add phase or noise) |

### Descriptors (1 tool)

| Tool | Backend | Notes |
|------|---------|-------|
| `measure_descriptors` | Pure Python | Centroid, spread, brightness, tilt (dB/oct LS slope), RMS envelope per frame |

### Spectral Transforms (1 tool)

| Tool | Backend | Notes |
|------|---------|-------|
| `transform_model` | `ats_transform` | Modes: Linear / Stretch / Compress / Harmonic (via `AtsFrequencyShape`); `amp_scale`, `noise_scale` |

### Surgical Edits (7 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `edit_partials` | Pure Python | Per-partial amp scale, freq scale/shift, or zero; `invert_selection` to solo; `frame_start`/`frame_end` |
| `edit_bands` | Pure Python | Per-Bark-band energy scale or zero; `invert_selection`; requires type 3/4; `frame_start`/`frame_end` |
| `freeze_partial` | Pure Python | Copies amp+freq from a reference frame across all (or range of) frames; `frame_index`, `frame_start`/`frame_end` |
| `smooth_partial_envelope` | Pure Python | Box-smooth amplitude envelope of selected partials; `window_frames`; `frame_start`/`frame_end` |
| `tilt_spectral` | Pure Python | Spectral tilt via `gain = 10^(tilt_db_per_oct × log2(freq/pivot_hz)/20)`; positive=brighter, negative=warmer |
| `smooth_residual_bands` | Pure Python | Box-smooth energy of selected Bark bands over time; requires type 3/4 |
| `tilt_residual_bands` | Pure Python | Linear ramp gain over band index (0=no change, N=±N×tilt_db); shapes noise color |

### Envelopes (2 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `build_envelope` | `ats_envelope` | `BarkSpectralEnvelope::build()` per frame; Bark-gridded log-amp bins + formant peaks |
| `compare_envelopes` | `ats_envelope` | `FormantAlignedEnvelopeMorph::build()` between two models; formant warp mapping |

### Transients (3 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `detect_onsets` | `ats_transient` | Smearnov flux; returns `{frame, pre_frame, peak_frame, time_s, strength}` per onset |
| `extract_transient_layer` | `ats_transient` | `extractTransientLayer()` → per-onset `{band_energy[25], deterministic_energy}` |
| `render_with_transients` | `ats_synth` + `ats_transient` | Base WAV + transient WAV mixed in Python; `transient_gain` scale; normalizes if peak > 1 |

### Morphing (1 tool)

| Tool | Backend | Notes |
|------|---------|-------|
| `plan_morph` | `ats_morph_plan` | 1D Wasserstein OT on Bark-position/amplitude-mass repr; returns coupling matrix + cost; optionally writes JSON |

### Batch / Workflow (4 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `batch_analyze` | `ats_analyze` | Directory of WAVs → directory of `.ats`; glob filter; returns succeeded/errors; **forwards all 24 ANARGS params** |
| `batch_render` | `ats_synth` | Directory of `.ats` → directory of WAVs |
| `sweep_frec` | `ats_synth` | One model × N frequency scales → N WAVs |
| `generate_variations` | `ats_transform` + `ats_synth` | Random walk in (freq_mode, freq_amount, amp_scale, noise_scale); seed for reproducibility |

### Recipe (2 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `create_recipe` | Pure Python | Builds `AtsMorphRecipe` flat key=value text from two ATS files; includes hash, hop, freq range |
| `validate_recipe` | Pure Python | Checks required fields, schema/engine version, numeric field parseability |

### Diagnostics (5 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `check_build` | Pure Python | Probes all 7 CLI tools; runs `--help`; returns found/path/help_ok per tool |
| `list_fixtures` | Pure Python | Reads `tests/fixtures/morph_fixture_manifest.json` if present |
| `estimate_analysis_time` | Pure Python | Heuristic: `dur × base_factor × type_mult × multi_res_mult` |
| `run_golden_compare` | Python script | Runs `tests/golden_compare.py` subprocess |
| `evaluate_morph_fixture` | Python script | Runs `tests/morph_metric_harness.py` subprocess; optional `--fixture` name |

---

## analyze_audio — full parameter reference

All 24 ANARGS fields are exposed. Defaults match ATSA classic.

| Parameter | Default | Notes |
|-----------|---------|-------|
| `analysis_type` | 4 | 1=amp+freq · 2=+phase · 3=+noise · 4=+phase+noise |
| `multi_res` | False | Enable multi-resolution analysis front-end |
| `no_slopes` | False | Skip DDM slope computation (legacy baseline) |
| `start_time` | 0.0 s | Analysis start within the WAV |
| `duration` | 0.0 s | 0 = whole file |
| `lowest_freq_hz` | 20.0 | Also drives adaptive window size: `win_size = window_cycles × sr / lowest_freq_hz` |
| `highest_freq_hz` | 20000.0 | Post-tracking frequency ceiling |
| `window_cycles` | 4 | 1–8; out-of-range forced to 4 by ATSA |
| `window_type` | 1 | 0=Blackman · 1=Blackman-Harris · 2=Hanning |
| `window_size` | 1024 | Overridden by adaptive formula — see design note above |
| `hop_size` | 0.25 | Fraction of window size |
| `lowest_mag_db` | -60.0 | Peak detection floor |
| `freq_dev` | 0.1 | Frequency deviation tolerance for tracking |
| `track_len` | 3 | Tracking history length in frames |
| `min_seg_len` | 3 | Minimum segment length in frames |
| `min_gap_len` | 3 | Minimum gap length in frames |
| `last_peak_cont` | 0.0 | Last peak continuation factor |
| `smr_cont` | 0.0 | SMR continuation factor |
| `smr_threshold_db` | 30.0 | SMR threshold for `ats_save` partial pruning |
| `min_seg_smr_db` | 60.0 | Minimum segment SMR |
| `mr_window_mult` | 2.0 | Multi-res: low-band window size multiplier |
| `mr_crossover_hz` | 500.0 | Multi-res: crossover frequency |

---

## CLI tool build targets

```bash
cmake -B build-plugin -DCMAKE_BUILD_TYPE=Release
cmake --build build-plugin --target \
  ats_analyze ats_synth ats_prune \
  ats_transform ats_envelope ats_transient ats_morph_plan
```

| Binary | Lang | Links against |
|--------|------|---------------|
| `ats_analyze` | C99 | `atsa` (tracker, save-load) |
| `ats_synth` | C99 | `atsa` (synthesis, curves) |
| `ats_prune` | C99 | `atsa` (optimize_sound, save-load) |
| `ats_transform` | C++17 | `atsa` + `AtsFrequencyShape.cpp` + `AtsParams.cpp` |
| `ats_envelope` | C++17 | `atsa` + `AtsSpectralEnvelope.cpp` |
| `ats_transient` | C++17 | `atsa` + `AtsTransientLayer.cpp` + `AtsTransientSmear.cpp` |
| `ats_morph_plan` | C++17 | `atsa` + `AtsMorphTransport.cpp` + `AtsSpectralEnvelope.cpp` |

---

## Claude Desktop config

`~/Library/Application Support/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "ats-tools": {
      "command": "python3",
      "args": ["/Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/server.py"],
      "env": {
        "ATS_TOOLS_DIR": "/Users/caseywescott/Documents/Projects/ATS-PLUGIN/build-plugin/tools"
      }
    }
  }
}
```

---

## Test suite

```
mcp/tests/
  conftest.py                  — shared built() helper + crotales_path fixture
  test_summarize_model.py      — 18 tests: magic, header fields, type flags, dict keys
  test_render_model.py         —  4 tests: wav produced, valid, frec changes output, analyze→ats
  test_pure_python_tools.py    — 28 tests: validate_model, measure_descriptors, check_build,
                                            list_fixtures, create_recipe, validate_recipe,
                                            estimate_analysis_time
  test_cli_tools.py            — 25 tests: prune, export, time_stretch, transform,
                                            build_envelope, compare_envelopes,
                                            detect_onsets, extract_transient_layer, plan_morph
  test_batch_tools.py          — 10 tests: batch_render, sweep_frec, batch_analyze,
                                            generate_variations
  test_transient_render.py     —  5 tests: render_with_transients (file, valid wav, keys,
                                            peak finite, zero-gain edge case)
  test_edit_tools.py           — 18 tests: edit_partials (zero, scale, freq shift, invert/solo,
                                            identity, none-indices); edit_bands (same + noise-type guard)
  test_spectral_edit_tools.py  — 30 tests: freeze_partial (5), smooth_partial_envelope (4),
                                            tilt_spectral (5), smooth_residual_bands (5),
                                            tilt_residual_bands (5), noise_balance in
                                            render_model/time_stretch_model (6)
  test_analyze_params.py       — 13 tests: default params, freq range (highest_freq ceiling,
                                            zero-content raises RuntimeError), window types 0/2,
                                            invalid window type, magnitude threshold, SMR threshold,
                                            hop size → frame count, multi-res, type 1/3 flags,
                                            invalid type raises
```

**Total: 151 tests, all passing.** Integration tests auto-skip when the required CLI binary is not built.

Run:

```bash
cd /Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp
python3.11 -m pytest tests/ -v
```

---

## AtsMorphRecipe format (reference)

Flat `key=value` newline-separated text (not JSON). Required fields:

```
schema_version=1
engine_version=morph-recipe-v1
source_a.id=<stem>
source_a.hash=<sha1 prefix>
source_a.name=<stem>
source_a.sample_rate=<float>
source_a.duration_seconds=<float>
source_a.frame_hop_seconds=<float>
source_a.frame_count=<int>
source_a.partial_count=<int>
source_b.*=   (same fields)
```

`create_recipe` also writes: `lowest_frequency_hz`, `highest_frequency_hz`, `channel_count`, `has_phase`, `has_residual`, `default_morph_amount`.

---

## Related notes

- [[Modern ATSH Engine - PRD Summary]] — engine design this MCP exposes offline
- [[Spectral Envelope - PRD Summary]] — `build_envelope` / `compare_envelopes` back-end spec
- [[Plugin (VST) - PRD Summary]] — what the MCP is *not* (RT path lives in the plugin)
- [[Feature_Matrix]] — which classic ATSH features are implemented vs stubbed

`#ats-plugin` `#mcp` `#tooling` `#spec`

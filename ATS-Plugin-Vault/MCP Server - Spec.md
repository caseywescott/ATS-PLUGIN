# ATS MCP Server — Spec

**Source:** [mcp/server.py](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/server.py)  
**Parser:** [mcp/ats_parser.py](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/ats_parser.py)  
**Tests:** [mcp/tests/](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/tests/)  
**README:** [mcp/README.md](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/mcp/README.md)  
**Status:** v1.1 complete — 27 tools, 108 tests, 108 passing

---

## What this is

A **Model Context Protocol (MCP) server** that exposes the ATS offline toolchain to Claude Desktop (or any MCP client). Claude can call any of the 25 tools below by name, pass typed arguments, and receive structured JSON results — enabling full spectral-analysis/synthesis/morphing workflows from a conversation.

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
      │
      └── CLI-backed tools
            ├── ats_analyze    (C99)    WAV → ATS model
            ├── ats_synth      (C99)    ATS → WAV (oscillator bank)
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

## Tool surface — 25 tools

### Model I/O (8 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `summarize_model` | Pure Python | Header + partial stats; no build needed |
| `validate_model` | Pure Python | Checks magic, size formula, type ∈ {1,2,3,4} |
| `analyze_audio` | `ats_analyze` | Mono WAV → ATSA `tracker()` → `.ats`; type 1–4, multi-res flag |
| `render_model` | `ats_synth` | Oscillator-bank synthesis; `amp`, `frec` controls |
| `time_stretch_model` | `ats_synth` | `stretch` factor via CURVE x-endpoint; `beg`/`end` window |
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

### Surgical Edits (2 tools)

| Tool | Backend | Notes |
|------|---------|-------|
| `edit_partials` | Pure Python | Per-partial amp scale, freq scale/shift, or zero; `invert_selection` to solo |
| `edit_bands` | Pure Python | Per-Bark-band energy scale or zero; `invert_selection` to solo; requires type 3/4 |

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
| `batch_analyze` | `ats_analyze` | Directory of WAVs → directory of `.ats`; glob filter; returns succeeded/errors |
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
  conftest.py               — shared built() helper + crotales_path fixture
  test_summarize_model.py   — 18 tests: magic, header fields, type flags, dict keys
  test_render_model.py      —  4 tests: wav produced, valid, frec changes output, analyze→ats
  test_pure_python_tools.py — 28 tests: validate_model, measure_descriptors, check_build,
                                         list_fixtures, create_recipe, validate_recipe,
                                         estimate_analysis_time
  test_cli_tools.py         — 25 tests: prune, export, time_stretch, transform,
                                         build_envelope, compare_envelopes,
                                         detect_onsets, extract_transient_layer, plan_morph
  test_batch_tools.py       — 10 tests: batch_render, sweep_frec, batch_analyze,
                                         generate_variations
  test_transient_render.py  —  5 tests: render_with_transients (file, valid wav, keys,
                                         peak finite, zero-gain edge case)
  test_edit_tools.py        — 18 tests: edit_partials (zero, scale, freq shift, invert/solo,
                                         identity, none-indices); edit_bands (same + noise-type guard)
```

All 108 tests pass. Integration tests auto-skip when the required CLI binary is not built.

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

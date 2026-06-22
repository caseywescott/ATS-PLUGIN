# ATS MCP Server — v1.0

Offline spectral modeling tools for the ATS plugin, exposed as an
[MCP (Model Context Protocol)](https://modelcontextprotocol.io) server.
Provides 25+ tools for analysis, synthesis, transforms, envelopes, transients,
morphing, batch workflows, and diagnostics — all usable from Claude Desktop or
any MCP-compatible client.

## Prerequisites

### Python ≥ 3.10
The `mcp` library requires Python 3.10+ (uses `match` statements internally).

```bash
cd mcp
pip install -r requirements.txt
```

### CLI tools (required for synthesis and analysis tools)

Build all tools from the repo root:

```bash
cmake -B build-plugin -DCMAKE_BUILD_TYPE=Release
cmake --build build-plugin --target \
  ats_analyze ats_synth ats_prune \
  ats_transform ats_envelope ats_transient ats_morph_plan
```

The server looks for tools in `build-plugin/tools/` by default.
Override with the `ATS_TOOLS_DIR` environment variable:

```bash
export ATS_TOOLS_DIR=/path/to/your/build/tools
```

Run `check_build` (see below) to verify all tools are present and runnable.

## Running the server

```bash
cd mcp
python3 server.py
```

Or via the MCP CLI (stdio transport):

```bash
mcp run server.py
```

## Claude Desktop integration

Add to `~/Library/Application Support/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "ats-tools": {
      "command": "python3",
      "args": ["/absolute/path/to/ATS-PLUGIN/mcp/server.py"],
      "env": {
        "ATS_TOOLS_DIR": "/absolute/path/to/ATS-PLUGIN/build-plugin/tools"
      }
    }
  }
}
```

Replace the paths with your actual repo location.

## Tool surface

### Model I/O

| Tool | Backend | Description |
|------|---------|-------------|
| `summarize_model` | Pure Python | Header + partial stats from `.ats` file |
| `validate_model` | Pure Python | Structural integrity check (magic, size, type) |
| `analyze_audio` | `ats_analyze` | WAV → ATSA peak tracking → `.ats` model |
| `render_model` | `ats_synth` | `.ats` → oscillator-bank WAV (amp, frec controls) |
| `time_stretch_model` | `ats_synth` | `.ats` → WAV with time stretch / window selection |
| `compare_models` | Pure Python | Header diff + per-Bark-band energy delta |
| `prune_model` | `ats_prune` | Drop low-energy/short partials, re-save |
| `export_model` | `ats_prune` | Re-export as different ATS type (strip phase/noise) |

### Descriptors

| Tool | Backend | Description |
|------|---------|-------------|
| `measure_descriptors` | Pure Python | Centroid, spread, brightness, tilt, RMS envelope |

### Surgical Edits

| Tool | Backend | Description |
|------|---------|-------------|
| `edit_partials` | Pure Python | Scale amp/freq, shift freq, or zero individual partials; `invert_selection` to solo |
| `edit_bands` | Pure Python | Scale or zero individual Bark-band noise energies; requires type 3/4 |

### Spectral Transforms

| Tool | Backend | Description |
|------|---------|-------------|
| `transform_model` | `ats_transform` | Frequency warp (Linear/Stretch/Compress/Harmonic), amp/noise scale |

### Envelopes

| Tool | Backend | Description |
|------|---------|-------------|
| `build_envelope` | `ats_envelope` | True-Envelope (cepstral) per frame, Bark-gridded + formant peaks |
| `compare_envelopes` | `ats_envelope` | Formant-aligned envelope comparison between two models |

### Transients

| Tool | Backend | Description |
|------|---------|-------------|
| `detect_onsets` | `ats_transient` | Smearnov flux-based onset detection |
| `extract_transient_layer` | `ats_transient` | Per-onset band-energy event descriptors |
| `render_with_transients` | `ats_synth` + `ats_transient` | Sinusoidal base mixed with transient re-injection layer |

### Morphing

| Tool | Backend | Description |
|------|---------|-------------|
| `plan_morph` | `ats_morph_plan` | Optimal-transport correspondence plan (1D Wasserstein on Bark) |

### Batch / Workflow

| Tool | Backend | Description |
|------|---------|-------------|
| `batch_analyze` | `ats_analyze` | Analyze all WAVs in a directory |
| `batch_render` | `ats_synth` | Render all `.ats` files in a directory |
| `sweep_frec` | `ats_synth` | Render one model at multiple frequency scales |
| `generate_variations` | `ats_transform` + `ats_synth` | Random parametric variations of a model |

### Recipe

| Tool | Backend | Description |
|------|---------|-------------|
| `create_recipe` | Pure Python | Build AtsMorphRecipe key=value text from two ATS files |
| `validate_recipe` | Pure Python | Validate a recipe string against required fields |

### Diagnostics

| Tool | Backend | Description |
|------|---------|-------------|
| `check_build` | Pure Python | Verify all 7 CLI tools are present and runnable |
| `list_fixtures` | Pure Python | List morph test fixtures from manifest JSON |
| `estimate_analysis_time` | Pure Python | Heuristic time estimate for ATSA analysis of a WAV |
| `run_golden_compare` | Python script | Run `tests/golden_compare.py` harness |
| `evaluate_morph_fixture` | Python script | Run `tests/morph_metric_harness.py` harness |

## Selected tool schemas

### `summarize_model`

```
Input:  { "ats_path": str }
Output: {
  "file_path": str, "file_size_bytes": int,
  "type": 1|2|3|4,
  "type_description": "amp+freq[+phase][+noise]",
  "sample_rate": int, "frame_size": int, "window_size": int,
  "partials": int, "active_partials": int, "frames": int,
  "duration_s": float, "max_amp": float, "max_freq_hz": float,
  "freq_range_hz": [float, float], "has_phase": bool, "has_noise": bool,
  "mean_band_energies": [float × 25] | null
}
```

### `transform_model`

```
Input:  {
  "ats_path": str, "output_path": str,
  "freq_mode": "Linear"|"Stretch"|"Compress"|"Harmonic",  # default "Linear"
  "freq_amount": float,    # mode-specific warp amount (default 0.0)
  "freq_pivot_hz": float,  # pivot for Stretch/Compress/Harmonic (default 440.0)
  "amp_scale": float,      # multiply all amplitudes (default 1.0)
  "noise_scale": float,    # multiply all band energies (default 1.0)
  "output_type": 1|2|3|4 | null  # re-save as different type (null = keep)
}
Output: { "output_path": str, "model": <summarize_model output>, "stdout": str }
```

### `time_stretch_model`

```
Input:  {
  "ats_path": str, "wav_path": str,
  "stretch": float,  # time stretch factor (>1 = longer; default 1.0)
  "beg": float,      # start time in seconds (default 0.0)
  "end": float|null, # end time in seconds (null = model duration)
  "amp": float       # amplitude scale (default 1.0)
}
Output: { "wav_path": str, "stretch": float, "beg": float, "end": float|null }
```

### `build_envelope`

```
Input:  {
  "ats_path": str,
  "frame_start": int,    # default 0
  "frame_end": int|null, # null = all frames
  "stride": int          # default 1
}
Output: {
  "frames": [{
    "frame": int, "time_s": float,
    "bark_bins": [float × N],
    "formant_peaks": [float, ...]
  }, ...]
}
```

### `plan_morph`

```
Input:  {
  "ats_path_a": str, "ats_path_b": str,
  "plan_output_path": str | null  # write JSON here if given
}
Output: {
  "source_partials": int, "target_partials": int,
  "source_mass": float, "target_mass": float,
  "transport_cost": float, "algorithm": "optimal_transport",
  "coupling": [[src_idx, tgt_idx, mass, src_bark, tgt_bark], ...]
}
```

### `generate_variations`

```
Input:  {
  "ats_path": str, "output_dir": str,
  "n": int,          # number of variations (default 4)
  "seed": int|null,  # RNG seed for reproducibility
  "variation_space": {
    "freq_mode": ["Stretch", "Compress", "Harmonic"],
    "freq_amount": [min, max],
    "amp_scale":   [min, max],
    "noise_scale": [min, max]
  } | null
}
Output: {
  "variations": [{"index": int, "params": dict, "ats_path": str, "wav_path": str}, ...],
  "errors": [...], "seed": int|null
}
```

## Running tests

```bash
cd mcp
# All 85 tests (pure-Python + integration; integration auto-skips if tools not built):
python3 -m pytest tests/ -v

# Only the pure-Python tests (no build required):
python3 -m pytest tests/test_pure_python_tools.py tests/test_summarize_model.py -v
```

## ATS binary format (reference)

All values are IEEE 754 float64 (little-endian; auto-detected via magic `123.0`).

```
Header (80 bytes = 10 × f64):
  mag=123.0, sample_rate, frame_size, window_size,
  partials, frames, max_amp, max_freq, duration, type

Per-frame loop (frames iterations):
  time:        1 × f64
  per partial (partials iterations):
    amp:       1 × f64
    freq:      1 × f64
    pha:       1 × f64   (only if type ∈ {2, 4})
  per Bark band (25 iterations):
    band_energy: 1 × f64  (only if type ∈ {3, 4})
```

ATS types:
- **1** — amplitude + frequency only
- **2** — + phase
- **3** — + 25-band noise (Bark critical bands)
- **4** — + phase + noise (full)

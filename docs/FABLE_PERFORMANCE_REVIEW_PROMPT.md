# Fable Model Prompt: ATS Resynth Performance & Correctness Review

Copy everything below the horizontal rule into a Fable session. Attach or open this repository at `/Users/caseywescott/Documents/Projects/ATS-PLUGIN`.

---

## Your role

You are a senior DSP / systems engineer reviewing **ATS Resynth**, a JUCE VST3/AU plugin built on the **Qatsh ATSA** sinusoidal analysis/resynthesis engine. The goal is **maximum practical speed** for:

1. **Analysis** — `tracker()` on imported WAV (worker thread)
2. **Classic render** — `do_synthesis()` playback buffer rebuild (worker thread)
3. **Morph render** — `renderMorphTrajectoryBuffer()` and correspondence building (worker thread)

Also review **Qatsh analysis quality** (frequency/amp/phase estimation, peak tracking, multi-res, DDM slopes) for correctness and further improvement opportunities.

The user wants actionable, implementable findings — not generic advice.

## Hard constraints (do not violate)

- **Real-time safety:** `tracker`, `do_synthesis`, `optimize_sound`, morph render, and MathExpr must **never** run on the audio thread. See `src/core/AtsRtSafety.h` and `docs/adr/0002-ui-stack-threading-and-build.md`.
- **Patch discipline:** Changes to `qatsh/src/atsa/*.c` must ship as new or updated files under `patches/` with CMake marker guards (see root `CMakeLists.txt` and `patches/README.md`). Do not suggest editing qatsh in place without a patch workflow.
- **Determinism:** Offline CLI golden tests (`tests/golden_compare.py`, `tests/estimator_accuracy_test.c`) must stay green unless a change is explicitly flagged as intentional fidelity trade-off.
- **Build type:** Release builds are mandatory for performance work (`CMAKE_BUILD_TYPE=Release`); Debug is 5–20× slower for ATSA.
- **Bit-identical defaults:** `SPARAMS.use_ifft = 0` and `ANARGS.multi_res = 0` must preserve legacy behavior when disabled.

## Repository map (read these first)

### Documentation
- `docs/PERFORMANCE.md` — budgets, O(P×T) vs IFFT synth, soft caps
- `patches/README.md` — **12 applied Qatsh patches** with measured speedups (~11× `tracker()` on arm64)
- `docs/DSP_FIDELITY_PRD.md`, `docs/DSP_FIDELITY_TASKS.md` — estimator/tracking/synthesis fidelity roadmap
- `BUILDING.md`, `docs/DETERMINISM.md`, `COMMANDS.md`

### Qatsh ATSA engine (`qatsh/src/atsa/` — clone qatsh if missing)
| File | Hot path |
|------|----------|
| `tracker.c` | Main analysis: frame loop, multi-res, progress, parallel Phase A/B |
| `peak-detection.c` | Peak pick, QIFFT amp, reassignment, main-lobe gain table |
| `peak-tracking.c` | Serial peak-to-partial linking across frames |
| `other-utils.c` | `fft()` — vDSP on Apple, scalar fallback |
| `residual.c` | Partial resynthesis subtracted from original (`cos()` heavy) |
| `residual-analysis.c` | Per-frame critical-band FFT of residual (FFT size `2×win_size`, not power-of-2) |
| `synthesis.c` | `do_synthesis` (oscillator bank) and `do_synthesis_ifft` (overlap-add) |
| `critical-bands.c` | SMR / masking |
| `atsa.h` | `ANARGS`, `SPARAMS`, `ATS_SOUND` API |

### Plugin orchestration (C++)
| File | Role |
|------|------|
| `src/plugin/PluginProcessor.cpp` | `startAnalysis()`, `renderClassicLayers()`, `runDoSynthesis()`, `renderMorphTrajectoryBuffer()`, worker threading |
| `src/core/AtsParams.{h,cpp}` | `AnalysisParams::toAnargs()`, `SynthParams::toSparams()` — **note: `multi_res` and `use_ifft` not wired** |
| `src/core/AtsMorphRecipe.cpp` | Correspondence strategies, `profileMorphPerformance()` |
| `src/core/AtsMorphTransport*.cpp` | Optimal-transport morph pairing |
| `src/core/AtsSpectralEnvelope.cpp` | Bark envelope for timbre |
| `src/core/AtsMathExpr.cpp` | Batch partial transforms |
| `tools/ats_offline.c` | CLI reference — has `--multi-res` flag |

### Tests / benchmarks
- `tests/estimator_accuracy_test.c` — freq/amp estimator gates
- `tests/multires_resolution_test.c` — multi-res low-band separation
- `tests/ifft_synth_test.c` — IFFT synth vs oscillator reference
- `tests/morph_recipe_test.cpp` — morph performance profiling
- `ctest --test-dir build --output-on-failure`

## What to review

### A. Correctness audit

1. **Threading & lifetime:** Race conditions between `jobGeneration`, `modelLock`, `copy_sound`, playback buffer swap, morph workers. Are there use-after-free or stale-model reads?
2. **Sample-rate handling:** `renderClassicLayers` sets `modelSampleRate` from `core.sampleRate`; verify no double-resample bugs when host SR ≠ analysis SR.
3. **Numerical edge cases:** NaN/Inf propagation in morph render, soft limiting, residual diagnostics.
4. **Parameter plumbing:** Are all analysis APVTS fields mapped in `AnalysisParams::toAnargs()`? Are synthesis params complete?
5. **Qatsh estimator correctness:** Reassignment + QIFFT + DDM interaction; multi-res band merge; phase estimation (`phase_interp` vs reassignment).
6. **Peak tracking:** Order-dependent serial phase — any correctness bugs under parallel Phase A?

### B. Analysis speed (`tracker()`)

Known state (from `patches/README.md` on 10-core arm64, 7.8 s test tone):
- Baseline two-FFT scalar: ~1198 ms
- After all 12 patches: ~110 ms (~11×)

Identify **remaining serial bottlenecks** and new optimizations:

1. `peak_tracking.c` — still serial after parallel frame extraction
2. `build_mainlobe_gain` — ~5 ms warm even with parallel table build; ~4× cost when `multi_res` enabled
3. Residual FFT at `2×window_size` (e.g. 4410) — not power-of-two, bypasses vDSP
4. DDM slope fit per peak — complex inner products
5. Duplicate work when `multi_res` runs a second long-window pass
6. Memory allocation patterns inside frame loop
7. Windows: parallel patches fall back to serial (`_WIN32`)

For each finding: estimate impact (ms or ×), implementation difficulty, determinism risk.

### C. Classic render speed (`do_synthesis`)

1. **IFFT synth not exposed:** `SPARAMS.use_ifft` exists in ATSA but `SynthParams::toSparams()` never sets it. When does IFFT beat oscillator bank? Propose auto-selection heuristic.
2. **Triple synthesis:** `renderClassicLayers()` runs up to **3 full passes** (partials, residual, combined) via `std::async`. Can UI/layer model skip passes?
3. **`copy_sound` before render:** Frequency shape / timbre / morph paths deep-copy entire model. Quantify and propose copy-on-write or in-place scratch.
4. **double→float conversion** in `runDoSynthesis()` — can `do_synthesis` write float directly?
5. **Resample path:** `ResampleLinear.cpp` after synthesis — avoid when unnecessary.

### D. Morph render speed

1. `renderMorphTrajectoryBuffer()` — already uses phasor recurrence (~100–300× vs naive sin). Find remaining hotspots: Bark envelope builds, timbre centroid scan, per-chunk allocations.
2. **Correspondence build:** `buildMorphCorrespondence()` strategies 1–3; OT with `morphQualityToMinMassFraction()`. Profile and propose faster defaults.
3. **`profileMorphPerformance()`** — use its soft caps to recommend UI guardrails.
4. **Smearnov / timeline renders** — duplicate `renderClassicLayers` calls in morph slot paths.
5. **`performanceMacro` parameter** in morph render — is it fully exploited?

### E. Qatsh analysis quality improvements

Without sacrificing the ~11× speed stack, propose:

1. Wire **`multi_res`** to plugin (already in CLI) — bass partial separation
2. Better **two-tone resolution** in crowded low band
3. **Peak tracking** robustness (gaps, SMR thresholds, `track_len`)
4. Optional **analysis presets** (Fast / Default / Quality) mapping to hop, window, SMR, multi_res
5. **`optimize_sound`** — is it called post-tracker? Should it be?
6. Any redundant FFT or SMR work per frame

### F. Plugin-level overhead (non-ATSA)

1. `startAnalysis()` copies entire `sourceMono` vector to analysis thread
2. Float source kept as `double` in `sourceMono` — memory bandwidth
3. Spectrogram UI refresh during analysis
4. MathExpr whole-model passes under lock
5. Excessive `copy_sound` for morph slot snapshots

## Measurement methodology (required in your report)

For every optimization proposal, specify:

1. **Benchmark command** — e.g. `time build/tools/ats_offline --input foo.wav`, or instrument `tracker()` with `std::chrono` in a test harness
2. **Baseline metric** — wall time, CPU samples, or `partials×frames` normalized time
3. **Expected delta** — conservative estimate
4. **Verification** — which `ctest` targets must pass; any new test needed
5. **Platform notes** — Apple Silicon vDSP vs Linux scalar FFT vs Windows serial fallback

If qatsh is not cloned, state assumptions and cite patch README timings.

## Output format

Produce a structured report with these sections:

### 1. Executive summary
- Top 5 highest-ROI optimizations ranked by impact ÷ risk
- Top 3 correctness issues (if any), severity (blocker / major / minor)

### 2. Correctness findings
For each: **ID (C-001…)**, file:line, description, reproduction, recommended fix

### 3. Analysis optimizations
For each: **ID (A-001…)**, bottleneck, proposed change, files, patch vs plugin, estimated speedup, determinism/fidelity risk, test plan

### 4. Render optimizations
**ID (R-001…)** — same fields, split classic vs morph

### 5. Qatsh analysis quality
**ID (Q-001…)** — quality improvement, speed cost, wiring steps

### 6. Plugin overhead
**ID (P-001…)**

### 7. Implementation roadmap
Three phases:
- **Phase 1:** Low-risk plugin-only wins (1–2 days each)
- **Phase 2:** ATSA patches with tests (1 week each)
- **Phase 3:** Research / architectural (parallel peak tracking, float model, etc.)

### 8. Anti-recommendations
Things **not** worth doing and why (e.g. moving analysis to audio thread, removing reassignment for speed without quality gate).

## Success criteria

Your review succeeds if a **junior model** could implement Phase 1 items from your report without guessing file paths or behavior. Be specific: function names, struct fields, APVTS parameter IDs, CMake markers, patch filenames.

Do not write code unless illustrating a non-obvious algorithm. Focus on diagnosis and precise change specs.

# Performance Optimizations — Implementation Guide

**Audience:** A smaller/faster model implementing fixes from the Fable performance review.  
**Companion prompt:** [`FABLE_PERFORMANCE_REVIEW_PROMPT.md`](./FABLE_PERFORMANCE_REVIEW_PROMPT.md)  
**Status:** Initial audit (2025-06-11). Update IDs and checkboxes as work lands.

## How to use this doc

1. Work **top to bottom within each phase** unless a task is blocked.
2. Each task has: **Files**, **Steps**, **Verify**, **Do not**.
3. Run **`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`** before benchmarking.
4. Run **`ctest --test-dir build --output-on-failure`** after every task.
5. Qatsh C changes **must** go through `patches/` + apply scripts (see `patches/README.md`).

## Prerequisites checklist

- [ ] Qatsh cloned: `git clone https://gitlab.com/dxarts/projects/ats/qatsh.git qatsh`
- [ ] All 12 patches applied: run each `scripts/apply_qatsh_*.sh status` (all should report applied)
- [ ] Release build (not Debug)
- [ ] Baseline timing recorded (see §Benchmark harness at bottom)

---

## Phase 1 — Plugin-only wins (low risk, high ROI)

### P1-001 — Wire IFFT synthesis to plugin (auto for dense models)

**Impact:** Large win when `partials × frames` is high (O(P×T) → O(P + N log N) per frame).  
**Risk:** Audible change vs oscillator bank; gated by existing `ifft_synth_test`.

| Field | Value |
|-------|-------|
| Files | `src/core/AtsParams.h`, `src/core/AtsParams.cpp`, `src/plugin/SynthTabPanel.cpp` (optional UI toggle), `src/plugin/PluginProcessor.cpp` (`runDoSynthesis`) |

**Steps:**

1. Add to `SynthParams`:
   ```cpp
   bool useIfftSynth = false;  // default off = legacy oscillator
   ```
2. In `SynthParams::toSparams()`:
   ```cpp
   out.use_ifft = useIfftSynth ? static_cast<short>(1) : static_cast<short>(0);
   ```
3. In `runDoSynthesis()` (or caller), auto-enable when model is dense **unless user explicitly chose oscillator**:
   ```cpp
   // Heuristic: IFFT wins when partial count is large (tune threshold with benchmark)
   constexpr int kIfftPartialThreshold = 64;  // start conservative; benchmark on real material
   if (!sp.useIfftSynth && sound->partials >= kIfftPartialThreshold)
   {
       SPARAMS s {};
       sp.toSparams(s, sound->dur);
       s.use_ifft = 1;
       // call do_synthesis with s ...
   }
   ```
   Prefer exposing an APVTS bool `useIfftSynth` default **Auto** (0=osc, 1=ifft, 2=auto) rather than silent auto-only.
4. Optional: add Synth tab combo "Synthesis engine: Oscillator / IFFT / Auto".

**Verify:**

- `ctest -R ifft_synth_test`
- `ctest -R ats_roundtrip` (with `use_ifft=0` default unchanged)
- Manual: dense piano/analysis type 4 — resynth wall time should drop vs oscillator

**Do not:** Enable IFFT on audio thread. Do not change default to IFFT without an Auto path (bit-identical regression).

---

### P1-002 — Skip redundant synthesis passes in `renderClassicLayers`

**Impact:** Up to **2×** classic resynth when residual exists (currently 3 passes: partials, residual, combined).  
**Risk:** Low if UI only displays combined layer by default.

| Field | Value |
|-------|-------|
| Files | `src/plugin/PluginProcessor.cpp` (`renderClassicLayers`, layer mix in `processBlock` / UI) |

**Steps:**

1. Audit which of `render.partials`, `render.residual`, `render.combined` the UI and `processBlock` actually read.
2. If only **combined** (+ optional solo toggles): replace triple `std::async` with **one** `runDoSynthesis(renderSound, spCombined)`.
3. If solo partials/residual buttons exist: compute partials/residual **lazily** on first solo enable, cache in `ClassicLayerRender`.
4. Keep parallel execution when two passes are genuinely needed.

**Verify:**

- Toggle partials/residual/combined solo in UI — audio unchanged
- Resynth time ~⅓ of prior for models with residual bands

**Do not:** Remove combined pass; it is the main output.

---

### P1-003 — Avoid full `copy_sound` when frequency shape / timbre inactive

**Impact:** Saves deep copy of all partial arrays before every resynth/morph render.  
**Risk:** Low.

| Field | Value |
|-------|-------|
| Files | `src/plugin/PluginProcessor.cpp` (`renderClassicLayers`, morph render entry points) |

**Steps:**

1. In `renderClassicLayers`, the `copy_sound` branches already guard on `isFrequencyShapeActive` and timbre flags — confirm **no copy** when both inactive (early `renderSound = sound`).
2. Search codebase for `copy_sound` in hot paths (`grep copy_sound src/plugin/`).
3. For morph paths that copy "for safety", pass `const ATS_SOUND*` into render if the worker holds a snapshot pointer exclusive to that thread (already done in some smear paths — unify pattern).

**Verify:**

- Resynth with default params — no `copy_sound` in Instruments/time profile
- Frequency shape ON — still applies correctly

---

### P1-004 — Wire `multi_res` analysis to plugin

**Impact:** **Quality** improvement for bass/low partial separation; **cost** ~1 extra long-window pass (slower analysis). Not a speed win — include "Fast analysis" preset with `multi_res=0`.

| Field | Value |
|-------|-------|
| Files | `src/core/AtsParams.h`, `src/core/AtsParams.cpp`, `src/plugin/AnalysisTabPanel.cpp`, `src/plugin/PluginProcessor.cpp` (`readAnalysisParamsFromApvts`) |

**Steps:**

1. Add to `AnalysisParams`:
   ```cpp
   bool multiRes = false;
   ```
2. In `toAnargs()` / `fromAnargs()`:
   ```cpp
   out.multi_res = multiRes ? 1 : 0;
   ```
3. Add APVTS bool parameter `analysisMultiRes` (default false). Mirror CLI `tools/ats_offline.c` lines ~160–212.
4. Add Analysis tab toggle with help text: "Better low-frequency partial separation; slower analysis."

**Verify:**

- `ctest -R multires_resolution_test`
- `golden_offline` unchanged with default off

---

### P1-005 — Analysis / synthesis "performance presets"

**Impact:** User-visible speed without code changes to ATSA.  
**Risk:** Low.

| Field | Value |
|-------|-------|
| Files | `src/plugin/AnalysisTabPanel.cpp`, `src/plugin/PresetLibrary.cpp` (or new factory presets) |

**Steps:**

1. Add factory presets (names illustrative):
   - **Fast:** larger `hop_size` (e.g. 1.5× default), `multi_res=0`, higher `SMR_thres` (fewer peaks)
   - **Default:** current ATSA defaults
   - **Quality:** smaller hop, `multi_res=1`, lower SMR threshold
2. Document hop ↔ speed tradeoff in UI hint (already noted in `AnalysisTabPanel.cpp`).
3. On preset load, do **not** auto-reanalyze unless user clicks Analyze.

**Verify:** Same file analyzed with Fast vs Quality — Fast finishes measurably sooner; Quality has ≥ partial count in low band on bass-heavy test.

---

### P1-006 — Reduce analysis thread memory copy

**Impact:** Medium for long files (full mono duplicated into `std::vector<double> localMono`).  
**Risk:** Medium — threading/lifetime care.

| Field | Value |
|-------|-------|
| Files | `src/plugin/PluginProcessor.cpp` (`startAnalysis`) |

**Steps:**

1. Today: `localMono = sourceMono` copies entire buffer into analysis lambda.
2. Option A (preferred): move analysis to operate on a **shared immutable snapshot**:
   - Under `sourceLock`, increment `sourceGeneration`, capture `const std::vector<double>&` or `shared_ptr<const vector>` to existing storage.
   - Analysis thread reads read-only; import replaces vector only when analysis not running.
3. Option B: pass pointer + length if `sourceMono` lifetime is guaranteed for thread duration (join on cancel).

**Verify:** Analyze 5+ minute file — peak RSS lower; cancel mid-analysis — no crash.

**Do not:** Analyze from `float` buffer without documented precision policy.

---

### P1-007 — Vectorize `runDoSynthesis` double→float copy

**Impact:** Small unless output is millions of samples.  
**Risk:** Very low.

| Field | Value |
|-------|-------|
| Files | `src/plugin/PluginProcessor.cpp` (`runDoSynthesis`) |

**Steps:**

1. Replace scalar loop with JUCE `FloatVectorOperations::convertFixedPoint` or a simple SIMD-friendly loop:
   ```cpp
   for (int i = 0; i < nOut; ++i)
       dst[i] = static_cast<float>(rawOut[i]);
   ```
   → use `juce::FloatVectorOperations::copyWithConversion` if available, or `-O3` autovec is often enough.
2. Consider writing float directly in ATSA (Phase 3) only if benchmark proves copy is hot.

**Verify:** Bit-identical float output within float epsilon vs prior loop.

---

### P1-008 — Morph economy defaults for interactive preview

**Impact:** Fewer correspondence records → faster morph render.  
**Risk:** Audible only at high morph amounts; economy mode already exists.

| Field | Value |
|-------|-------|
| Files | `src/plugin/PluginProcessor.cpp` (`morphQualityToMinMassFraction`, morph preview workers) |

**Steps:**

1. For **live scrubbing** / preview (not bounce), temporarily use `morphQuality=2` (Economy, `minMassFraction=1e-2`) documented in `morphQualityToMinMassFraction()`.
2. On mouse-up / bounce, re-render with user's quality setting.
3. Expose in UI as "Preview quality: Draft / Final" if not already tied to `performanceMacro`.

**Verify:** Scrub morph slider — lower CPU; release — full quality render triggers.

---

## Phase 2 — ATSA patches (medium risk, follow patch discipline)

### A2-001 — vDSP / mixed-radix FFT for residual analysis size

**Impact:** High — `residual-analysis.c` uses FFT size `2×window_size` (e.g. 4410), **not** power-of-two, so it misses vDSP and stays scalar (largest serial cost before residual-parallel patch on some profiles).  
**Risk:** Medium — band energies must match within test tolerance.

| Field | Value |
|-------|-------|
| Files | `qatsh/src/atsa/other-utils.c`, `qatsh/src/atsa/residual-analysis.c`, new `patches/qatsh-atsa-residual-vdsp.patch` |

**Steps:**

1. Add `fft_mixed_radix()` or extend vDSP path for non-POT sizes (vDSP supports factorizations; or pad to next POT with documented energy scaling — **only if** tests allow).
2. Prefer **exact** mixed-radix port of existing scalar FFT for size 4410.
3. Ship as patch; add CMake marker `ATSA_RESIDUAL_VDSP`.
4. Benchmark `tracker()` on material with heavy residual energy.

**Verify:**

- `golden_offline` bit-exact or within documented float tolerance
- `atsa_residual_test`

**Do not:** Change FFT size without updating stored `band_energy` semantics.

---

### A2-002 — Cache / reuse main-lobe gain tables across analyses

**Impact:** ~5–38 ms per analysis (warm) depending on window type/size and multi_res.  
**Risk:** Low if keyed by `(win_type, win_size, mr_win_mult)`.

| Field | Value |
|-------|-------|
| Files | `qatsh/src/atsa/peak-detection.c`, `qatsh/src/atsa/tracker.c`, new patch |

**Steps:**

1. Replace per-`tracker()` call to `build_mainlobe_gain` with memoized table:
   ```c
   /* pseudo */
   static struct { int win_type, win_size; double *table; int len; } cache;
   ```
2. Invalidate only when window params change.
3. Thread-safe: build once under mutex or precompute at plugin startup for known presets.

**Verify:**

- `estimator_accuracy_test` unchanged
- Second analysis with same window — table build skipped (instrument with counter)

---

### A2-003 — Optional skip DDM slopes for "Fast analysis" mode

**Impact:** Removes per-peak 2×2 complex solve in frame loop.  
**Risk:** Fidelity — vibrato/transients less accurate; must be opt-in via `ANARGS` flag.

| Field | Value |
|-------|-------|
| Files | `qatsh/src/atsa/tracker.c`, `atsa.h`, plugin `AnalysisParams` |

**Steps:**

1. Add `ANARGS.skip_ddm_slopes` (or reuse analysis type) — when set, skip `ATSA_DDM_SLOPES` block; `has_slopes=0`.
2. Wire to Fast preset (P1-005).
3. Synthesis falls back to LIN3 (existing behavior when `has_slopes=0`).

**Verify:** `ats_roundtrip` with flag off unchanged; with on — faster tracker, slopes absent.

---

### A2-004 — Parallelize peak tracking (research)

**Impact:** Potentially large — Phase B after parallel frame extraction is still serial.  
**Risk:** **High** — order-dependent; may break determinism and tracking quality.

| Field | Value |
|-------|-------|
| Files | `qatsh/src/atsa/peak-tracking.c`, `tracker.c` |

**Steps:**

1. **Do not implement until Fable/review approves algorithm** (e.g. frame-chunk parallel + merge, or track-level parallelism with locks).
2. Prototype behind `ANARGS.experimental_parallel_tracking=0` default off.
3. Gate on new test comparing partial count/freq RMSE vs serial on fixture corpus.

**Verify:** New test + `golden_offline` on default off.

---

### A2-005 — Call `optimize_sound` after analysis (if not already)

**Impact:** Reduces partial count / memory → faster render & morph.  
**Risk:** Changes model; user may want full partial set.

| Field | Value |
|-------|-------|
| Files | `src/plugin/PluginProcessor.cpp` (`startAnalysis` after `tracker`) |

**Steps:**

1. Grep for `optimize_sound` — if unused after `tracker()`, add optional call:
   ```cpp
   if (ap.optimizeAfterAnalysis)  // new param, default false initially
       optimize_sound(raw, ...);
   ```
2. Expose as "Compact model after analysis" checkbox.
3. Log partial count before/after in debug builds.

**Verify:** Resynth faster; A/B audio acceptable on test corpus.

---

### R2-001 — Parallel IFFT + oscillator selection per layer

**Impact:** When IFFT enabled and residual pass needed, run partials-ifft and residual pass concurrently (same pattern as current triple async).

| Field | Value |
|-------|-------|
| Files | `src/plugin/PluginProcessor.cpp` (`renderClassicLayers`) |

**Steps:** Combine P1-001 and P1-002 — use IFFT for partial-dominant passes; keep residual noise generator path compatible (`ifft_synth_test` notes residual reuses band-noise).

**Verify:** `ifft_synth_test`, layer solos.

---

## Phase 3 — Architectural / research

| ID | Title | Notes |
|----|-------|-------|
| X-001 | Float model storage (`ATS_SOUND`) | Halves memory bandwidth; massive refactor; breaks `.ats` compatibility unless converted at I/O only |
| X-002 | Incremental / region analysis | Analyze selection only — product scope change |
| X-003 | GPU FFT (Metal/CUDA) | Only if CPU FFT still dominates after A2-001 |
| X-004 | Lock-free playback buffer triple-buffer | Micro-optimization for `processBlock` — already cheap |
| X-005 | Windows parallel parity | `_WIN32` falls back to serial in parallel patches — port pthreads → std::thread or Win32 threads |

---

## Correctness fixes to verify (from static audit)

Implement only if Fable review confirms; listed here for tracking.

| ID | Issue | File | Suggested fix |
|----|-------|------|---------------|
| C-001 | `multi_res` not passed to `tracker` from plugin | `AtsParams.cpp` | P1-004 |
| C-002 | `use_ifft` not passed to `do_synthesis` | `AtsParams.cpp` | P1-001 |
| C-003 | Debug build default in some IDE configs | `CMakeLists.txt` | Document; add `#pragma message` in debug plugin builds |
| C-004 | Windows missing parallel speedups | `tracker.c`, patches | X-005 |

---

## Benchmark harness (record baselines before changes)

### CLI analysis + synth

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ats_offline

# Replace with a local WAV path (30–120 s realistic material)
/usr/bin/time -l build/tools/ats_offline --input /path/to/test.wav --output /tmp/out.wav
```

### CTest perf-adjacent tests

```sh
ctest --test-dir build -R 'estimator_accuracy|multires_resolution|ifft_synth|golden' --output-on-failure
```

### Plugin resynth (manual)

1. Load WAV, Analyze (note time from progress bar).
2. Tweak synthesis param — note resynth delay until audio updates.
3. Morph two sources — scrub amount; note CPU in Activity Monitor.

### Optional: instrument `tracker()`

Add temporary timing around `tracker()` in `startAnalysis`:

```cpp
const auto t0 = std::chrono::steady_clock::now();
ATS_SOUND* raw = tracker(...);
const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - t0).count();
DBG("tracker: " << ms << " ms, partials=" << (raw ? raw->partials : 0));
```

Remove or gate behind `#ifdef ATS_PROFILE` before merge.

---

## Patch workflow reminder (for ATSA tasks)

```sh
# Edit qatsh/src/atsa/<file>.c locally, then:
git diff qatsh/src/atsa/ > patches/qatsh-atsa-<name>.patch
# Add scripts/apply_qatsh_<name>.sh mirroring existing scripts
# Add ATS_REQUIRE_QATSH_* marker check in CMakeLists.txt
./scripts/apply_qatsh_<name>.sh reverse-check
ctest --test-dir build --output-on-failure
```

---

## Expected cumulative impact (order-of-magnitude)

| Layer | Current state | After Phase 1 | After Phase 2 |
|-------|---------------|---------------|---------------|
| Analysis (`tracker`) | ~11× vs unpatched baseline (arm64, patches) | User presets + no extra copy: modest | Residual FFT + table cache: ~1.3–2× on top |
| Classic render | 3× synth passes, oscillator O(P×T) | 1× pass + IFFT auto: **2–10×** on dense models | Same |
| Morph render | Phasor-optimized | Economy preview + fewer copies: **1.5–3×** interactive | OT caps tuning |

Numbers are conservative planning estimates — **replace with measured baselines** from the harness above.

---

## Changelog

| Date | Author | Change |
|------|--------|--------|
| 2025-06-11 | Initial audit | Created from codebase review + Fable prompt template; pending Fable pass for C-* confirmations |

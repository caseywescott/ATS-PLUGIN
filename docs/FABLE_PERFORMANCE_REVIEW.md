# ATS Resynth — Performance & Correctness Review

Reviewer: senior DSP/systems pass over the Qatsh ATSA engine + JUCE plugin render paths.
Scope: analysis (`tracker`), classic render (`do_synthesis`), morph render
(`renderMorphTrajectoryBuffer`), correspondence build, Qatsh analysis quality, plugin overhead.

Conventions used below:

- **Build (Release, mandatory for all timing):**
  `cmake -S . -B build-plugin -DCMAKE_BUILD_TYPE=Release && cmake --build build-plugin -j`
- **Test suite:** `ctest --test-dir build-plugin --output-on-failure`
- **Golden gate:** ctest target `golden_offline` (drives `tests/golden_compare.py` against
  `build-plugin/tools/ats_offline`).
- **Analysis bench:** `time ./build-plugin/tools/ats_offline <in.wav> /tmp/out.ats`
  (repeat ≥5, report median; use a dense source — e.g. the crotales material — and a sparse one).
- Reference machine in prior measurements: 10-core arm64 (Apple Silicon), where the 12 applied
  patches already took `tracker()` 1198 ms → ~110 ms on the reference file.
- "Bit-identical" below means the `golden_offline` + `estimator_accuracy_test` gates stay green
  with no regeneration. Anything that is not bit-identical is explicitly flagged.

---

## 1. Executive summary

### Top-3 correctness issues

1. **C-001 — Data race: three concurrent `do_synthesis` passes share mutable global state.**
   `renderClassicLayers` launches partials/residual/combined passes via `std::async`
   (`src/plugin/PluginProcessor.cpp:634-636`). The comment claims the passes are read-only on a
   const model. They are not: each pass calls `srand()`/`rand()` (global libc state), the combined
   pass calls `band_energy_to_res()` which **writes `sound->res[][]`** on the shared `ATS_SOUND`,
   and `synthesis.c` lazily initializes a sine table and band-center statics with non-atomic
   guards. Residual/combined layers are nondeterministic run-to-run and can read torn state.

2. **C-004 / Q-001 — Residual analysis runs a radix-2-only FFT at non-power-of-2 sizes.**
   `residual_get_N()` returns `2*M` unrounded when ≥4096 (e.g. 4410 at 44.1 kHz); the scalar
   fallback in `qatsh/src/atsa/other-utils.c` (`fft_scalar`, simplfft) assumes a power of two
   (`imh = log2`, pow-2 bit reversal). At N=4410 it is deterministic but **not a valid DFT** — the
   25-band residual energies are computed from a mathematically wrong transform. Fixing this also
   makes the residual FFT vDSP-eligible. *Behavior-changing: golden must be regenerated.*

3. **C-003 — `remove_partials`/`add_partial` lose DDM slope alignment.**
   `qatsh/src/atsa/utilities.c` swaps/copies `time/frq/amp/pha/smr/res` but **not
   `frq_slope`/`amp_slope`** when reordering or trimming partials in `optimize_sound`. With
   `has_slopes=1` (DDM patch active, which is the patched default), trimmed/reordered models carry
   chirp slopes belonging to *other* partials into synthesis's cubic-frequency path.

### Top-5 ROI optimizations

| ID | What | Where | Expected win | Exactness |
|----|------|-------|--------------|-----------|
| A-001 | Invert the O(P²·T) track-store loop to O(P·T) | `tracker.c:696-710` | 1.2–2× whole-tracker on dense models; grows with P | Bit-identical |
| R-001 | Loop-interchange + register-resident state in the morph hot loop | `PluginProcessor.cpp:4664-4713` | 2–4× on the dominant partial loop | FP-reassociation only (~1 ulp); no golden coverage |
| A-003 | Hoist `frq2bark`/`amp2db_spl` out of the O(n²) SMR inner loop | `critical-bands.c` (`evaluate_smr`) | 5–20× on the SMR stage | Bit-identical |
| R-005 | Stop rebuilding Bark envelopes per chunk (cache per-frame, lerp log-bins) | `PluginProcessor.cpp:3776-3785, 4303-4304` | Removes ~2 True-Envelope builds + 1 formant align per chunk when env lanes active | Flagged (envelope-of-interp vs interp-of-envelope) |
| A-002 | Binary-search candidates + O(1) claimed-check in peak tracking | `peak-tracking.c` | Phase B from O(P²·T) → O(P log P·T) | Bit-identical if candidate order preserved |

Honorable mention: R-003/R-004 (wire the already-patched, already-tested `use_ifft` and
`multi_res` features into the plugin — they are currently dead code from the plugin's
perspective; `src/core/AtsParams.cpp` never sets either field).

---

## 2. Correctness findings

### C-001 — Concurrent `do_synthesis` passes race on global state

- **Where:** `src/plugin/PluginProcessor.cpp:634-636` (`renderClassicLayers`):
  two `std::async(std::launch::async, ...)` + one inline call into `runDoSynthesis`
  (`PluginProcessor.cpp:160-188`) → `do_synthesis` (`qatsh/src/atsa/synthesis.c`).
- **Races, concretely:**
  1. `srand(ATSA_SYNTH_RANDOM_SEED)` at `synthesis.c:592` and `rand()` inside `randi`/`randif`
     — global libc PRNG state shared by all three threads. Even the partials-only pass calls
     `srand()` on entry, so *all* pairs race.
  2. The combined pass (`amp!=0 && ramp!=0`) calls `band_energy_to_res(ats_sound, f)` for every
     frame, which **writes `sound->res[p][f]`** on the same `ATS_SOUND*` the other two passes are
     reading.
  3. `make_sine_table()` guarded by a plain (non-atomic) `sine_table_ready`; first concurrent
     render can double-initialize / read a half-built table.
  4. File-static `res_band_centers[]` initialization in the residual path.
- **Repro:** load any model with residual; trigger classic render (all three layers) twice in a
  row; diff the residual and combined layer buffers between runs — they differ (rand interleaving)
  even with identical inputs. TSan on a headless harness flags `rand` and `sound->res` writes.
- **Fix (Phase 1, plugin-only):** serialize the three passes (drop the `std::async` pair). Order:
  partials → residual → combined. Cost: classic render wall time ≈ sum instead of max of passes.
- **Fix (Phase 2, patch — restores parallelism):** new patch `ATSA_SYNTH_STATE`:
  per-call PRNG state (explicit LCG seeded with `ATSA_SYNTH_RANDOM_SEED`) threaded through
  `randi`/`randif`; `band_energy_to_res` writes into a caller-provided scratch instead of
  `sound->res`; sine table built once behind `std::call_once`-equivalent (or eagerly at first
  synthesis on one thread). *Determinism flag:* replacing libc `rand()` changes the noise
  sequence → `golden_offline` regeneration required (and it removes a platform-dependence: glibc
  vs macOS `rand()` differ, so current goldens are platform-bound anyway).
- **Verify:** `ctest --test-dir build-plugin -R golden_offline` (after regen for Phase 2 form);
  run-to-run hash equality of the three rendered layers (add a small harness assert).
- **Platforms:** race is UB everywhere; macOS `rand()` is a biased LCG, glibc's is an additive
  generator — symptom severity differs, the bug does not.

### C-002 — IFFT synthesis path dispatched before `srand()`

- **Where:** `qatsh/src/atsa/synthesis.c:583-585` — `if (sparams->use_ifft) return
  do_synthesis_ifft(...)` precedes `srand(ATSA_SYNTH_RANDOM_SEED)` at `:592`.
- **Effect:** the IFFT path's residual noise uses whatever global rand state is current →
  nondeterministic renders on the opt-in path. Default path (`use_ifft=0`) unaffected.
- **Fix:** one-line patch (move the `srand` above the dispatch, or seed inside
  `do_synthesis_ifft`). Extend the existing `qatsh_ifft` patch.
- **Verify:** `ctest -R ifft_synth_test` plus a new determinism assert (render twice, memcmp).
  `golden_offline` unaffected (defaults bypass IFFT).

### C-003 — DDM slope arrays not maintained by `remove_partials`/`add_partial`

- **Where:** `qatsh/src/atsa/utilities.c:577-588` (`remove_partials` swap block) and
  `add_partial` (both the memcpy branch and the memset-else branch) omit `frq_slope`/`amp_slope`.
- **Effect:** with `has_slopes=1`, any model where `optimize_sound` trims or reorders partials has
  slope rows mis-bound to the wrong partials. Synthesis's Fritsch-Carlson-limited cubic frequency
  then applies another partial's chirp rate — audible as wrong vibrato/glide on affected partials.
- **Repro:** analyze a source where `trim_partials` removes ≥1 partial; dump
  `(frq[p][f], frq_slope[p][f])` pairs before/after `optimize_sound`; slopes follow the *old*
  row index, not the partial.
- **Fix:** include both slope arrays (guarded by `sound->has_slopes` / non-NULL) in the swap, the
  copy, and the zero-fill. Extend the `qatsh_ddm` patch. Bit-identical when `has_slopes=0`.
- **Verify:** `ctest -R estimator_accuracy_test` (chirp cases); add a unit check that
  slope-row identity survives a forced trim. `golden_offline`: .ats files don't carry slopes, so
  unaffected; rendered output with DDM enabled changes (it was wrong before — flag in notes).

### C-004 — Residual FFT at non-power-of-2 N is not a valid transform

- **Where:** `residual_get_N(M, 4096, 2)` in `qatsh/src/atsa/residual-analysis.c` returns `2*M`
  unrounded when `2*M ≥ 4096` (44.1 kHz default window → N=4410); `fft()` in
  `qatsh/src/atsa/other-utils.c` routes non-pow-2 to `fft_scalar`, which is radix-2-only
  (`imh = (int)(log(n+1)/log(2))`, pow-2 bit-reversal). N=4410 silently produces garbage-adjacent
  output that the band-energy integration then consumes.
- **Effect:** 25-band residual energies are systematically wrong at common sample rates (anything
  where `2*win_size` isn't a power of two). The error is deterministic, so it has been invisible
  to golden testing — the goldens encode the wrong transform.
- **Fix:** round N up to the next power of two and zero-pad in `residual_get_N` (new patch
  `ATSA_RESIDUAL_POW2`). Side benefit: the residual FFT becomes vDSP-eligible on macOS
  (`qatsh_vdsp_fft` patch covers pow-2 only) → residual-analysis speedup for free.
- **Determinism flag:** **intentional fidelity fix.** `golden_offline` and any `.ats` band-energy
  comparisons must be regenerated. Add a one-off validation: analyze white noise; band energies
  should follow the critical-band bandwidth profile — they don't today, they will after.
- **Verify:** `ctest -R atsa_residual_test` (update expectations), white-noise sanity test,
  regenerate goldens.
- **Platforms:** pow-2 rounding also fixes Windows/Linux (scalar path) — currently *all*
  platforms are wrong; macOS additionally gains vDSP.

### C-005 — Peak identity matched by float equality (minor)

- **Where:** `qatsh/src/atsa/peak-tracking.c:55` — `peaks[i].frq == matched_peak->frq`.
- **Effect:** benign today (`matched_peak` aliases the same array), but two peaks with identical
  frequency would both "match." Replace with index-based identity when touching A-002.
  Bit-identical.

---

## 3. Analysis optimizations

### A-001 — Track-store loop: O(P²·T) → O(P·T), bit-identical

- **Where:** `qatsh/src/atsa/tracker.c:696-710`:
  `for(k=0..n_partials) for(frame) for(i<n_peaks) if(peak.track==k) store;`
  Because killed tracks re-push zero-amp copies every frame, `n_peaks == n_partials` in every
  frame (set explicitly at `tracker.c:659`), so this is exactly P²·T.
- **Fix:** invert: `for(frame) for(i<n_peaks) { k = peak.track; store into row k; }`. Each
  `(k,frame)` cell is written exactly once with the same value → **bit-identical**.
- **Benchmark:** `time ./build-plugin/tools/ats_offline crotales-source.wav /tmp/o.ats`
  (dense, high-P case) vs a sparse vocal. Baseline: tracker ~110 ms on the arm64 reference; the
  store loop's share scales with P, so expect 1.2–2× whole-tracker on dense material, ~nil on
  sparse. Instrument with a `clock_gettime` pair around the store loop for the direct number.
- **Verify:** `golden_offline` green with **no** regeneration.

### A-002 — Phase B tracking data structures

- **Where:** `qatsh/src/atsa/peak-tracking.c` —
  `find_candidates` does a linear scan although peaks are frequency-sorted;
  `find_best_candidate` re-scans all tracks per candidate to test "claimed";
  `update_tracks` is O(tracks × track_len × peaks_per_frame).
- **Fix:** (a) binary-search the `[frq·(1−dev), frq·(1+dev)]` window (peaks sorted);
  (b) per-frame `claimed[]` flag array indexed by peak, plus a track→matched-peak index;
  (c) per-frame track→peak index map for `update_tracks`.
  Keep iteration order ascending so candidate selection ties break identically →
  **bit-identical** (verify on goldens; if any tie-break divergence shows up, keep the linear
  scan *inside* the binary-searched window).
- **Benchmark:** same `ats_offline` timing; Phase B is serial (post-`qatsh_parallel`), so this is
  the long pole once Phase A is parallel. Expect Phase B ~P/log P faster on dense models.
- **Verify:** `golden_offline`, `estimator_accuracy_test`.

### A-003 — SMR evaluation: hoist per-peak conversions out of the O(n²) loop

- **Where:** `qatsh/src/atsa/critical-bands.c` (`evaluate_smr`) — `frq2bark()` (2×atan) and
  `amp2db_spl()` (log10) recomputed for both peaks in the inner pair loop.
- **Fix:** one pass computing `bark[i]`, `db[i]` per peak, then the pair loop uses the arrays.
  Same arithmetic on the same values → **bit-identical**.
- **Benchmark:** instrument the SMR stage; with hundreds of peaks/frame this is thousands of
  transcendentals per frame today. Expect 5–20× on the stage; whole-tracker effect a few percent
  on sparse, more on dense.
- **Verify:** `golden_offline` (SMR feeds track scoring and the stored `smr` plane).

### A-004 — Allocation churn in the frame stage

- **Where:** `to_polar` (peak-detection.c) mallocs magnitude/phase buffers per call;
  `push_peak` (other-utils.c) reallocs per element.
- **Fix:** fold both into the existing `FrameScratch` (tracker.c Phase A already has per-thread
  scratch); geometric growth for peak lists. Bit-identical.
- **Benchmark:** small (likely 2–5% of frame stage); take it while touching the files.

### A-005 — Dead-track culling (flagged, Phase 3)

- Tracks are never removed; killed tracks re-push zero-amp copies every frame, which is *why*
  A-001's loop is quadratic and why P only grows. Culling dead tracks (or capping the re-push
  horizon) changes gap-filling and the final partial set → **behavior-changing**; only worth
  doing after A-001/A-002 land, and only with golden regeneration + listening checks.

---

## 4. Render optimizations

### Classic render

### R-002 — Serialize, then re-parallelize `renderClassicLayers` safely

- Phase 1: fix C-001 by serializing (correctness first). Recover the lost wall time by rendering
  **lazily**: `makePlaybackBufferFromClassicLayers` (`PluginProcessor.cpp:678-718`) already falls
  back to partials+residual when combined is absent, but note the semantics differ (the combined
  pass uses per-partial noise via `band_energy_to_res`, not the 25-band model), so the combined
  layer must still be rendered when the user auditions "both" — render it on first demand
  instead of eagerly.
- Phase 2: with the `ATSA_SYNTH_STATE` patch (C-001), restore the `std::async` fan-out — it
  becomes genuinely safe and the comment becomes true.
- Also: replace the per-sample double→float loop at `PluginProcessor.cpp:181-184` with
  `vDSP_vdpsp` (macOS) / `std::transform` (portable). Minor but free.
- **Benchmark:** wall-clock around `renderClassicLayers` for a 10 s model; baseline = current
  async version (racy), compare serialized vs lazy vs re-parallelized.

### R-003 — Wire `SPARAMS.use_ifft` (the IFFT synthesis patch is dead code in the plugin)

- **Where:** `src/core/AtsParams.cpp` — `SynthParams::toSparams` memsets SPARAMS and never sets
  `use_ifft`; the Rodet-Depalle overlap-add path (O(P + N log N) vs O(P×T)) is only reachable
  from tests. For dense models (P in the hundreds) it is the single biggest classic-render lever.
- **Fix (plugin-only):** add a `SynthParams::useIfft` field + APVTS toggle (or auto-enable above
  the `partials×frames > 2,000,000` soft cap from `docs/PERFORMANCE.md`), set it in `toSparams`.
  Fix C-002 first. Default **must stay 0** (bit-identical constraint).
- **Benchmark:** render a dense model both ways; expected order-of-magnitude on P≳300 material.
- **Verify:** `ctest -R ifft_synth_test`; A/B null test partials layer (IFFT path is an
  approximation — document the dB floor, don't claim bit-exactness).

### R-004 — Wire `ANARGS.multi_res` (same situation on the analysis side)

- `AnalysisParams::toAnargs` never sets `multi_res`/`mr_win_mult`/`mr_crossover`;
  `tools/ats_offline.c:211-214` shows the intended wiring (`ATSA_MRWINMULT`, `ATSA_MRCROSS`).
  Plugin users cannot reach the multi-res front-end at all. Add an "Analysis: Multi-resolution"
  toggle; default off (bit-identical constraint). Verify with `ctest -R multires_resolution_test`.

### Morph render

### R-001 — Hot-loop interchange + struct-of-arrays

- **Where:** `PluginProcessor.cpp:4664-4713`. Current shape: per *sample*, iterate active
  partials; each iteration loads/stores `p.re/p.im/p.amp` and reads `rotRe/rotIm/ampInc` through
  an index indirection (`activeP[ai]`) — ≈48–64 bytes of state traffic per partial per sample.
  At 300 active partials × 44.1 kHz that is >1 GB/s of L1/L2 traffic per rendered second, and the
  phasor's loop-carried dependency means the CPU stalls on it.
- **Fix:** interchange loops — per partial, run the entire chunk (one ATS hop, typically
  256–2048 samples) accumulating into a chunk-local accumulation buffer; phasor state stays in
  registers for the whole inner loop. Then unroll 2–4 partials per pass to hide the dependency
  chain. Optional second step: move partial state to SoA arrays (`re[]`, `im[]`, `rot_re[]`, …)
  so the multi-partial unroll vectorizes (NEON 2×double / 4×float).
- **Exactness:** per-sample sums become per-partial accumulation — same addends, different FP
  association order → ~1 ulp differences. The morph path has no golden gate; verify with a null
  test (old vs new render < −120 dBFS peak) and `morph_metric_harness` tolerances.
- **Benchmark:** wall-clock around `renderMorphTrajectoryBuffer` for a 10 s morph of two dense
  sources (log added temporarily or via Instruments "time profiler"). Expect 2–4× on the partial
  loop; the residual carrier loop (125 carriers max) benefits identically.
- **Platforms:** wins everywhere; SoA+NEON step is arm64-tuned but the interchange alone is
  portable and is most of the win.

### R-005 — Bark envelope cache misses on every chunk of a moving timeline

- **Where:** `buildBarkEnvelopeCached` keys on `(frame0, frame1, fracBits)` with `fracBits` the
  *exact double bits* (`PluginProcessor.cpp:3783-3785`). During a timeline render the fractional
  position is unique per chunk → 100% miss; every chunk with env-transfer/env-morph active does
  two full builds (`PluginProcessor.cpp:4303-4304`): O(P) point seeding + up to 32 True-Envelope
  iterations × O(order×64) naive DCT each, plus `FormantAlignedEnvelopeMorph::build`
  (`AtsSpectralEnvelope.cpp:467`). The cache only helps exact re-renders.
- **Fix options, in preference order:**
  1. Cache **per-frame** envelopes (frac=0 only, ≤frames entries per source per revision) and
     lerp the cached `logBins` by frac per chunk (O(64) per chunk). *Flagged:* interpolating
     envelopes ≠ envelope of interpolated partials; differences are small (envelopes are smooth
     by construction) but not zero — validate with `morph_metric_harness` and listening.
  2. Quantize frac in the key (e.g. 1/32) — smaller change, same flag, smaller win.
  3. Keep exact keys for the fixed-morph preview (already hits), apply (1) only to timeline mode.
- **Benchmark:** time a 10 s timeline render with EnvTransfer=1 before/after; instrument
  build count (expect: from ~2×chunks builds → ≤2×frames total, then ~0 on re-render).

### R-006 — `copy_sound` deep copies on the snapshot path

- **Where:** call sites at `PluginProcessor.cpp:576, 593, 2653, 3239, 3251, 5403, 5566, 5590,
  5908`; `copy_sound` (`utilities.c`) duplicates 8 P×T double planes + 25 band-energy rows.
- Freq-shaping mutates only `frq` (and slopes); timbre shaping only `amp`. A plugin-side
  "selective snapshot" that deep-copies only the mutated planes and *owns* shallow references to
  the rest needs a non-owning wrapper around `ATS_SOUND` (free path frees every plane —
  lifetime-sensitive). Medium risk, medium reward; do after Phase 1, only if profiling shows
  snapshot cost matters for large models (it is O(P×T×8×8 bytes) — a 500×2000 model is ~64 MB
  per copy).
- Note `copy_sound` has the same slope-array omission as C-003 — audit it in the same patch.

### R-007 — Per-chunk re-pair slice mapping is O(k²) (minor)

- `PluginProcessor.cpp:4341-4348`: mapping current assignment to slice index is a nested scan per
  band group. k per band is small (usually <16) so this is noise; fix only if profiling disagrees.

### Correspondence build

- `buildOptimalTransportPlan` (`src/core/AtsMorphTransport.cpp:48`) is the 1-D
  north-west-corner sweep: O((m+n) log(m+n)) from the sorts, exact for convex line costs.
  **Already optimal — no action.** The OT defaults (`morphQualityToMinMassFraction`,
  `PluginProcessor.cpp:253-262`) are well-tuned per the documented sweep (knee at 1e-5, 1e-3
  inaudible). The expensive part of `rebuildMorphCorrespondenceIfReady` is upstream assignment
  building, which is cached for the live-smear path (`smearedTimeline`,
  `PluginProcessor.cpp:4929-4950`) — the non-smear path could adopt the same revision-keyed
  cache, but profile first; it only runs on play/recipe changes.

---

## 5. Qatsh analysis quality

### Q-001 — Residual band energies are wrong at non-pow-2 N

Same as **C-004**; filed there. Quality impact: residual layer level/spectral balance is
systematically off at 44.1/88.2 kHz family rates. The fix is also a speed win (vDSP).

### Q-002 — Multi-resolution front-end unreachable from the plugin

Same as **R-004**. The `qatsh_multires` patch (long low-band window below `mr_crossover`=500 Hz)
is tested (`multires_resolution_test`) and pays for itself on bass-heavy material; it is simply
not wired. Pure plumbing.

### Q-003 — Slope-aware peak tracking (research, behavior-changing)

`peak_tracking` matches candidates on instantaneous `|Δfrq|` only. DDM slopes are already
computed (`ddm_slopes`, tracker.c) — predicting each track's frequency at the next frame
(`frq + frq_slope · hop_seconds`) before the window test is the standard McAulay-Quartieri
extension and materially improves continuation through fast chirps (fewer track splits → fewer
partials → faster store/synthesis too). **Flag:** changes track topology → golden regeneration +
`estimator_accuracy_test` chirp cases as the quality gate. Phase 3.

### Q-004 — `ddm_slopes` inner products via phasor recurrence

Each peak's DDM solve does O(win_size) `cos`/`sin` calls. A phasor recurrence (one complex
multiply per sample) removes the transcendentals. **Flag:** FP drift over a 2048-sample window is
~1e-12 relative — not bit-identical. Gate on `estimator_accuracy_test` tolerances rather than
goldens, or split the recurrence into 256-sample re-seeded segments to stay within 1 ulp.
Phase 2/3 boundary; only worth it if profiling shows `ddm_slopes` hot after A-001/A-002.

### Q-005 — Estimator stack is otherwise sound

Parabolic interp + Auger-Flandrin reassignment + QIFFT main-lobe gain correction (1025-point
G(δ) table) + Fritsch-Carlson-limited cubic synthesis frequency is a literature-correct chain;
the gain table's linear interpolation error is far below the estimators' own bias. No action.

---

## 6. Plugin overhead

### P-001 — `startAnalysis` copies the whole source under `sourceLock`

- `PluginProcessor.cpp:2500`: `localMono = sourceMono` — full `std::vector<float>` copy while
  holding the lock. Store the source as `std::shared_ptr<const std::vector<float>>`; swap the
  pointer under the lock, let the worker hold a reference. Removes an O(N) copy + long lock hold
  (a 60 s 48 kHz file is ~11 MB copied per analysis kick).

### P-002 — `playMorphPreview` renders synchronously on the calling thread

- `PluginProcessor.cpp:4721-4826` (render at `:4774`): the full `renderMorphTrajectoryBuffer`
  runs on whatever thread calls it (UI button → message thread), freezing the editor for the
  render duration. `playMorphTimelinePreview` already does this correctly on `transformThread`
  (`:4907`). Move the preview render onto the same worker pattern (generation-checked, state via
  the existing atomics). No audio-thread implication — this is UI responsiveness.

### P-003 — Avoidable full-buffer copies at publish time

- `playbackBuffers[...].makeCopyOf(timelineBuffer)` (`:5099`) and `.makeCopyOf(rendered)`
  (`:4796`) duplicate a buffer that is already thread-local — `std::swap` /
  `buffer = std::move(...)` after the readers-drained spin. One large memcpy per render; trivial.

### P-004 — Spin-yield on `readingPlaybackBufferIndex` (note only)

- `:4793, :5087` — bounded by one audio callback, generation-checked; acceptable. Don't "fix."

### P-005 — `joinWorkers()` before timeline render blocks the caller

- `:4892` runs on the calling thread before `transformThread` spawns. If invoked from the
  message thread while a long analysis runs, the UI stalls until join. Consider gating with the
  same `isWorkerBusy()` early-out used elsewhere (it is checked at `:4841`, so the residual risk
  is a race between check and join — small; note only).

---

## 7. Implementation roadmap

### Phase 1 — plugin-only, no qatsh patches, no golden changes

| Step | Item | Files | Gate |
|------|------|-------|------|
| 1.1 | C-001 mitigation: serialize the three `do_synthesis` passes (delete the two `std::async` at `PluginProcessor.cpp:634-636`, call sequentially) | `src/plugin/PluginProcessor.cpp` | run-to-run layer hash equality; full `ctest` |
| 1.2 | R-002 lazy combined-layer render (render combined on first "both" audition) | `PluginProcessor.cpp` (`renderClassicLayers`, `makePlaybackBufferFromClassicLayers`) | audition all three modes; null-check vs eager render |
| 1.3 | R-003 wire `use_ifft` as opt-in APVTS toggle, default off | `src/core/AtsParams.{h,cpp}`, param layout, `PluginProcessor.cpp` | `ifft_synth_test`; default-off ⇒ `golden_offline` untouched |
| 1.4 | R-004 wire `multi_res` (+`mr_win_mult`, `mr_crossover` defaults `ATSA_MRWINMULT`/`ATSA_MRCROSS`) as opt-in toggle, default off | same | `multires_resolution_test`; default-off ⇒ goldens untouched |
| 1.5 | R-001 morph hot-loop interchange (+optional SoA/unroll) | `PluginProcessor.cpp:4664-4713` | null test old-vs-new < −120 dBFS; `morph_metric_harness` |
| 1.6 | P-001 shared_ptr source snapshot; P-002 move preview render to worker; P-003 move-instead-of-copy publishes | `PluginProcessor.cpp/.h` | full `ctest`; manual UI smoke (analyze, preview, timeline, bounce) |
| 1.7 | vDSP_vdpsp double→float in `runDoSynthesis` | `PluginProcessor.cpp:181-184` | bitwise-equal float output on a test vector |

Every step: Release build, `ctest --test-dir build-plugin --output-on-failure` green,
`golden_offline` green **without regeneration**.

### Phase 2 — ATSA patches (each = patch file under `patches/` + marker guard + apply script + `-DATS_REQUIRE_QATSH_*` CMake check, per `patches/README.md` conventions)

| Step | Item | Patch | Golden impact |
|------|------|-------|---------------|
| 2.1 | C-002 srand-before-IFFT-dispatch | extend `qatsh_ifft` | none (non-default path) |
| 2.2 | C-003 slope arrays in `remove_partials`/`add_partial` (+ audit `copy_sound`, `fill_sound_gaps`) | extend `qatsh_ddm` | none for `.ats`; DDM renders change (were wrong) |
| 2.3 | A-001 store-loop inversion | new `qatsh_trackstore` | none (bit-identical) |
| 2.4 | A-003 SMR precompute | new `qatsh_smr_precompute` | none (bit-identical) |
| 2.5 | A-002 tracking data structures | new `qatsh_tracking_ds` | none expected; verify tie-breaks on goldens before merging |
| 2.6 | A-004 scratch reuse (`to_polar`, `push_peak`) | fold into 2.3/2.5 | none |
| 2.7 | C-001 root fix: `ATSA_SYNTH_STATE` (per-call PRNG, scratch `res`, safe sine-table init), then restore `renderClassicLayers` parallelism | new `qatsh_synth_state` | **regenerate** (`rand` sequence changes — flag as intentional; also de-platform-binds goldens) |

### Phase 3 — research / behavior-changing (each requires golden regen + listening sign-off)

1. **C-004/Q-001** pow-2 residual N (`qatsh_residual_pow2`) — correctness-motivated; do first in
   this phase since it re-baselines residual goldens that 3.x items would touch anyway.
2. **R-005** per-frame envelope cache + log-bin lerp for timeline renders.
3. **Q-003** slope-aware tracking prediction.
4. **A-005** dead-track culling (after A-001/A-002 — measure first; they may make it moot).
5. **Q-004** DDM phasor recurrence (only if still hot).

Measurement discipline for every phase: median of ≥5 Release runs, dense + sparse fixture, record
machine/thread count; tracker stage timings via the existing `progress_cb` hooks or temporary
`clock_gettime` brackets; morph/classic render via wall-clock brackets around the public calls.

---

## 8. Anti-recommendations

1. **Do not move any render onto the audio thread** — `tracker`, `do_synthesis`,
   `optimize_sound`, morph render, MathExpr are model-thread-only (`src/core/AtsRtSafety.h`,
   ADR 0002). Nothing in this report changes that; P-002 moves work *off* the message thread,
   not onto the audio thread.
2. **Do not vectorize the phasor across samples.** The recurrence is loop-carried; forcing SIMD
   across time needs angle-addition unrolling that accumulates FP drift and re-introduces trig.
   Vectorize across *partials* (R-001) instead.
3. **Do not parallelize Phase B tracking.** Track assignment is order-dependent frame-to-frame by
   construction (serial confirmation in the `qatsh_parallel` patch was deliberate). A-002 makes it
   fast while keeping it serial.
4. **Do not edit `qatsh/src/atsa/*.c` in place.** Every engine change ships as a `patches/` entry
   with marker guards, or it will be silently lost on vendor refresh and unverifiable by the
   `ATS_REQUIRE_QATSH_*` CMake checks.
5. **Do not flip `use_ifft` or `multi_res` defaults.** Bit-identical default behavior is a hard
   constraint; both ship opt-in (R-003/R-004).
6. **Do not "fix" C-001 by giving each async pass its own `copy_sound`.** It removes the
   `sound->res` write race but the `srand`/`rand` and sine-table races remain — and it triples
   peak memory. Serialize (Phase 1) or patch the state (Phase 2.7); nothing in between is sound.
7. **Do not switch the residual FFT size or the synthesis PRNG silently.** Both change golden
   output; they are correct changes (C-004, 2.7) but must land with explicit regeneration commits
   and a note in `patches/README.md`, never as a side effect.
8. **Do not micro-cache `BarkSpectralEnvelope::sampleAtHz` or shrink `kDefaultBins`(64).** The
   per-call cost is two atans and a lerp; the real cost is the per-chunk *build* (R-005).
   Reducing bins or lifter order trades audible envelope fidelity for negligible time.
9. **Do not replace the 1-D OT solver.** The north-west-corner sweep on sorted marginals is exact
   and near-linear; entropic/Sinkhorn or general LP solvers would be slower and approximate.
10. **Do not benchmark in Debug** (5–20× slower, different inlining) or with the UI editor open
    (paint traffic skews message-thread measurements).

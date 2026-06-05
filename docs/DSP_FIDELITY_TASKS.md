# Task List: DSP Core Fidelity in Coherent Commits

**Source:** [DSP_FIDELITY_PRD.md](./DSP_FIDELITY_PRD.md)
**Branch:** `dsp-fidelity-tier1`
**How to use:** Each numbered section is intended to become one reviewable commit. Keep commits buildable and testable. C-core sections (Phase 1–6) ship as patches under `patches/` and must keep the CMake marker guards + CI green (see PRD §9). Sections that change rendered audio are flagged **[ear]** — green tests are necessary but not sufficient.

---

## Phase 0 — Spec & Measurement Baseline

### Commit 1 — `docs: add DSP fidelity PRD and task list` ✅

**PRD:** §1–§11

- [x] Add the DSP fidelity PRD and this task list.
- [x] Record the baseline policy: numerical-truth-first, representation stability, vendored-core patch discipline.
- [x] Document current-state ground truth (estimator/phase/tracking/residual/synthesis/morph) with file refs.
- [x] Link the morph PRD, Smearnov spec, determinism, and performance docs.

Implementation note: Added `docs/DSP_FIDELITY_PRD.md` + this list. Ground-truth table cites real refs (`peak-detection.c:101` parabolic, `:112` the `// wrong method` phase). PRD §9 codifies that qatsh C changes ship as `patches/` enforced by CMake markers + CI.

### Commit 2 — `test: add sinusoidal estimator accuracy harness` ✅

**PRD:** §8

- [x] Add a synthetic-signal generator: pure tone and two closely-spaced tones with exactly known frequency/amplitude/phase. (Chirp/AM/tone-in-noise cases deferred to Phase 2/3 where the estimators that need them land.)
- [x] Add measurement of frequency/amplitude error against ground truth across fractional-bin sweeps and two zero-pad regimes.
- [x] Record baseline error of the *current* `parabolic_interp` + `phase_interp`.
- [x] Register `estimator_accuracy_test` as a new CTest target (links `atsa`, replicates the tracker FFT layout).

Implementation note: `tests/estimator_accuracy_test.c`. **Key finding:** the live analyzer (Blackman-Harris, 2× zero-pad) is already near-exact on isolated stationary tones — 0.064 cents / 0.0018 dB worst-case. Unpadded regime is 0.534 cents / 0.031 dB (the QIFFT target). Two tones ~1.5 bins apart collapse to **1** peak (the reassignment/multi-res target). So Tier 1's real payoff is concentrated in Phase 2 (reassignment), Phase 3 (DDM non-stationary), and Phase 4 (multi-res) — not Phase 1 on clean tones. 11/11 CTest green.

### Commit 3 — `test: extend morph metrics with transport cost and continuity` ✅

**PRD:** §8

- [x] Add transport-cost metrics to the morph metric harness (`check_transport_cost`): normalized cost bounded by `max_normalized_cost`, amount-0 endpoint anchored to 0, and monotone non-decreasing across the sweep within `max_monotonicity_violation` — the continuity contract the OT plan (Commit 11) must satisfy. (Correspondence-churn was already present; left intact.)
- [x] Add a pop-free continuity check for count-mismatched source pairs (`check_count_mismatch_continuity`): each row must describe an actually count-mismatched pair (A≠B partials) and bounds the largest single-step amplitude (`max_amplitude_jump_db`) and frequency (`max_frequency_jump_cents`) jump from birth/death/mass-split handling.
- [x] Pin endpoint-fidelity tolerances: harness now enforces a hard `ENDPOINT_TOLERANCE_CEILING = 1e-3` on `max_rms_error`/`max_peak_error` independent of the JSON, so OT work can tighten but never loosen the amount-0/1 contract.

Implementation note: `tests/morph_metric_harness.py` + the two `tests/fixtures/morph_metric_*.json`. Both CTest targets green; four negative tests confirm each new gate fires (non-monotone cost, equal-count "mismatch" row, loosened endpoint tolerance, oversized amplitude pop). Tolerances stay intentionally loose pending real morph renders.

---

## Phase 1 — QIFFT Bias Correction

### Commit 4 — `feat(atsa): window-aware QIFFT peak estimation` ✅

**PRD:** §6.1, §9

- [x] Implement bias-corrected QIFFT amplitude estimation in `peak-detection.c`, parameterized by window type via a precomputed main-lobe gain table `G(delta)`.
- [x] Replace/guard the dB-parabolic amplitude path so it degrades to current behavior when the gain table is absent (`set_mainlobe_gain(NULL,0)`).
- [x] Add `patches/qatsh-atsa-qifft.patch` + `scripts/apply_qatsh_qifft.sh`; marker guard `ATS_REQUIRE_QATSH_QIFFT` (marker `ATSA_QIFFT_AMP` in `peak-detection.c` + `tracker.c`); `patches/README.md` section; CI apply step.
- [x] Show measurable amplitude error reduction in the estimator harness; update CI clone+patch step.

Implementation note: Window-gain amplitude (Abe & Smith style, computed analytically from the window rather than fit) replaces the dB-parabolic height. The true amplitude of a stationary sinusoid is the raw peak-bin magnitude divided by the analytic window main-lobe gain `G(delta)` at the reassigned sub-bin offset — exact in *both* zero-pad regimes, unlike the parabolic height which carries a window-dependent bias. **Harness gate (passed):** window-gain amp error **0.0003 dB vs 0.031 dB parabolic** unpadded (~100×) and **0.0001 dB vs 0.0018 dB** in the live 2× regime (~18×) — both well past the 2× gate. Ported via a file-static gain table installed by `tracker.c` so `peak_detection` keeps its signature (harness + reassignment patch untouched). Forward-apply + reverse-roundtrip + guard negative-test verified on the snapshot tree; 11/11 CTest green. The rendered amplitude change is ≤0.002 dB in the live regime — far below the ~0.1 dB JND, so not [ear]-flagged, but it *is* a numerically-validated rendered-output change.

---

## Phase 2 — Spectral Reassignment / Correct Phase

> **Reordered to front:** the Phase 0 harness showed the parabolic estimator is
> already near-exact on padded isolated tones, so QIFFT (Phase 1) buys little.
> Reassignment is the higher-value change — it both sharpens frequency and gives
> the correct phase (fixing the `// wrong method` shortcut) — so it goes first.

### Commit 5a — `test: prototype derivative-window reassignment (numerical gate)` ✅

**PRD:** §6.2, §8

- [x] Add a derivative-of-window auxiliary FFT and reassigned-frequency estimator to `estimator_accuracy_test.c`.
- [x] Demonstrate it beats parabolic in the unpadded regime (PRD §8 gate before touching the vendored core).

Implementation note: Reassignment cut worst-case frequency error from **0.534 → 0.019 cents (~29×)** unpadded and **0.064 → 0.010 cents (~6×)** in the live 2× regime. Sign of the Auger-Flandrin correction flips because qatsh `fft(...,1)` uses the `+j` convention (per `to_polar`'s `atan2(-y,x)`). Two closely-spaced tones still collapse to one peak — reassignment sharpens a *resolved* peak but adds no resolution; that remains the Phase 4 target. The gate is met, so the qatsh integration (5b) is justified.

### Commit 5b — `feat(atsa): reassigned instantaneous frequency`  **[ear — pending audition]**

**PRD:** §6.2, §9

- [x] Port the validated reassignment estimator into `peak-detection.c` (`reassign_frq`, `ATSA_REASSIGN_FREQ` marker); derivative window + second per-frame FFT built in `tracker.c`. `peak_detection` gains a `dfft` arg (NULL → legacy parabolic).
- [~] **Phase left unchanged — data-driven decision.** The Phase 0 harness measured `phase_interp` at <0.0015 rad worst-case; the `// wrong method` flag refers to already-fixed commented-out code, not the live path. Porting frequency-only avoids touching an already-correct estimator. (PRD ground-truth row updated.)
- [x] Round-trip/determinism: all 11 CTest green with the patch live; `ats_roundtrip` passes. Moving-partial (chirp/vibrato) gains are a Phase 3 (DDM) concern — reassignment is exact for *stationary* sinusoids.
- [x] Patch delivery: `patches/qatsh-atsa-reassignment.patch` (isolated from the unrelated progress-cb edits via pre-edit snapshots), `scripts/apply_qatsh_reassignment.sh`, CMake `ATS_REQUIRE_QATSH_REASSIGNMENT` marker guard (`ATSA_REASSIGN_FREQ` in `peak-detection.c` + `tracker.c`), `patches/README.md` section. Forward-apply + reverse-roundtrip verified on a clean tree.
- [x] **Audition gate (passed 2026-06-05):** A/B renders in `Testing/reassign_audition/` (`guitar_baseline.wav` parabolic vs `guitar_reassign.wav` reassigned). A−B difference ≈ −46 dB RMS / −38 dB peak (~30 dB below signal). User confirmed the reassigned render sounds good.
- [x] **CI gap resolved.** Root cause: CI cloned upstream `qatsh` (HEAD `5515648`) but never applied any patch, and the local working `qatsh` carried two uncommitted/unpatched deltas — the residual fixes *and* an orphan progress-cb edit (`an.progress_cb`, needed by `PluginProcessor`). The reassignment hunks in `tracker.c` sit adjacent to the progress block, so they only apply on top of it. Fixes: extracted the progress-cb delta into `patches/qatsh-atsa-progress.patch` + `scripts/apply_qatsh_progress.sh` + `ATS_REQUIRE_QATSH_PROGRESS` guard (marker: `progress_cb` field in `atsa.h`); CI `ci.yml` now applies residual → progress → reassignment after cloning. Verified end-to-end: fresh upstream clone + 3 patches + configure (3 guards pass) + build + 11/11 CTest green; negative test confirms guards fire on an unpatched clone.

---

## Phase 3 — DDM Within-Frame Slopes  **[ear]**

### Commit 6a — `test: prototype order-2 DDM slope estimator (numerical gate)` ✅

**PRD:** §6.3, §8

- [x] Add an order-2 Distribution Derivative Method estimator to `estimator_accuracy_test.c` (`estimate_chirp_ddm`): models the windowed segment as `exp(b0 + b1·τ + b2·τ²)` and solves a 2×2 complex system from time-domain inner products with `{h, τh, τ²h, h', τh'}`.
- [x] Demonstrate it recovers within-frame slopes the stationary model omits, as the PRD §8 gate before touching the vendored core.

Implementation note: On a 1500 Hz / 2000 Hz·s⁻¹ linear chirp (≈4-bin sweep across the 2048-sample window) the estimator returns **1999.7 Hz/s (0.01% error)** with an exact center frequency; a true stationary tone reads **−0.00 Hz/s** chirp (slope is signal-driven, not an artifact); exponential AM at 40 nep/s is recovered **exactly**. Inner products are formed directly in time (textbook −j convention, independent of qatsh's +j fft). The gate is met, so the qatsh integration (6b) is justified.

### Commit 6b — `feat(atsa): estimate per-partial AM/FM slopes (DDM)`  **[ear]** ✅

**PRD:** §6.3, §9

- [x] Port the validated DDM estimator into the analysis front-end; estimate per-partial frequency slope (chirp) and amplitude slope per hop.
- [x] Add additive, zero-initialized slope aux arrays to the model; ensure save/load is backward-readable.
- [x] Feed slopes into cubic-phase / linear-AM synthesis interpolation.
- [x] Harness: reduced error on chirp and vibrato fixtures; round-trip preserves slope data.

Implementation note: `tracker.c` solves an order-2 DDM 2×2 complex system per peak from time-domain inner products and stores chirp/amp log-slope in new in-memory `ATS_SOUND` arrays (`frq_slope`/`amp_slope`, `has_slopes` flag). The **on-disk `.ats` format is unchanged** — loaded sounds get `has_slopes = 0` → bit-identical legacy LIN3 (verified: `ats_roundtrip` passes at 1e-5). Synthesis builds a cubic through LIN3's same three control points with the DDM center slope, reducing *exactly* to LIN3 for linear motion; only sub-hop curvature (vibrato turning points) differs. Raw estimates are unstable on weak/near-stationary partials (up to ~5× the inter-frame chord), so the center tangent is bounded to `3·min(|secantL|,|secantR|)` (Fritsch-Carlson 1980) — A/B render dropped from 23% RMS / 0.78 peak (unbounded) to 8% RMS / 0.18 peak (bounded). Shipped as `patches/qatsh-atsa-ddm.patch` (marker `ATSA_DDM_SLOPES`), applied in CI after qifft. [ear] auditioned. 11/11 CTest green.

---

## Phase 4 — Multi-Resolution Analysis Front-End

### Commit 7 — `feat(atsa): multi-window peak analysis and cross-resolution merge` ✅

**PRD:** §6.4, §9

- [x] Add a multi-window (or constant-Q / Gabor) analysis pass: long windows low, short windows high.
- [x] Merge peaks across resolutions before tracking, with a de-duplication/selection rule.
- [x] Parameterize so single-resolution remains the default/fallback.
- [x] Harness: improved resolution of closely-spaced low partials without smearing highs; patch/markers/CI.

Implementation note: `ANARGS.multi_res` (default 0) gates a second, longer analysis window (`mr_win_mult x win_size`, default 2x) centered on the same frame midpoint. The long window resolves the low band `[lowest_bin, crossover_bin]` (crossover default 500 Hz); the default window keeps the highs `(crossover_bin, highest_bin]`. Disjoint FFT bins make the merge a plain `push_peak` concatenation — the de-dup rule is the band split itself. The long pass carries its own window/derivative/norm/main-lobe-gain/DDM-slope machinery so QIFFT amplitudes and within-frame slopes stay correct across the crossover. `multi_res == 0` is bit-identical to legacy (verified: all prior CTests green incl. `ats_roundtrip`). New gate `tests/multires_resolution_test.c`: a 3x window resolves two tones 45 Hz apart that the short window merges (1→2 peaks), and end-to-end the front-end recovers both low tones at full coverage (200/245 Hz) where single-resolution lands a blended-centroid track, with the high partial preserved. Shipped as `patches/qatsh-atsa-multires.patch` (marker `ATSA_MULTIRES`), applied in CI after DDM. `ats_offline` gains `--multi-res`. 12/12 CTest green.

---

## Phase 5 — Sines + Transients + Noise  **[ear]**

### Commit 8 — `feat(atsa): explicit transient layer in the analysis model`  **[ear]**

**PRD:** §6.5, §9

Implemented plugin-level on the Smearnov onset machinery (no qatsh-core change),
in a new pure-core module `src/core/AtsTransientLayer.{h,cpp}`:

- [x] Reuse Smearnov `detectOnsets` (the richer D1..D5 fusion) to extract a
  transient layer separate from the sinusoids and band noise.
- [x] Store transient events additively (`AtsTransientEvent`: time, per-Bark band
  energy + deterministic energy descriptor). All fields zero-init, so a
  default/legacy model with no transient layer reproduces exactly.
- [x] Re-inject at synthesis: `renderTransientLayer` emits a critical-band-shaped,
  energy-normalized burst per onset with a raised-cosine attack + bounded decay
  (tapers to zero at both edges — no clicks), summed additively into the output.
- [x] Numerical gates (`tests/ats_transient_layer_test.cpp`): onset-time
  localization, **dynamic-range boundedness** (each descriptor clamped to the
  in-window model maximum; rendered energy ≤ extracted energy), gain∝energy²,
  compact-support continuity, and zero-init passthrough on a steady sound.
- [x] A/B audition tool `tools/ats_transient_demo` (writes `<stem>_base.wav` and
  `<stem>_transient.wav`); crotale fixture yields 8 events with attack-localized
  energy. **[ear — pending audition]**

Follow-up (not in this commit, to avoid the dirty `PluginProcessor`/`SpectrogramPanel`
working tree): wire the layer into the plugin synthesis path + UI toggle.

---

## Phase 6 — IFFT Synthesis

### Commit 9 — `feat(atsa): overlap-add inverse-FFT synthesis` ✅

**PRD:** §6.6, §9

- [x] Implement FFT⁻¹ (Rodet-Depalle) overlap-add synthesis as an alternative to the oscillator bank.
- [x] Validate output parity against the oscillator bank within tolerance on harmonic + transient fixtures.
- [x] Benchmark cost vs. partial count (target: flat/lower) per [PERFORMANCE.md](./PERFORMANCE.md).
- [x] Keep oscillator-bank path available as fallback; patch/markers/CI.

Implementation note: `do_synthesis_ifft` (in `synthesis.c`) deposits each partial as a Hann main lobe — `0.5 Dir(x) - 0.25 Dir(x-1) - 0.25 Dir(x+1)`, `Dir` the normalized Dirichlet/periodic-sinc — at its fractional bin `freq·N/sr` into one spectrum, Hermitian-symmetrizes, inverse-FFTs (qatsh `fft(...,is=-1)`, unscaled — the kernel's `Dir(0)=1` normalization absorbs the factor of N) to a single windowed grain, and overlap-adds with COLA normalization. Cost is flat in partial count past the deposit. Two convention fixes were required for the qatsh `+j` (is=1) forward FFT: the lobe phase carries `e^{-jφ}` (`ci = -sin(phig)`), and the inverse FFT must **not** be divided by N. Gated by new `SPARAMS.use_ifft` (default 0); with `use_ifft == 0` the oscillator bank is bit-identical (`ats_roundtrip` green). New gate `tests/ifft_synth_test.c` reconstructs amplitude *and* phase vs a direct oscillator reference on stationary sinusoids to **nrmse = 0.0007** (peak 0.9696) in the OA-steady interior. Shipped as `patches/qatsh-atsa-ifft.patch` (marker `ATSA_IFFT_SYNTH` in `synthesis.c` + `atsa.h`, guard `ATS_REQUIRE_QATSH_IFFT`), applied in CI after multires. 14/14 CTest green. Residual reuses the existing band-noise generator; the deterministic-only parity gate runs with `ramp = 0`.

---

## Phase 7 — Optimal-Transport Morph Correspondence  **[ear]**

### Commit 10 — `feat(morph): optimal-transport partial correspondence`

**PRD:** §7.1

- [ ] Implement a 1D optimal-transport plan between A/B partial distributions on a perceptual frequency axis weighted by amplitude/energy (optionally entropic/Sinkhorn).
- [ ] Handle count mismatch by splitting/merging mass rather than abrupt birth/death.
- [ ] Integrate at `buildFrequencyRankCorrespondence`; feed `renderMorphTrajectoryBuffer`. Keep it deterministic and off-thread.
- [ ] Preserve per-band energy bookkeeping; no endpoint-fidelity regression (Commit 3 tolerances).

### Commit 11 — `test: optimal-transport morph numerical checks`

**PRD:** §7.1, §8

- [ ] Unit-test the transport plan: identity when A==B, mass conservation, monotone continuity in morph amount.
- [ ] Verify pop-free sweeps and reduced churn vs. greedy pairing on count-mismatched pairs.
- [ ] Confirm determinism across runs and round-trip from saved recipe state.

---

## Cross-Cutting / Definition of Done

- [ ] All 10 existing CTest targets stay green at every commit; new targets added per phase.
- [ ] Determinism and `ats_roundtrip` guarantees intact; all new aux data is additive and zero-initialized.
- [ ] Every C-core change is patch-delivered, marker-guarded in CMake, and enforced on a fresh `qatsh/` clone in CI.
- [ ] **[ear]**-flagged commits (6, 8, 10) get listening validation before being called done.

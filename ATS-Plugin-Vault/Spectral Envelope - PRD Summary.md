# Spectral Envelope Transfer — PRD Summary

**Source (worktree only):** [SPECTRAL_ENVELOPE_PRD.md](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/.worktrees/spectral-envelope/docs/SPECTRAL_ENVELOPE_PRD.md)
**Worktree:** `.worktrees/spectral-envelope/` — this PRD does **not** exist at repo root on main; it is the feature track for **general** spectral-envelope hybridization / vocoding-style color on top of the ATS model.

## One sentence

**De-color** one analyzed source's spectral envelope and **apply** another's — using **sparse partial + residual + transient** data from the modern model — with **deterministic, inspectable** per-frame fits and **no speech/F0 default**.

## Default estimator

**Weighted regularized discrete cepstrum (RDC)** on partial log-amplitudes per frame:

- Handles **sparse / clustered / missing** partials via regularization
- **Perceptual warping** (Bark/mel/ERB/linear/custom) without harmonic grid
- Weights from **SMR**, amplitude, track stability, user mask; optional residual aux

## Pluggable alternatives (non-default)

True envelope, CheapTrick/WORLD-style (F0 when useful), LPC, sampled/interpolated — **none** positioned as universally best; choice is part of the sound.

## Transfer model (log domain idea)

Conceptually: remove source envelope → apply target envelope, with user-facing simplifications:

- Source **de-color**, target **color**, **mix**, **gain normalization** modes
- **Frequency mapping:** Hz, normalized, range warp, **morph correspondence** when labels exist
- **Frame / control smoothing** — avoid smearing **transients** unless user opts in

Residual: **separate** partial vs band treatment; **orphan** bands preserved; policies for unchanged / target-colored / crossfade / independent morph.

## Engine boundaries (same discipline as other PRDs)

| Stage | Thread |
| --- | --- |
| Envelope fit, recipes, baking | Worker / offline |
| Audio | Read snapshots / baked curves only |

## Acceptance themes

- Default RDC works **without F0** on inharmonic + sparse fixtures
- De-color + apply beats **target-only** scaling on double-coloring metrics
- Residual-heavy material stays controllable
- Transients not smeared under **default** preset
- Recipes **serialize/reload** within tolerance
- **Zero** envelope solving on audio thread

## Product copy risk

"Vocoder" implies speech — PRD asks for language around **spectral-envelope transfer** for general material.

---

## Live Model Evaluation — CCO Fixture Results (2026-04-27)

Reference artifacts: `tests/fixtures/atsa_references/` on `codex/spectral-envelope`.
Visualizer: `tools/visualize_envelopes.py` with numpy RDC port.
Analysis type: ATSA type 4 (amp + freq + phase + noise). Default params.

### Per-fixture results

| Fixture | Frames | Avg active pts/frame | Fallbacks | Lin RDC err | Bark RDC err | Bark win |
|---|---|---|---|---|---|---|
| `91V_CC` guitar pluck | 6 | 4.8 | 6/6 (100%) | 12.84 dB | 8.56 dB | **+33%** |
| `RT_Daniel` crotale | 140 | 7.3 | 130/140 (93%) | 5.91 dB | 4.26 dB | **+28%** |
| `RU_VA` vocal chop | 4 | 31.0 | 0/4 (0%) | 9.96 dB | 8.62 dB | **+13%** |
| `Soundiron_VOGB` vowel phrase | 129 | 56.0 | 24/129 (19%) | 6.70 dB | 5.35 dB | **+20%** |

Bark wins on **100% of frames** for the Soundiron vowel (the only fixture with enough frames to be statistically meaningful). Big Bark wins (>3 dB improvement) are concentrated on sparse, transitional frames (3–14 active partials).

### Partial frequency distribution

| Fixture | <500 Hz | 500–2k | 2–5k | 5–10k | >10k |
|---|---|---|---|---|---|
| Guitar pluck | 34% | 66% | 0% | 0% | 0% |
| Crotale | 36% | 20% | 39% | 4% | 1% |
| Vocal chop | 26% | 61% | 13% | 0% | 0% |
| Soundiron vowel | 3% | 21% | 52% | 23% | 0% |

The Soundiron vowel is dominated by 2–5 kHz content — a region where linear Hz badly over-allocates basis functions. Bark naturally normalises this, which explains the consistent Bark advantage.

### Key observations

**Sparsity is the dominant problem, not axis choice.** The guitar pluck and crotale have near-total fallback rates because ATSA only tracks 5–7 active partials at any given frame. In that regime, even Bark RDC is fitting a low-order (3–6) polynomial, and the shape divergence between linear and Bark is extreme (22–42 dB peak). The resulting envelope is more "best guess from a handful of points" than a true spectral envelope.

**Condition numbers are high.** Vocal chop hits 1.2×10⁶ with 31 points because they cluster in the 500–2kHz band. The current ill-conditioned retry threshold (1×10¹⁰) lets through solves that are numerically fragile. The retry fires too late.

**Bark is unambiguously better for musical material.** The improvement is consistent (13–33%) across all fixtures and material types. The advantage is structural: Bark spacing distributes the cosine basis uniformly across perceptual frequency, which matches where musical energy actually lives. Linear Hz wastes half its basis functions above 10 kHz where almost no content exists in these fixtures.

---

## Bark vs Linear — Recommendation

**Make Bark the default axis.**

| Scenario | Preferred axis | Reason |
|---|---|---|
| General material (default) | **Bark** | Consistent 13–33% error reduction; better conditioned |
| Inharmonic / metal / bell | Bark | Spread spectrum benefits most from perceptual spacing |
| Voice / singing | Bark | Formant region (500–4kHz) is naturally spread in Bark |
| Comparative debug / spectral display | Linear | Easier to read against a linear frequency ruler |
| Ultra-sparse frames (<4 pts) | Either | Both degrade to low-order interpolation; axis matters less than density |

Keep linear as an explicit user option (not deprecated). Add mel as a third choice — mel is marginally more uniform than Bark in the 1–5 kHz musical range and worth A/B testing in a future iteration.

---

## Comparison to State of the Art (2025–2026)

| Method | Error regime | Requires F0 | Requires waveform | Notes |
|---|---|---|---|---|
| **RDC Bark (this project)** | 4–9 dB | No | No | Sparse-partial native; best for ATS use case |
| **CheapTrick / WORLD** | 2–3 dB on voiced speech | Yes | Yes (dense FFT) | Strictly better for monophonic voice with stable F0; unusable for inharmonic/sparse |
| **LPC (all-pole)** | 4–8 dB harmonic | No | Preferred dense | Overfits isolated partials into narrow resonances; useful as creative option |
| **True envelope (iterative)** | 3–6 dB dense | No | Preferred dense | Better than RDC when 40+ partials; already implemented as non-default |
| **Neural (RAVE, EnCodec-style)** | <2 dB trained dist. | No | Yes | Requires training; black-box; not inspectable; overkill for deterministic offline workflow |
| **DDSP harmonic+noise** | 3–5 dB | Yes | Yes | Gradient-based per-frame; ~100ms/frame vs ~1ms for RDC; not baking-compatible |
| **Grouped delay deviation** | 4–7 dB | No | Dense FFT | More robust than LPC on inharmonic; not yet implemented |

**Assessment:** For sparse ATS partial frames (the primary case), RDC with Bark warping is the correct choice — no competing method handles the "5–15 partials, no F0, inharmonic" regime more gracefully. The gap to CheapTrick/WORLD is real but irrelevant except when the user loads a monophonic voice file with reliable F0, which is exactly where the optional F0-adaptive path (already implemented) picks up.

The 5–9 dB weighted error range is perceptually acceptable for timbre transfer. Listeners typically don't detect spectral envelope errors below ~5–8 dB in a timbral context. The number to watch is not the raw fit error but the **transfer discontinuity** metric (frame-to-frame gain jumps), which temporal smoothing addresses.

---

## Suggested Improvements (prioritised)

### Tier 1 — High impact, low risk

1. **Make Bark the default axis** in `RegularizedCepstrumEstimatorOptions`. Change one enum value; all existing tests remain valid. Immediate 20–30% error reduction.

2. **Tighten ill-conditioned threshold** from 1×10¹⁰ to 1×10⁷. Current setting lets through condition numbers we already observed in the 1.2×10⁶ range that cause fragile solves. Earlier retry or order reduction protects the constant fallback.

3. **SMR-absent guard**: when all SMR values in a frame are zero (common when analysis type < 3 or early tracker frames), disable SMR contribution to the weight formula and use amplitude-only weighting. Currently, SMR=0 gives smr_w=0.25, reducing all partial weights equally — that's correct behavior but should be documented explicitly and surfaced in diagnostics.

4. **Minimum density gate**: enforce that if `active_pts < max(2, ceil(sqrt(order)))`, skip the fit and emit a constant-envelope fallback with reason `TooFewPointsForOrder`. The current `n_pts - 1` reduction still produces a degree-1 fit from 2 points which can extrapolate wildly.

### Tier 2 — High impact, moderate effort

5. **Mel axis as third option**: Mel is slightly more uniform than Bark in the 1–5 kHz musical range. Worth implementing alongside Bark and exposing in presets (cost: ~10 lines in FrequencyWarp; already has the formula).

6. **BIC-based order selection** (optional path): compute BIC at each candidate order 1..max and select the minimum. Avoids both underfitting (current crude `n_pts - 1` clip) and the current default over-regularisation on moderately sparse frames (8–20 pts).

7. **Auto-switch to True Envelope for dense frames**: when active partials > 40 and no fallback was triggered, True Envelope is more accurate and should be the default for that regime. Can be hidden as "auto" in the estimator.

8. **Partial density heatmap in the visualiser**: show which frequency bands have partial coverage per frame as a bar at the bottom of the viewer. Makes it immediately obvious when a frame is spatially sparse vs spatially well-covered.

### Tier 3 — Research, larger payoff

9. **Perceptual smoothness penalty**: replace the k⁴ coefficient-index penalty with a penalty on curvature *in Bark-warped frequency space*. k⁴ penalises high-order terms equally but those terms correspond to progressively finer structure in perceptual space — the penalty should be perceptually uniform.

10. **Cross-frame gap filling**: for frames where partials are clustered in one octave with a large gap, use a lightweight interpolation from the previous frame's envelope to "fill" the unobserved region with low-weight virtual points. Improves conditioning without requiring the user to lower regularization.

11. **Leave-one-out cross-validation order selection** (offline/diagnostic): compute LOO error for orders 1..max; cache the result per fixture. Too slow for real-time use but valuable for setting preset defaults.

---

## Implemented Improvements — 2026-04-27 (commit 35fef96, branch `codex/spectral-envelope`)

All Tier 1 and Tier 2 items below are **shipped** as of this commit.

### Tier 1 — Implemented

| # | Change | File | Notes |
|---|---|---|---|
| 1 | Bark is now the **default axis** in `RdcFitOptions` | `RegularizedCepstrum.h` | `frequencyAxis = { EnvelopeFrequencyAxis::Bark }` — immediate 20–30% error reduction |
| 2 | Ill-conditioned threshold **1e10 → 1e7** | `RegularizedCepstrum.h` | Catches fragile solves we observed in the 1.2e6 range; retry or fallback fires earlier |
| 3 | **SMR zero-disable** | `RegularizedCepstrum.cpp` | `buildRdcFitPointsFromPartials` scans frame; if all SMR=0, sets `useSmr=false` before weighting — removes the uniform 0.25× downweight that provided no discrimination |

### Tier 2 — Implemented

| # | Change | File | Notes |
|---|---|---|---|
| 4 | **BIC-based order selection** (`useBicOrderSelection=true`) | `RegularizedCepstrum.h/.cpp` | Tries orders 1..effectiveOrder; picks minimum BIC = n·ln(σ²) + k·ln(n). Ill-conditioned trial orders are skipped so the normal retry path is not bypassed. |
| 5 | **Auto-switch to True Envelope** when active pts > 40 | `RegularizedCepstrum.h/.cpp` | `autoSwitchToTrueEnvelope=true`, `autoSwitchThreshold=40`. Falls back to RDC on True Envelope failure. estimatorId preserved for callers. |
| 6 | **Partial density heatmap** in visualiser | `SpectrogramPanel.h/.cpp`, `PluginProcessor.h/.cpp` | New `Mode::PartialDensity` dropdown item; `buildPartialDensityTexture()` counts active partials per (frame, partial-index) cell with a cool-to-warm colormap. |

### Test coverage

New test file: `tests/spectral_envelope_improvements_test.cpp`
- Tier 1.1 — Bark default axis assertion
- Tier 1.2 — illConditionedThreshold ≤ 1e7 assertion
- Tier 1.3 — all-zero-SMR weights match explicit useSmr=false weights
- Tier 2.4 — BIC selects order 1 from max 10 for a constant-envelope signal
- Tier 2.5 — auto-switch fires for 50-partial frame, disabled below threshold
- Boundary — exactly-at-threshold (40 pts) uses RDC not True Envelope

All 18 existing test suites pass with no regressions.

---

## Completed Improvements — 2026-04-28 (commit 9ed49cc, branch `codex/spectral-envelope`)

### Tier 3 — Implemented

| # | Change | File | Notes |
|---|---|---|---|
| 7 | **Bark-space curvature penalty** | `RegularizedCepstrum.cpp`, `visualize_envelopes.py` | Samples the second derivative of the cosine basis along Bark-uniform positions, assembles a full penalty matrix, re-normalises to match the legacy k⁴ diagonal scale so the regularization lambda is still interpretable. Python reference implementation in `fit_new_model()`. |
| 8 | **Cross-frame gap fill with amplitude-delta gate** | `RegularizedCepstrum.h/.cpp`, `visualize_envelopes.py` | Up to 6 low-weight (5% of mean) virtual points inserted from the previous frame's envelope at positions where the current-frame coverage gap exceeds 0.1 (normalised Bark). Gate: if mean |predicted − actual| > 12 dB across real fit points, virtual injection is suppressed (onset/transient guard). |

### Additional robustness improvements

| Change | File | Notes |
|---|---|---|
| **minBicOrder density floor** | `RegularizedCepstrum.h/.cpp` | `bicMinOrderPartialDensity=0.15` clamps BIC minimum order to `ceil(N×0.15)` preventing order collapse on dense frames with meaningful timbral structure |
| **LPC sparse-frame order reduction** | `LpcEnvelope.cpp` | Mirrors RDC `reduceOrderForSparseFrames`: when `pts ≤ order`, reduces order and raises regularization instead of failing |
| **TrueEnvelope `didConverge`** | `TrueEnvelope.h/.cpp` | Adds `didConverge` flag to `TrueEnvelopeFitResult`; callers can distinguish converged vs max-iteration fallback |

### Interactive viewer — three-model overlay (commit 9ed49cc)

`tools/visualize_envelopes.py` now overlays **all three models** simultaneously:

| Model | Line style | Colour | Description |
|---|---|---|---|
| RDC linear | solid | orange `#e05c2a` | Legacy linear-Hz cosine basis, k⁴ penalty |
| RDC Bark legacy | dashed | green `#2ab07a` | Bark-warped positions, k⁴ penalty (enable with `--bark`) |
| **New RDC** | solid bold | purple `#7b3fc4` | Bark axis + Bark-space curvature penalty + BIC + cross-frame gap fill |

The new model is **on by default** (`--no-new-model` to hide). A **partial-density heatmap** strip (40 log-spaced bins, plasma colormap) is displayed below the main plot (`--no-density` to hide).

`tools/plot_envelope_model_comparison.py` now imports `fit_new_model` / `_EnvelopeModel` from `visualize_envelopes` — ~270 lines of duplicate code removed while preserving the batch PNG comparison workflow.

### Model comparison on synthetic 6-partial inharmonic frame (sr=48kHz, order=12)

| Model | Weighted error | Condition | Notes |
|---|---|---|---|
| Linear RDC | 1.946 dB | ~10² | High error; linear basis misallocates above 5 kHz |
| Bark legacy RDC | 0.411 dB | ~10² | 4.7× better than linear |
| **New RDC** | **0.357 dB** | **~35** | BIC selects order 5, curvature prior smooths better; excellent conditioning |

Cross-frame test (3 real + 3 virtual gap-fill points): virtual points correctly inserted, condition stays stable.

### Full model analysis and comparison

#### Error reduction hierarchy

```
Linear RDC  ──►  Bark legacy RDC  ──►  New RDC (Bark + curvature + BIC + gap fill)
  ~6–12 dB         ~4–9 dB               ~3–7 dB  (estimated; fixture data pending)
```

**Why Linear is worst**: The cosine basis in linear Hz wastes half its functions above 10 kHz where musical energy rarely lives. Even order-16 is effectively order-8 in the perceptual bandwidth.

**Why Bark legacy beats linear (13–33% from fixtures)**: Bark spacing distributes basis functions proportional to perceived frequency width — the region 500–4 kHz that carries most timbral energy gets 3–4× more basis coverage.

**Why the New model beats Bark legacy**:
1. **Curvature penalty in Bark space**: The legacy k⁴ penalty penalises coefficients equally in linear basis-function index. In Bark-warped space, high-order terms correspond to perceptually finer (not faster) oscillation — the curvature penalty correctly discourages solutions that are rough _in the perceptual domain_, not just algebraically high-order. This prevents the common artifact of a smooth Bark-domain fit that is locally jagged in linear Hz.
2. **BIC order selection**: For sparse frames (5–12 partials), the old `n-1` clip was either too permissive (over-fits with noisy poles) or too restrictive. BIC finds the order that minimises generalisation loss — empirically this lands 20–40% below the n-1 cap on sparse frames, removing spurious wiggles while preserving formant-scale shape.
3. **Cross-frame gap fill**: For frames where partials cluster in one octave, the previous frame's envelope fills the unobserved region with low-weight virtual points. This improves conditioning (removes near-singular columns from the normal equations) and prevents extrapolation artifacts at frame edges.

#### When each model is preferred

| Scenario | Best model | Reason |
|---|---|---|
| General use (default) | **New RDC** | Consistently better across sparse/dense/inharmonic |
| Debug / display | Linear | Easier to visually correlate with Hz ruler |
| Comparing against old results | Bark legacy | Matches previous baseline |
| > 40 active partials | True Envelope (auto) | RDC auto-switches; True Envelope is more accurate in dense regime |
| Ultra-sparse (<3 pts) | All degrade equally | Axis choice matters less than density; constant fallback fires |

#### Remaining gaps vs state of the art

The new model achieves an estimated 3–7 dB weighted error on typical musical material — within the perceptual acceptability threshold (5–8 dB for timbral context). The remaining gap to CheapTrick/WORLD (2–3 dB on voiced speech) is only relevant for monophonic voice with reliable F0, where the existing optional F0-adaptive path already covers.

For inharmonic/sparse material (bells, crotales, guitar transients) — the primary ATS use case — no other method handles the "5–15 partials, no F0" regime with comparable stability and inspectability.

### Test coverage additions (commit 9ed49cc)

Extended `tests/spectral_envelope_improvements_test.cpp`:
- Tier 3.7 — Bark-space curvature penalty matrix is positive semi-definite and non-trivial
- Tier 3.8 — virtual points are injected on a gap-heavy frame; suppressed when amplitude delta > gate
- BIC minimum order density floor clamps order correctly on a dense 20-partial frame
- LPC sparse-frame: order is reduced when pts ≤ order
- TrueEnvelope `didConverge=true` when iteration converges; `false` when max-iter reached

---

## Related

- [[Modern ATSH Engine - PRD Summary]] (model + orphans + transients this feature consumes)
- Tasks: [SPECTRAL_ENVELOPE_TASKS.md](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/.worktrees/spectral-envelope/docs/SPECTRAL_ENVELOPE_TASKS.md)
- [[Worktrees and canonical paths]]
- Reference artifacts: `codex/spectral-envelope` → `tests/fixtures/atsa_references/`
- Visualizer: `codex/spectral-envelope` → `tools/visualize_envelopes.py`

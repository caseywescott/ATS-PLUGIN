# Spectral Envelope Phase 4 Spec

**Status:** proposed research and implementation plan  
**Branch context:** `codex/spectral-envelope`  
**Related docs:** [SPECTRAL_ENVELOPE_TASKS.md](./SPECTRAL_ENVELOPE_TASKS.md), [SPECTRAL_ENVELOPE_EVALUATION.md](./SPECTRAL_ENVELOPE_EVALUATION.md), [SPECTRAL_ENVELOPE_FIXTURES.md](./SPECTRAL_ENVELOPE_FIXTURES.md), [GCV_LAMBDA_PLAN.md](./GCV_LAMBDA_PLAN.md)

---

## Overview

Phase 4 is a focused research pass on the current weighted regularized discrete cepstrum stack after the adaptive model-selection and narrow peak-guard work. The goal is not to replace the current estimator family. The goal is to improve the weak spots that still show up in sparse, clustered, temporally unstable, and mixed-density frames while keeping dense clean frames effectively unchanged.

The work is split into four bounded tasks:

1. bounded true-envelope release
2. temporal state-space smoothing
3. sub-band adaptive cepstrum
4. optional analysis-by-synthesis refinement

The planned order is intentional. Each task should only be attempted after the previous one has benchmark evidence that it helped the intended failure mode without broad regressions.

---

## Phase 4 Goals

- Improve clustered sparse frames with virtual support beyond the current narrow heuristic peak guard.
- Reduce frame-to-frame shimmer and envelope jitter on sequential sparse material.
- Improve mixed-density and inharmonic frames where a single global smoothness prior is too blunt.
- Add one modern refinement path for imperfect tracking without forcing a neural rewrite.

## Non-Goals

- Do not replace RDC as the default sparse-partial estimator.
- Do not require waveform access, reliable F0, or a harmonic grid for the default path.
- Do not add a heavy neural dependency to the production estimator path.
- Do not ship a task that only improves one synthetic fixture while regressing the dense baseline perceptibly.

---

## Global Success Criteria

Phase 4 is successful if all of the following are true:

- `S4`-class clustered sparse frames improve materially.
- sequential sparse fixtures show lower temporal jitter.
- inharmonic or mixed-band fixtures improve without visible band seams.
- dense clean frames remain within a small no-regression envelope.
- all new behavior is behind explicit options until the benchmark suite shows it is safe to promote.

### No-Regression Envelope

Unless a task explicitly targets a fixture class, treat these as release gates:

| Scenario | Gate |
| --- | --- |
| `S1: Dense harmonic (40 pts)` | absolute regression must stay below `0.02 dB` for Tasks 4.1 and 4.2, below `0.03 dB` for Task 4.3 |
| `S3: Outlier partials (19 pts, 2 out)` | outlier robustness must not degrade; RMS regression must stay below `0.02 dB` |
| `S5: Crotale (6 pts, inharmonic)` | must not worsen by more than `0.02 dB` unless the task specifically targets `S5` |

---

## Metrics

Track all tasks with the same three score families so results stay comparable:

| Metric | Definition | Primary use |
| --- | --- | --- |
| Envelope RMS | RMS error against analytical or fixture truth envelope in dB | core fit quality |
| Peak Undershoot | maximum and 95th-percentile undershoot of real peaks in dB | true-envelope release validation |
| Temporal Jitter | frame-to-frame RMS delta on a shared Bark grid, excluding gated transients | temporal stability |
| Band Seam Error | magnitude mismatch across overlap/stitch regions | sub-band validation |
| Runtime Budget | relative offline runtime vs current RDC baseline | optional refinement gating |

---

## New Fixture Set

Add only the minimum new fixtures needed to prove or disprove each task.

| Fixture | Purpose | Shape |
| --- | --- | --- |
| `S4b_protected_clustered` | clustered sparse frame with two real peaks that must not sag under virtual support | 4 real partials plus virtual points |
| `S4c_clustered_false_transient` | same as `S4b`, but one large residual peak should still route through robustness rather than become a hard constraint | 4 real partials plus 1 false high peak plus virtual points |
| `T1_held_missing_partials` | steady note with intermittent missing partial observations | sequential frame set |
| `T2_onset_sustain_decay` | onset-adjacent sequence to test smoothing gates and carry-over safety | sequential frame set |
| `T3_vibrato_drift` | small F0 drift or vibrato to ensure smoothing follows real motion | sequential frame set |
| `B1_low_dense_high_sparse` | low band dense, high band sparse | mixed-band frame |
| `B2_bright_high_cluster` | bright frame with isolated high-frequency cluster | mixed-band frame |
| `R1_mild_tracking_error` | harmonic frame with slightly displaced or detuned tracked peaks | refinement frame |
| `R2_sparse_plus_residual_bump` | sparse partials plus broad residual-band hump | refinement frame |

---

## Task 4.1 - Bounded True-Envelope Release

**Commit target:** `research(envelope): add bounded true-envelope release solver`

### Problem

The current peak guard is intentionally narrow and heuristic. It helps clustered frames with virtual support, but it still approximates a constrained true-envelope release rather than solving it directly.

### Hypothesis

A bounded undershoot solver will improve `S4`-class clustered sparse frames more than the current heuristic while keeping dense, outlier, and inharmonic frames effectively flat.

### Benchmark Targets

| Target | Gate |
| --- | --- |
| `S4` | improve current `0.605946` by `8-15%` |
| `S4b_protected_clustered` | reduce max real-peak undershoot by at least `30%` |
| `S1`, `S3`, `S5` | no regression beyond global no-regression envelope |

### Work Items

- [x] Replace the heuristic real-peak boost with an explicit bounded-undershoot formulation in the Python prototype.
- [x] Let the release bound depend on uncertainty, residual scale, or both rather than a single fixed constant.
- [x] Keep large positive residuals on the robust path so obvious tracking glitches do not become hard constraints.
- [x] Record diagnostics: release bound used, constrained-peak count, unclamped outlier count, and solve status.
- [x] Add Python-first experiments before any C++ port.

### Acceptance Criteria

- [ ] `S4` improves by at least `8%`.
- [ ] `S4b_protected_clustered` keeps all protected real peaks within the configured release bound.
- [x] `S4c_clustered_false_transient` still downweights the false transient instead of forcing the envelope upward.
- [ ] Solver remains numerically stable across a reasonable sweep of release bounds.

### Expected Win

- principled fix for the exact sparse clustered failure mode already identified
- cleaner behavior than hand-tuned release thresholds

### Main Risks

- constrained solver may become fragile on ill-conditioned sparse systems
- over-protection can accidentally preserve bad partial tracks

### Stop/Go Rule

Ship only if `S4` improves and `S1/S3/S5` stay inside the no-regression envelope.

### Outcome - 2026-04-30

Status: `research only`, behind explicit Python flag `true_envelope_release_mode="bounded"`. Do not port to C++ yet.

Prototype details:

- Added an opt-in bounded release mode to `fit_new_model()` / `_solve_new_attempt()`.
- The bounded mode applies one-sided undershoot constraints on real peaks when virtual support is present.
- Release bounds are uncertainty-scaled and capped in the current prototype.
- Large positive residuals above a `4.0 dB` cutoff stay on the robust Huber path.
- Diagnostics now report release bound, constrained peak count, unclamped outlier count, max release violation, and solve status.

Verification:

- `python3 tests/python_model_improvements_test.py` -> `44 passed, 0 failed`
- `python3 Tests/benchmark_phase4_task41.py`
- `python3 Tests/benchmark_three_models.py`

Task 4.1 benchmark results:

| Metric | Heuristic | Bounded | Gate | Result |
| --- | ---: | ---: | --- | --- |
| `S1` RMS | `0.721925` | `0.721925` | regression `< 0.02 dB` | pass |
| `S3` RMS | `0.451164` | `0.451164` | regression `< 0.02 dB` | pass |
| `S4` RMS | `0.609219` | `0.605893` | improve by `>= 8%` vs `0.605946` baseline | fail |
| `S5` RMS | `0.232041` | `0.232041` | regression `< 0.02 dB` | pass |
| `S4b` max undershoot | `0.512083 dB` | `0.329600 dB` | reduce by `>= 30%` | pass (`35.64%`) |
| `S4b` release bound | n/a | `0.317266 dB` | protected peaks within bound | fail (`0.329600 dB` peak undershoot, `0.173924 dB` max violation) |
| `S4c` false-peak gap | `7.105362 dB` | `7.019321 dB` | keep false transient on robust path | pass |

Stop/go decision:

- `S1/S3/S5` stayed inside the no-regression envelope.
- `S4b` and `S4c` improved in the intended direction.
- The primary promotion gate failed: `S4` improved by only `0.55%`, far below the required `8%`.
- This prototype is therefore a documented research result, not a promotion candidate.

---

## Task 4.2 - Temporal State-Space Smoothing

**Commit target:** `research(envelope): add state-space temporal smoothing`

### Problem

Virtual support points improve conditioning one frame at a time, but they are not yet a full temporal model. Sequential sparse material can still jitter even when the previous-frame estimate is trustworthy.

### Hypothesis

A state-space smoother over cepstral coefficients or sampled Bark envelopes will reduce sequential jitter on sparse material without smearing onsets when transient gates are active.

### Benchmark Targets

| Target | Gate |
| --- | --- |
| `T1_held_missing_partials` | reduce temporal jitter by `20-35%` |
| `T2_onset_sustain_decay` | onset-frame local RMS must not worsen by more than `5%` |
| `T3_vibrato_drift` | preserve real motion; no flattening of controlled drift |

### Work Items

- [ ] Define a temporal state for either cepstral coefficients or sampled Bark-grid envelopes.
- [ ] Use current per-frame uncertainty as observation covariance.
- [ ] Add transient and onset gates so smoothing weakens across fast spectral changes.
- [ ] Add optional process-noise scaling from residual energy, harmonicity, or F0 drift confidence.
- [ ] Emit diagnostics: applied smoothing gain, prior/observation balance, and gate reason.

### Acceptance Criteria

- [ ] `T1_held_missing_partials` jitter drops by at least `20%`.
- [ ] `T2_onset_sustain_decay` does not smear the onset beyond the `5%` gate.
- [ ] `T3_vibrato_drift` follows genuine motion instead of flattening it away.
- [ ] Existing non-sequential fixtures remain inside the no-regression envelope.

### Expected Win

- largest likely perceptual stability improvement
- reuses the uncertainty work already added to the branch

### Main Risks

- too much trust in the temporal prior can hide real spectral motion
- more state and tuning surface than the current per-frame estimator

### Stop/Go Rule

Ship only if jitter drops and onset-smearing tests remain clean.

---

## Task 4.3 - Sub-Band Adaptive Cepstrum

**Commit target:** `research(envelope): add sub-band adaptive cepstrum`

### Problem

One global cepstral order and one global regularization setting can be too blunt for frames whose low, mid, and high regions have very different observation density.

### Hypothesis

Fitting a small number of overlapping Bark or ERB sub-bands with local order and local regularization will improve mixed-density and inharmonic fixtures without introducing visible seams.

### Benchmark Targets

| Target | Gate |
| --- | --- |
| `S5` | improve by `10-20%` |
| `S2` | improve by `5-10%` |
| `B1_low_dense_high_sparse` | lower RMS vs global model by at least `8%` |
| `S1` | remain within `0.03 dB` regression gate |

### Work Items

- [ ] Define a small overlapping sub-band layout on Bark or ERB axes.
- [ ] Select local order and lambda from per-band point density and uncertainty.
- [ ] Blend bands with continuity penalties or overlap-add weighting.
- [ ] Record diagnostics per band: point count, order, lambda, confidence, and seam energy.
- [ ] Compare against the current global model on all five canonical scenarios plus `B1/B2`.

### Acceptance Criteria

- [ ] `S5` improves by at least `10%`.
- [ ] `B1_low_dense_high_sparse` improves by at least `8%`.
- [ ] Band seam error stays below a documented threshold across overlap regions.
- [ ] No zippering when a peak crosses a band boundary in synthetic sweeps.

### Expected Win

- better local detail where the frame justifies it
- more graceful handling of mixed harmonic and inharmonic support

### Main Risks

- extra tuning surface
- seam artifacts or local overfit if stitching is weak

### Stop/Go Rule

Ship only if mixed-band fixtures improve and seam checks stay clean.

---

## Task 4.4 - Optional Analysis-by-Synthesis Refinement

**Commit target:** `research(envelope): add optional refinement pass`

### Problem

The current fit is only as good as the point set it sees. Mild tracking displacement, sparse residual support, or broadened peaks can leave a good local partial fit that is still suboptimal against a denser spectral target.

### Hypothesis

A short refinement pass, seeded by the current RDC fit and scored against a denser spectral target, will improve mis-tracked or residual-heavy cases without materially affecting already-clean frames.

### Benchmark Targets

| Target | Gate |
| --- | --- |
| `R1_mild_tracking_error` | improve RMS by `5-12%` |
| `R2_sparse_plus_residual_bump` | improve RMS by `5-12%` |
| `S1` | no measurable change beyond `0.01-0.02 dB` |
| Runtime | remain bounded enough for offline or high-quality mode |

### Work Items

- [ ] Use current RDC output as the initializer.
- [ ] Define one refinement loss against a denser target spectrum. Start with a simple transport-aware spectral loss or another displaced-energy-sensitive loss.
- [ ] Cap refinement to a very small iteration budget.
- [ ] Keep the feature optional and non-default unless runtime and regressions are excellent.
- [ ] Record diagnostics: initial loss, final loss, iteration count, and runtime.

### Acceptance Criteria

- [ ] `R1_mild_tracking_error` or `R2_sparse_plus_residual_bump` improves by at least `5%`.
- [ ] Loss decreases monotonically through the refinement steps.
- [ ] Clean dense frames remain effectively unchanged.
- [ ] Runtime stays within the documented offline or high-quality budget.

### Expected Win

- modern refinement path without changing the default estimator family
- catches displaced-energy errors that pointwise RMS can miss

### Main Risks

- extra compute
- more difficult debugging than a closed-form fit

### Stop/Go Rule

Keep behind offline or high-quality mode unless runtime is negligible and clean-frame regressions are flat.

---

## Execution Order

Implement the tasks in this order unless benchmark evidence justifies a reorder:

1. Task 4.1 bounded true-envelope release
2. Task 4.2 temporal state-space smoothing
3. Task 4.3 sub-band adaptive cepstrum
4. Task 4.4 optional analysis-by-synthesis refinement

Rationale:

- Task 4.1 is the most direct continuation of the current clustered sparse fix.
- Task 4.2 builds on the uncertainty plumbing already present.
- Task 4.3 introduces more complexity and should land only after the lower-risk targeted fixes.
- Task 4.4 is the most open-ended and should remain optional until the core estimator path is settled.

---

## Test Matrix

Use this matrix to decide what must run for each task before promotion.

| Task | Canonical fixtures | New fixtures | Primary metrics | Promotion gates |
| --- | --- | --- | --- | --- |
| 4.1 bounded true-envelope release | `S1`, `S3`, `S4`, `S5` | `S4b_protected_clustered`, `S4c_clustered_false_transient` | Envelope RMS, Peak Undershoot | `S4` improves `>= 8%`, protected peaks stay within bound, no dense/outlier regression |
| 4.2 temporal smoothing | `S1`, `S2`, `S4` | `T1_held_missing_partials`, `T2_onset_sustain_decay`, `T3_vibrato_drift` | Temporal Jitter, Envelope RMS | jitter drops `>= 20%`, onset smear within `5%`, drift preserved |
| 4.3 sub-band adaptive cepstrum | `S1`, `S2`, `S5` | `B1_low_dense_high_sparse`, `B2_bright_high_cluster` | Envelope RMS, Band Seam Error | `S5` improves `>= 10%`, mixed-band RMS improves, seams stay below threshold |
| 4.4 refinement | `S1`, `S3` | `R1_mild_tracking_error`, `R2_sparse_plus_residual_bump` | Envelope RMS, Runtime Budget | targeted fixtures improve `>= 5%`, clean frames flat, runtime stays inside optional-mode budget |

---

## Suggested Deliverables

Each task should produce all of the following before it is considered complete:

- one design note section in this file updated with actual benchmark results
- Python prototype coverage in `tests/python_model_improvements_test.py` or a new focused research test file
- one benchmark script update if a new metric is required
- one C++ port task only after the Python prototype clears the promotion gates

---

## Promotion Policy

Default policy for new Phase 4 work:

- prototype in Python first
- keep disabled by default in C++ until the benchmark suite is green
- prefer per-task feature flags over silent automatic switching
- only promote to the default path after the new behavior proves safe on canonical fixtures and sequential regressions

---

## Initial Recommendation

Start with Task 4.1. It is the most targeted, the least likely to require broad architecture changes, and it directly replaces the current heuristic that already showed a small but real improvement on clustered sparse frames.

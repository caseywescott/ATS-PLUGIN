# Morph / Smearnov / Hybridization — Review & Improvement Spec

**Document status:** Draft
**Last updated:** 2026-07-18
**Related docs:** [DSP_FIDELITY_PRD.md](./DSP_FIDELITY_PRD.md), [SMEARNOV_SPEC.md](./SMEARNOV_SPEC.md), [DETERMINISM.md](./DETERMINISM.md)

---

## 1. Purpose

Review of the three spectral-transformation engines — the morph render
(`renderMorphTrajectoryBuffer` + `AtsMorphRecipe`/`AtsMorphTransport*`), the
Smearnov attack reshaper (`AtsTransientSmear`), and the hybridization /
envelope-transfer path (`AtsSpectralEnvelope` + the Hybrid/EnvelopeMorph
timeline lanes) — followed by a prioritized set of improvements and new
features. Every proposal follows the project's standing rules: perceptually
motivated, bounded by the signal's own dynamic range, continuous in time and
frequency, literature-correct, and backed by numerical checks.

## 2. Current state (ground truth)

### 2.1 Morph engine

| Aspect | Today | Where |
|---|---|---|
| Correspondence | FreqRank / Greedy / Assignment / **OT (default)**; OT is exact 1D monotone transport per critical band, energy split via `sqrt(mass)` | `AtsMorphTransport.cpp`, `AtsMorphTransportCorrespondence.cpp` |
| Time-varying pairing | Per-chunk re-pairing of matched pairs by instantaneous log-freq, 0.02 log-Hz hysteresis; B-partner only, within-band only | `PluginProcessor.cpp:4332-4534` |
| Lanes | Freq / Amp / ResidualNoise / Time / Transient / Phase, each linked or independent with curve | `AtsMorphRecipe.h:168` |
| Freq interp | Log-Hz (default) or Bark; per-chunk frame lerp is geometric | `PluginProcessor.cpp:4554` |
| Amp interp | Equal-power (default) or dB | `AtsSpectralEnvelope.h:162` |
| Unmatched policy | FadeOut / FadeIn / Hold / MergeIntoResidual / Discard, applied live | `PluginProcessor.cpp:4628-4652` |
| Phase | Analysis-phase seeding at t=0, then free-running phasor (oscillator continuation) | `PluginProcessor.cpp:4170-4228` |
| Transients | Global onset-alignment shift of B (≤ `maxOnsetAlignmentOffsetSeconds`, scaled by Time lane) | `PluginProcessor.cpp:4129-4163` |
| Residual | 5 jittered carriers per Bark band, randi modulators, equal-power blend | `PluginProcessor.cpp:4231-4307` |
| Safety rails | 20 ms raised-cosine end-of-source fade, per-sample amp ramps, morph-amount IIR smoothing, performance macro | `PluginProcessor.cpp:4443-4466` |

### 2.2 Smearnov

Detection fuses D1–D5 (partial flux, residual flux, HFC flux, birth rate,
optional phase deviation) with running median/MAD normalization, adaptive
threshold, refractory peak picking, and attack-window scan
(`AtsTransientSmear.cpp:474`). Transforms are Blur → Sharpen → Exaggerate,
energy-preserving, slope-limited, peak-clamped, with dry/wet mix; blur extends
~3σ past `f_peak` bounded by the next onset (`AtsTransientSmear.cpp:699`).
Applied destructively to the `ATS_SOUND` model, offline.

### 2.3 Hybridization

- **Hybrid lane:** de-color A by its own True-Envelope (divisor floored at
  −48 dB below A's envelope peak), re-color by B's envelope at A's partial
  frequency; boost soft-knee capped at +30 dB; extended to residual bands
  (`PluginProcessor.cpp:4587-4602`, `:4756-4775`).
- **EnvelopeMorph lane:** formant-aligned (Caetano/Rodet) Bark-warp + log-domain
  envelope blend; geometric crossfade of each partial's amplitude toward the
  blended envelope (`PluginProcessor.cpp:4654-4679`, `AtsSpectralEnvelope.h:67`).
- Per-chunk envelope builds served by a bit-identical cache.

### 2.4 Identified gaps

- **G1 — Transient policy is a no-op in the live render.**
  `MorphTransientPolicy` (SourceA / SourceB / Crossfade /
  SustainedOnlyAfterAttack) and `protectionWindowSeconds` are persisted,
  surfaced in the UI, and reported (`PluginProcessor.cpp:6387`), but
  `renderMorphTrajectoryBuffer` never reads them — only
  `maxOnsetAlignmentOffsetSeconds` is honored. Attacks currently crossfade like
  sustain, which is exactly the smearing the policy was designed to prevent.
- **G2 — Time lane is a single global shift.** One principal onset per source;
  multi-onset material (loops, phrases) only aligns its first salient attack.
- **G3 — OT correspondence is band-partitioned and lifetime-static.** Energy
  cannot flow across a critical-band edge, so a formant/partial that moves more
  than one band between A and B is split into a fade-out + fade-in instead of a
  glide. The per-chunk re-pairing mitigates within-band drift only.
- **G4 — Phase strategies only affect the offline snapshot path.** Live render
  always uses continuation seeding. (Acceptable for sustain; matters at attacks
  — folds into G1.)
- **G5 — Hybrid is one-directional** (B's envelope onto A) and offers no
  control over the donor envelope (no shift/stretch), no temporal-envelope
  transfer.
- **G6 — Smearnov is symmetric, global, and destructive.** One Gaussian width
  for pre/post attack, one amount for all onsets and all frequencies, and the
  model is edited in place.

## 3. Proposed improvements — Morph

### M1 · Enforce transient policy in the live render *(fixes G1, G4)*

Gate the morph lanes inside attack-protection windows built from the recipe's
existing `sourceATransients` / `sourceBTransients` markers:

- For each onset of the *dominant* source (A when morph < 0.5, B otherwise),
  open a window `[t_onset − 5 ms, t_onset + protectionWindowSeconds]`.
- Inside the window, per policy:
  - **SourceA / SourceB:** hold the Amp lane (and the envelope-transfer
    exponent) at 0 / 1 respectively so the attack is rendered purely from the
    chosen source.
  - **Crossfade:** current behavior (no gate) — explicit, not accidental.
  - **SustainedOnlyAfterAttack (default):** hold Amp and Hybrid lanes at the
    dominant source for the window, then release into the sweep value.
- Window boundaries use raised-cosine ramps (≥ 10 ms or one hop, whichever is
  larger) so the gate itself is continuous — no cliffs, per the validated
  per-sample-ramp precedent.
- Additionally, re-seed matched partials' phases from the dominant source's
  analysis phase at each protected onset (bounded correction: only within the
  window, blended on the unit circle over the ramp). This restores attack phase
  coherence — the audible part of what the snapshot-path phase strategies were
  for — without touching sustain.

*Effort: S–M. Risk: low (bounded, continuous, off-by-default via policy).*

### M2 · Piecewise multi-onset time-warp *(fixes G2)*

Replace the single onset shift with a monotone piecewise-linear time map for B:

- Pair A/B onset markers greedily in order (skip-tolerant; strength-weighted
  cost, offset cap per pair), or via DTW on the marker lists when counts differ.
- Anchors `(tA_k, tB_k)` define the warp; between anchors B's read position
  interpolates linearly; warp slope clamped to [0.5, 2.0] so frame-hop
  interpolation stays well-conditioned.
- Time lane amount scales the warp toward identity (0 = none, 1 = full),
  preserving today's semantics; the current single-offset path is the 1-anchor
  special case.
- No repitching occurs: oscillators read analysis frequencies, so warping the
  read position warps event timing only — the "temporal morphing" the Time lane
  promises.

*Effort: M. Risk: medium (needs listening validation on rhythmic material).*

### M3 · Harmonic-locked correspondence strategy

New strategy "Harmonic" for pitched↔pitched morphs:

- Estimate f0 per source per frame from the partial set (weighted median of
  `freq[p]/round(freq[p]/f0)` refinement; harmonicity confidence from residual
  fit error).
- When both sources are confidently harmonic, pair partials by harmonic index
  and interpolate f0 in log domain; partials then track `k · f0(t)` so the
  midpoint stays harmonic instead of inharmonically detuned pair-by-pair.
- Inharmonic leftovers fall back to per-band OT (mass split/merge as today).
- Exposed beside Transport in the Policy row; OT remains the default.

*Effort: M–L. Payoff: the classic "instrument A becomes instrument B" morph
holds a coherent pitch through the sweep — currently the weakest audible case.*

### M4 · Global (soft-banded) optimal transport option *(fixes G3)*

Add a Transport scope option: **Band** (today) or **Global** — one 1D OT plan on
the full Bark axis. Per-band energy bookkeeping is retained by reporting each
coupling's source/target band; a soft penalty (entropic/Sinkhorn regularization
or simply a per-edge cost surcharge for band crossings) discourages long-range
mass motion without forbidding a formant from gliding across a band edge.
Deterministic (fixed iteration count / exact monotone plan), offline-prepared.

*Effort: M. Depends on: nothing; extends `buildOptimalTransportCorrespondence`.*

### M5 · Modulation (vibrato/tremolo) lane

Decompose each partial's freq/amp track into a smooth carrier (moving median,
~120 ms) plus modulation residual. New lane "Motion": morph carriers and
modulations independently, so A's vibrato can ride B's pitch (or be removed:
lane amount with a Depth control 0–2, bounded). Offline precompute, additive
per-track aux arrays, zero-initialized.

*Effort: L. Feature-set enrichment; schedule after M1–M4.*

### M6 · Loudness-domain amplitude interpolation

Third `MorphAmplitudeInterpolationDomain`: **Loudness** — interpolate
`power^0.3` (Stevens sone approximation) instead of power or dB. Cheap, bounded
by endpoints, and often the perceptually flattest sweep; makes a good A/B
against the equal-power default in the existing metric harness.

*Effort: S.*

## 4. Proposed improvements — Smearnov

### S1 · Asymmetric attack kernel *(fixes part of G6)*

Split `blurMs` into `preMs` / `postMs` (UI: one knob + a Skew control).
Implement as a two-sided Gaussian (different σ each side), still
energy-normalized, slope-limited, and peak-clamped by the existing rails.
Enables "pre-echo swell" (reverse-like attacks) vs. "smear forward" (softening)
as distinct sounds — currently conflated.

*Effort: S. All safety rails already exist and apply unchanged.*

### S2 · Frequency-dependent smear amount

A Bark-tilt weight on the per-onset effect: `amount(band) = mix · 10^(tilt ·
(bark − 12)/24 / 20)` clamped to [0, 1] per band/partial (partials weighted by
their frequency's band). Lets the user blur only the high-band click of an
attack while keeping the low thump, or vice versa. One knob (`Tilt`, dB-like,
default 0 = current behavior).

*Effort: S.*

### S3 · Onset-strength weighting

Scale each onset's effective mix by its normalized detection strength through a
soft threshold curve (`strengthFloor`, `strengthKnee`). Strong attacks get the
full effect; borderline detections are treated gently — reduces the audible
lottery at the detection threshold and pairs naturally with the planned
sensitivity UI (SMEARNOV_SPEC §4).

*Effort: S.*

### S4 · Transient transfer (Smearnov × Morph bridge)

Make the currently-inert **Transient lane** meaningful: morph the *attack
envelope shapes* between sources. For each protected onset (M1 windows), fit
the window's broadband gain contour for A and for B (normalized to peak), and
at lane amount `x` render A's attack through the interpolated contour
`(1−x)·gA(t) + x·gB(t)` (log-domain blend, ±12 dB soft-capped). B's punch on
A's material without touching sustain — rhythmic hybridization.

*Effort: M. Depends on: M1 (windows), S1 (contour machinery reuse).*

### S5 · Non-destructive Smearnov

Store `SmearnovParams` + onset list in session state and apply at
render/preview time; the current destructive `smearAttacks` becomes an explicit
**Commit** action. Undoable experimentation; consistent with the recipe-based
morph architecture. (The transform is cheap: window-local, runs in the offline
render prep.)

*Effort: M. Mostly plumbing; transform code unchanged.*

## 5. Proposed improvements — Hybridization

### H1 · Direction toggle *(fixes part of G5)*

Per Hybrid timeline point (or global setting): **A←B** (today), **B←A**. The
transfer code is symmetric — swap envelope roles and the base spectrum. With
the existing per-lane decoupling this completes the classic cross-synthesis
matrix (excitation of X + color of Y, both ways).

*Effort: S.*

### H2 · Donor-envelope formant controls

Two bounded warps applied to the donor envelope before transfer, reusing
`sampleAtBark`:

- **Formant shift:** Bark-axis offset, ±4 Bark, edge-held.
- **Formant stretch:** scale about a pivot (default 8 Bark), 0.5–2.0.

Turns the hybrid from a fixed transplant into a playable timbre control
("same vowel, bigger head"). Both default to identity; automatable.

*Effort: S–M.*

### H3 · Temporal-envelope transfer

Complementary to spectral transfer: impose the donor's broadband amplitude
contour on the recipient. Per chunk, compute each source's total-energy
envelope (smoothed, ~30 ms), and scale the recipient by
`(envDonor/envRecipient)^amount` with the divisor floored relative to the
recipient's own envelope peak (−48 dB, the validated relative-floor pattern)
and the result soft-knee capped (reuse `softCapEnvelopeBoost`). B's dynamics /
rhythm on A's spectrum — the temporal dual of the Hybrid lane, as a third
timeline point kind.

*Effort: M. Risk: low — every gain is bounded by existing rails.*

### H4 · "Noise transplant" preset (documentation, not code)

Partials-from-A + residual-from-B is already reachable: unlink the
ResidualNoise lane and set it to 1.0 with the main morph at 0. Ship it as a
named preset + help text rather than a new engine path.

*Effort: XS.*

### H5 · Temporal smoothing of transfer envelopes

Optional one-pole smoothing of the donor/recipient envelope log-bins across
chunks (time constant 0–200 ms). Frame-to-frame True-Envelope wobble currently
modulates the transfer ratio and can read as flutter at high transfer amounts;
smoothing the *envelope* (not the partials) removes it without dulling
attacks when M1's gates are active. Default 30 ms.

*Effort: S.*

## 6. Cross-cutting requirements

- **X1 — Numerical checks first.** Every stage lands with a synthetic-fixture
  test in the style of `spectral_envelope_transfer_test.cpp`: warp
  monotonicity + slope bounds (M2), harmonic pairing correctness on synthetic
  harmonic pairs (M3), mass conservation + cost non-increase vs. banded OT
  (M4), kernel energy preservation and asymmetry (S1), gain-bound proofs for
  every new multiplicative path (M1, S4, H2, H3, H5). Audible-path stages
  additionally require ear validation before default-on (per DSP_FIDELITY_PRD
  §2.5).
- **X2 — Bounded gain discipline.** No new gain path may exceed the source's
  intrinsic dynamic range: relative floors (−48 dB below own peak) for every
  divisor, soft-knee ceilings for every boost, raised-cosine ramps for every
  gate. This is the validated house rule; treat violations as review blockers.
- **X3 — Determinism & state.** All new parameters live in APVTS + the morph
  recipe (unknown-field-tolerant serialization already exists); no
  wall-clock or nondeterministic iteration anywhere in prepared renders.
- **X4 — Offline-prepared, RT-safe.** All heavy work (f0 estimation, DTW,
  global OT, track decomposition) runs in the existing offline render/prepare
  path; the per-chunk loop gains only table lookups and bounded arithmetic.

## 7. Prioritization

| Priority | Items | Rationale |
|---|---|---|
| **P1** | M1, S1, S3, H2, H3, H4 | Fix the shipped-but-inert transient policy (G1); small bounded features with immediate audible payoff |
| **P2** | M2, M4, S4, H1, H5, M6, S2 | Larger wins needing listening validation; unlock rhythmic/temporal hybridization |
| **P3** | M3, M5, S5 | Highest effort / architectural; schedule after P1–P2 prove out |

Suggested first milestone: **M1 + S1 + H3** — attack-correct morphs, asymmetric
smear, and temporal transfer cover the most-requested perceptual territory and
share the onset-window machinery.

## 8. Non-goals

- No neural/DDSP components (per DSP_FIDELITY_PRD §4).
- No change to ATS file-format core meaning; aux data additive and
  zero-initialized only.
- No live low-latency analysis; all preparation stays on worker threads.
- The explicit transient layer stays a core module + offline tool; nothing
  here wires it into the live plugin (deliberate prior decision — its ceiling
  under the dynamic-range cap was judged marginal).

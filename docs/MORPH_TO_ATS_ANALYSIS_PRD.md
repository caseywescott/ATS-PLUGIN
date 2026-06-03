# Product Requirements Document: Morph-to-ATS Analysis

**Document status:** Draft
**Last updated:** 2026-05-21
**Related docs:** [MODERN_ATSH_PRD.md](./MODERN_ATSH_PRD.md), [MORPH_TO_ATS_ANALYSIS_TASKS.md](./MORPH_TO_ATS_ANALYSIS_TASKS.md), [ATS spectral morphing brainstorm](../ATS-Plugin-Vault/ATS%20spectral%20morphing%20brainstorm.md)

---

## 1. Executive Summary

Morph-to-ATS analysis turns two analyzed ATS-style source models into a reproducible, editable morph recipe and renderable ATS-compatible model-domain representation. The first implementation should prioritize musical stability over theoretical completeness: group partials on a perceptual frequency grid, match partials locally inside each band, interpolate frequency and amplitude in perceptual domains, preserve band energy, and keep all expensive work off the audio thread.

The baseline feature is intentionally modest. It should make two ATS analyses morphable without requiring global matching across thousands of partials, while explicitly managing the three places where naive spectral morphs usually fail: ambiguous correspondence inside a band, energy bookkeeping when partial counts differ, and phase/time behavior around births, deaths, and transients.

---

## 2. Baseline Policy

The first morph analysis pass shall follow this policy:

1. **Perceptual frequency grid:** partition the spectrum using Bark bands by default, with Mel available as an alternate grid if listening tests justify it.
2. **Within-band partials:** assign sinusoidal partials to bands by instantaneous frequency per frame, with track centroid assignment available for stable trajectory views and diagnostics.
3. **Morph:** for each band, or for pooled partials inside that band, interpolate frequency and amplitude as a function of morph amount. Phase interpolation may be added only when it improves stable harmonic material without creating broader artifacts.

This is a reasonable first pass because it keeps matching local and bounded. It must not silently ignore the known risks:

- **Ambiguous correspondence:** multiple partials in one band need an explicit matching rule.
- **Energy bookkeeping:** unmatched partials and residual/noise energy must be conserved or intentionally faded.
- **Phase/time behavior:** partial births, deaths, count mismatches, and transients need deterministic policies.

---

## 3. Goals

- Produce a serializable morph recipe from two ATS analyses.
- Render intermediate morph states as deterministic ATS-compatible model snapshots.
- Keep the first implementation local by Bark/Mel band while leaving room for more advanced correspondence later.
- Preserve perceived loudness and spectral energy closely enough that morph sweeps do not pump, hollow out, or over-brighten.
- Avoid unstable partial identity swaps across adjacent frames.
- Treat sinusoidal partials and residual/noise bands as separate morph lanes.
- Make morph analysis repeatable from saved plugin state and suitable for offline render tests.

## 4. Non-Goals

- No live low-latency morph analysis in the first release.
- No neural or DDSP-assisted correspondence in the first release.
- No guarantee of transparent morphs for arbitrary dense polyphonic material.
- No full phase-vocoder or Griffin-Lim projection pass until the baseline model-domain morph is validated.
- No replacement of the classic ATS analysis path.

---

## 5. Users and Workflows

| User | Need |
| --- | --- |
| Sound designer | Blend two analyzed sources with a stable morph knob and minimal metallic smear. |
| Electroacoustic composer | Recreate ATSH-style morphing with saved recipes, repeatable renders, and partial-level control. |
| Plugin user | Load source A and source B, analyze both, choose a morph policy, automate morph amount, and save the DAW session. |
| Research-oriented user | Inspect correspondence, band energy, and render diagnostics for each morph strategy. |

Representative workflows:

1. Analyze a vocal vowel and a bowed string, create a Bark-band morph recipe, sweep morph amount, and render a stable hybrid tone.
2. Analyze a bell hit and a plucked string, preserve the attacks, and morph sustained partial/noise content separately.
3. Load two saved ATS models, inspect unmatched partials per band, choose the nearest-frequency policy, and export the morph recipe for regression tests.

---

## 6. Functional Requirements

### 6.1 Inputs and Outputs

- The feature shall accept two compatible ATS-style source models, referred to as source A and source B.
- Each source model shall include sample rate, frame times, partial ids, per-frame frequency/amplitude/phase where available, and residual/noise band energy where available.
- The output shall be a morph recipe plus renderable model snapshots for requested morph amounts.
- Prepared morph snapshots shall own immutable model-domain value data for partials, residual/noise output, energy diagnostics, and transient diagnostics.
- The recipe shall be stored separately from the source models so users can edit, re-run, and automate morph decisions without mutating the source analyses.

### 6.2 Perceptual Band Grid

- Bark bands are the default analysis grid for compatibility with ATS critical-band thinking.
- Mel bands may be added as an alternate preset after Bark behavior is covered by tests.
- Band definitions shall be deterministic and stored in morph metadata: scale type, band count, frequency bounds, and edge frequencies.
- Partials shall be assigned to bands by instantaneous frequency per frame for render-time morphing.
- Track centroid assignment shall be available for diagnostics and correspondence stabilization.

### 6.3 Within-Band Correspondence

- Each band shall build explicit source A to source B correspondence records before interpolation.
- Tier 0 correspondence may sort partials by frequency inside the band and pair by rank.
- Tier 0 correspondence records shall be deterministic and explicit: `matched`, `unmatched_source`, and `unmatched_target`.
- Tier 1 shall add greedy nearest-neighbor matching in log-frequency with amplitude-rank bias.
- Tier 1 cost diagnostics shall report log-frequency cost, weighted amplitude-rank cost, and total cost per matched record.
- Tier 2 may add assignment-matrix matching with cost terms for log-frequency, amplitude, SMR/confidence, and track continuity.
- Tier 2 shall remain behind an explicit advanced strategy flag and use bounded per-band candidate sets for predictable CPU.
- Correspondence shall be stable across frames through smoothing, hysteresis, or lifecycle-aware penalties.
- Hysteresis smoothing shall be measurable through correspondence churn metrics across adjacent frames.
- Unmatched partials shall have explicit policies: fade out, fade in, hold, merge into band residual energy, or discard only when user-selected.
- The default unmatched policy metadata shall fade source-only partials out and fade target-only partials in; discard shall require an explicit opt-in flag in saved recipe metadata.

### 6.4 Interpolation Domains

- Frequency interpolation shall default to log-frequency interpolation, with Bark-coordinate interpolation available as a perceptual-domain option.
- Frequency interpolation shall preserve in-grid morph endpoints exactly within floating-point tolerance and clamp out-of-grid output to saved grid bounds when clamping is enabled.
- Amplitude interpolation shall default to power interpolation.
- dB amplitude interpolation shall be available with a configurable floor, defaulting to -120 dB, so silent or very quiet partials do not create abrupt level jumps during sweeps.
- Morph amount shall be a normalized value from 0 to 1 where 0 reproduces source A and 1 reproduces source B within documented tolerance.
- The engine shall support independent morph curves for frequency, amplitude, residual/noise energy, time, and optional phase.
- Linked morph lanes shall evaluate their selected curve against the main morph amount; unlinked lanes shall evaluate their selected curve against their own saved amount. Default linked linear curves shall preserve one-knob morph behavior.
- Frequency interpolation shall clamp or gracefully migrate across band edges so partials do not jump discontinuously between bands.

### 6.5 Energy Bookkeeping

- The morph engine shall track per-band sinusoidal power, residual/noise power, and total power before and after interpolation.
- Count mismatches shall conserve band power within a documented tolerance unless the selected policy intentionally changes level.
- When multiple partials merge into one target, the target power shall be summed or redistributed rather than dropping excess energy.
- When one partial splits into multiple targets, added targets shall fade in deterministically from source B energy.
- Default FadeOut/FadeIn unmatched handling shall compare output sinusoidal plus residual-routed power against the expected per-band power interpolation, with warning bands emitted when the delta exceeds the configured tolerance.
- Energy diagnostics shall be available for tests and UI inspection.

### 6.6 Phase and Time Behavior

- The MVP may synthesize intermediate partials without phase interpolation if oscillator phase continuity is maintained by the renderer.
- If phase interpolation is enabled, it shall use a named strategy such as source-A phase donor, source-B phase donor, relative phase preservation within harmonic clusters, or direct phase blend.
- The default saved phase strategy shall be oscillator continuation; direct blend shall preserve exact source/target phase endpoints when both phases are available, while births/deaths shall use the available donor phase or renderer continuation.
- The engine shall avoid phase resets except at explicit births, deaths, or transient boundaries.
- Time alignment shall default to normalized model time. Future strategies may include source-length scaling, anchor markers, onset alignment, and beat/tempo-aware warping.
- Track births and deaths shall fade over configurable frame windows to avoid clicks and zippering.
- Saved lifecycle settings shall include birth fade frames, death fade frames, fade curve, and a maximum per-frame gain-step threshold used to flag click/zipper risks around unmatched partial transitions.

### 6.7 Residual and Noise Morphing

- Sinusoidal partials and residual/noise bands shall morph through separate lanes.
- Residual/noise band energy shall use smoother amplitude-only interpolation by default.
- Orphan residual energy from modern ATS models shall remain separate from partial-attached noise.
- Residual/noise diagnostics shall report partial-attached noise power and orphan residual power separately per band, including source, target, and morphed output power.
- The renderer shall expose independent gain paths for partial, partial-noise, and orphan residual lanes when the underlying model supports them.
- Prepared morph snapshot previews shall render banded residual/noise power through deterministic per-band noise carriers rather than collapsing all residual energy to one pitched carrier.

### 6.8 Transient Handling

- Transient markers from source models shall be carried into the morph recipe.
- Saved transient settings shall include a named policy: source A attack, source B attack, crossfade attacks, or sustained-only morph after attack.
- Near strong onsets, the engine shall support biasing toward source A, biasing toward source B, crossfading transient buffers, or bypassing phase-sensitive interpolation in the protected window.
- The first release may protect attacks by choosing a source transient and morphing sustained content after the transient protection window.
- Morph analysis shall compare source onsets by time rank and report warnings when the signed target-minus-source offset exceeds the saved onset-alignment tolerance.

### 6.9 UI and Automation

- The plugin UI shall expose source A/source B slots, morph amount, grid type, correspondence strategy, unmatched-partial policy, interpolation domains, phase strategy, transient policy, and independent lane gains.
- Morph amount shall be host-automatable and safe to change on the fly.
- The host parameter id for the main A/B sweep is `morphAmount`; 0.0 means source A, 0.5 means midpoint, and 1.0 means source B.
- Frequency, amplitude, residual/noise, time, transient, and optional phase lanes shall be linkable to the main morph amount or independently controllable.
- Parameters intended for performance shall support smoothing, reset-to-linked behavior, host automation, and future MIDI learn.
- Diagnostics shall show band count, matched/unmatched partial counts, energy delta, warning bands, correspondence churn, and transient alignment warnings.
- Advanced correspondence controls may be hidden behind an expert panel, but the selected policy must be visible in saved state.
- The UI shall make the simple path immediate while keeping expert controls close: one morph control for performance, linked lanes for shaping, and band/partial inspection for debugging.
- The first morph preset set shall include Morph stable default, Morph tonal, Morph vocal/formant, Morph percussive, and Morph noisy texture factory presets.

### 6.10 Morph Workspace UI Design

The primary UI shall be an A/B morph workspace with source A on the left, source B on the right, and morph controls in the center:

```text
Source A          Morph Engine                         Source B
[ATS slot]        [large morph knob / XY pad]           [ATS slot]
[spectrogram]     [frequency | amplitude | noise]       [spectrogram]
[partials]        [time | phase | transient lanes]      [partials]

                [Bark/Mel band morph strip]
                [correspondence + energy diagnostics]

                [selected band / partial inspector]
```

#### 6.10.1 Source Panels

- Each source panel shall show model name, duration, sample rate, partial count, residual/noise band availability, source gain, and time offset.
- Each source panel shall include a compact spectrogram or partial-track overview with transient markers when available.
- Source actions shall include analyze/replace, normalize, listen source A, listen source B, and set time anchors when time alignment is supported.
- Source warnings shall be visible near the source that caused them: missing phase, missing residual/noise data, incompatible frame grid, or missing transient markers.
- The first UI pass may store Source A/B as morph-recipe metadata copied from the current analyzed model while full per-source model blobs, normalization gain writes, and time-anchor editing are completed in later commits.

#### 6.10.2 Central Morph Controls

- The center control shall provide a large morph knob or horizontal macro control where 0 is source A, 0.5 is the hybrid midpoint, and 1 is source B.
- An optional XY pad shall map X to A/B morph amount and Y to a performance macro such as stable-to-expressive, residual emphasis, or smoothing intensity.
- The first XY-pad Y parameter id is `morphPerformanceMacro`; it is saved as performance metadata for later mapping to stable-to-expressive, residual emphasis, or smoothing behavior.
- The morph workspace shall include an in-plugin Morph Timeline where users can draw breakpoint curves over the internal source duration, render that curve, stop, edit, and replay without relying on host automation.
- The first Morph Timeline stores normalized time/value breakpoints as recipe UI metadata and renders a prepared preview buffer from short morph snapshots with boundary crossfades.
- The UI shall expose lane controls for frequency, amplitude, residual/noise, time, transient, and optional phase.
- Each lane shall provide a linked/unlinked state. Linked lanes follow the main morph amount; unlinked lanes expose their own amount controls.
- Lane parameter ids shall use `morphLane*Amount` and `morphLane*Linked` pairs for frequency, amplitude, residual/noise, time, transient, and phase; control smoothing uses `morphSmoothingMs`.
- The UI shall provide quick snapshot buttons for useful positions such as A, 25%, 50%, 75%, and B, plus user variation slots when preset/state support is available.
- The first state-backed variation pass shall provide three user slots that save and recall the main morph amount, performance macro, smoothing, and lane amount/link state.

#### 6.10.3 Perceptual Band Strip

- A horizontal Bark/Mel band strip shall show per-band source A energy, source B energy, current morphed energy, matched partial count, unmatched partial count, and warning state.
- Clicking a band shall open a selected-band inspector with grid, correspondence strategy, unmatched policy, frequency interpolation, amplitude interpolation, smoothing, and energy tolerance controls.
- Band warnings shall call out energy deltas, unstable correspondence, excessive unmatched partials, and transient/onset conflicts.
- The band strip shall remain useful in default mode without requiring users to inspect individual partials.
- Band strip diagnostics shall be generated from copied morph recipe state on the UI/message side; the audio thread shall not compute band summaries, allocate diagnostic arrays, or inspect correspondence records.

#### 6.10.4 Partial and Policy Inspector

- The inspector shall show correspondence pairs, unmatched partials, selected policy, match costs when available, and lifecycle state around births/deaths.
- Unmatched policy controls shall include fade out, fade in, hold, merge into residual, split/redistribute energy, and discard only as an explicit expert choice.
- Policy parameter ids shall include `morphGridType`, `morphCorrespondenceStrategy`, `morphUnmatchedSourcePolicy`, `morphUnmatchedTargetPolicy`, `morphDiscardOptIn`, `morphFrequencyInterpolationDomain`, and `morphAmplitudeInterpolationDomain`.
- Phase controls shall default to oscillator continuation and expose source A donor, source B donor, relative harmonic cluster preservation, and direct blend only when supported by the engine.
- Transient controls shall expose source A attack, source B attack, crossfade attacks, and sustained-only morph after attack.
- Phase and transient policy parameter ids shall be `morphPhaseStrategy` and `morphTransientPolicy`.

#### 6.10.5 Live Editing Requirements

- User changes to morph amount, lane amounts, lane link state, gains, smoothing, and snapshots shall be safe during playback.
- Expensive changes such as correspondence strategy, grid type, time alignment mode, or assignment matching shall schedule a background recipe rebuild rather than run on the audio thread.
- Pending rebuilds shall keep the last valid snapshot playing until the new snapshot is ready.
- UI controls shall clearly indicate stale, rebuilding, ready, and warning states.

### 6.11 Persistence

- Plugin state shall include morph recipe metadata, source model identifiers or embedded blobs, grid settings, correspondence strategy, unmatched policies, interpolation domains, smoothing constants, and transient policy.
- Plugin state shall persist the `morphAmount` automation value and mirror it into the recipe default morph amount for deterministic reloads.
- Plugin state shall persist `morphPerformanceMacro`, `morphSmoothingMs`, lane amounts, and lane link states so live controls survive session reloads.
- Plugin state shall persist three morph variation slots containing user snapshot names, saved morph amounts, performance macro values, smoothing values, and lane amount/link states.
- Plugin state shall persist morph policy controls and mirror recipe-backed policies into morph recipe fields; policy-only UI selections shall be visible as deterministic `ui.*` recipe metadata.
- Applying a morph factory preset shall write `ui.morph_preset_name` and `ui.morph_preset_description` metadata into the recipe so saved sessions retain the policy rationale.
- Saved recipes shall be deterministic across reloads for the same source model hashes.
- Unknown future recipe fields shall be preserved where practical.

---

## 7. Technical Architecture

### 7.1 Engine Boundaries

- **Analysis source loader:** validates that source A and source B models are compatible enough to morph.
- **Morph analyzer:** groups partials into perceptual bands, builds correspondence, records diagnostics, and creates a morph recipe.
- **Morph renderer:** evaluates the recipe at a morph amount and produces an immutable render snapshot off the audio thread.
- **Synthesis engine:** reads prepared ATS renders or morph snapshot preview buffers on the audio thread with no analysis, file I/O, allocation, locks, or logging.
- **UI/plugin layer:** owns user choices, progress, cancellation, visualization, host automation, and state persistence.

### 7.2 Data Model Additions

The morph recipe shall store:

- recipe version and engine version,
- source A/source B ids and source hashes,
- grid type and band definitions,
- frame/time alignment policy,
- per-band correspondence maps,
- unmatched-partial policies,
- interpolation domains and smoothing constants,
- phase strategy,
- residual/noise lane strategy,
- transient strategy,
- UI lane link states, performance macro settings, and snapshot slots,
- diagnostics summary and warnings.

### 7.3 Determinism

- Morph analysis shall be deterministic for the same source models, parameters, and engine version.
- Any stochastic residual/noise synthesis shall use stored seeds for offline tests.
- Floating-point tolerances shall be documented for snapshot and audio golden tests.

---

## 8. Quality and Acceptance

### 8.1 Fixtures

Use a compact suite that includes:

- sustained harmonic tone,
- vibrato tone,
- vowel or voice-like source,
- bell or inharmonic metallic source,
- plucked/percussive source,
- noisy/breathy source,
- dense/polyphonic stress sample.

### 8.2 Metrics

- Endpoint fidelity at morph 0 and morph 1.
- Per-band power delta across a morph sweep.
- Partial correspondence churn across adjacent frames.
- Frequency and amplitude trajectory smoothness.
- Click/transient artifact checks around births, deaths, and attacks.
- CPU and memory cost for representative partial/frame counts.
- Morph performance profiles shall compare frequency-rank, greedy-nearest, and assignment-matrix correspondence using deterministic cost estimates, candidate-edge counts, evaluated-assignment counts, and estimated analysis/snapshot memory.
- Initial UI warning thresholds shall be: more than 512 active partial candidates per frame, more than 4096 frames, more than 2048 candidate edges in one band, more than 4096 evaluated assignment candidates, more than 64 MiB estimated analysis correspondence memory, or more than 8 MiB estimated snapshot memory.

### 8.3 Acceptance Criteria

- Morph amount 0 and 1 reproduce source A and source B render snapshots within documented tolerance.
- A slow morph sweep is deterministic across repeated offline renders.
- No unexplained per-band power loss or gain exceeds the configured tolerance in default mode.
- Correspondence maps do not flap frame-to-frame on sustained/vibrato fixtures.
- Count mismatches produce fades, merges, splits, or residual redistribution according to explicit policy.
- Synthesis remains real-time-safe when playing prepared morph snapshots.
- Prepared morph snapshot playback shall use pre-rendered partial/residual layer buffers so live gain changes and buffer swaps do not require audio-thread allocation or analysis.
- Performance diagnostics shall surface soft-cap warnings before users commit to expensive grid, correspondence, or snapshot settings.

---

## 9. Risks and Open Questions

- **Correspondence ambiguity:** Bark bands reduce global complexity but can hide multiple musically different partials in the same bin.
- **Energy vs clarity:** strict power conservation can make dense bands sound crowded; musical presets may need controlled energy shaping.
- **Phase expectations:** direct phase interpolation may sound worse than stable oscillator continuation for many sources.
- **Time alignment:** normalized time is simple but weak for sources with different onset structures.
- **UI scope:** exposing every policy can overwhelm users; defaults must be musically useful.
- **Model compatibility:** classic ATS files may lack diagnostics needed by advanced matching.

Open decisions before implementation:

- Should Mel be shipped in MVP or remain behind tests until Bark is validated?
- Should the default unmatched-partial policy move from fade out/fade in to merge-into-residual after residual energy rendering is validated?
- How much endpoint error is acceptable for morph amount 0 and 1 after domain interpolation and smoothing?
- Should transient protection choose one source by default or crossfade transient buffers when both are available?

---

## 10. References

- [Modern ATSH analysis, morphing, and synthesis PRD](./MODERN_ATSH_PRD.md)
- [ATS spectral morphing brainstorm](../ATS-Plugin-Vault/ATS%20spectral%20morphing%20brainstorm.md)

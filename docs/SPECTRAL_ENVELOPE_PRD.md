# Product Requirements Document: General Spectral-Envelope Transfer for ATS Hybridization

**Document status:** Draft
**Last updated:** 2026-04-21
**Related docs:** [MODERN_ATSH_PRD.md](./MODERN_ATSH_PRD.md), [SPECTRAL_ENVELOPE_TASKS.md](./SPECTRAL_ENVELOPE_TASKS.md), [SPECTRAL_ENVELOPE_FIXTURES.md](./SPECTRAL_ENVELOPE_FIXTURES.md), [SPECTRAL_ENVELOPE_BASELINES.md](./SPECTRAL_ENVELOPE_BASELINES.md)

---

## 1. Executive Summary

This PRD defines an improved spectral-envelope analysis and transfer system for ATS hybridization and vocoding. The core workflow is: estimate a continuous spectral envelope for each ATS frame, de-color one source into a spectral residual, then apply another source's envelope with independent controls for partials, residual/noise bands, time, and transients.

The default implementation must not assume the input is speech. It should work on the ATS model already available to the plugin: sparse partial frequencies/amplitudes, SMR/relevance data, residual critical-band energy, and optional transient markers. Speech-oriented and F0-adaptive methods remain useful as optional analysis sources, but the product default is a general sparse-partial envelope estimator suited to voices, instruments, field recordings, noisy textures, and inharmonic material.

---

## 2. Background and Design Position

### 2.1 Why spectral-envelope transfer matters

Classic vocoding often applies the spectral shape of one sound to the excitation or carrier of another. In an ATS context, the same idea becomes more editable because partial amplitudes, frequencies, phases, SMR, and residual bands are explicit model parameters. This enables hybrid sounds where one source supplies fine partial/residual detail and another supplies broader timbral contour.

### 2.2 Recommended default

Use a **regularized, weighted discrete cepstrum** as the default envelope estimator for ATS partial frames.

Reasons:

- ATS frames already provide sparse amplitude samples at partial frequencies.
- Regularized discrete cepstrum directly fits a continuous log-amplitude envelope to discrete frequency points.
- Regularization controls ill-conditioning when partials are sparse, clustered, missing, or uneven.
- Perceptual frequency warping can emphasize low/mid formant-like regions without assuming speech.
- Weights can use ATS confidence signals such as SMR, amplitude, track stability, and user selection.

### 2.3 Optional estimators

The system shall support a pluggable estimator interface so other envelope sources can be compared and used intentionally:

- **True Envelope:** useful when a dense spectrum is available or when the desired envelope is the upper spectral contour.
- **CheapTrick / WORLD-style F0-adaptive envelope:** useful for voiced speech or singing and some monophonic pitched instruments when waveform access and reliable F0 exist, but not the default for general ATS hybridization.
- **LPC / all-pole:** useful as a lightweight or formant-oriented option, but not the default because all-pole models can overfit isolated partials, especially in high-pitched or inharmonic material.
- **Sampled / interpolated envelope:** useful for debugging, UI visualization, and conservative transformations.

No estimator may be described in the UI or API as universally best. The estimator choice is part of the sound-design result.

---

## 3. Goals

### 3.1 Product Goals

- Provide a musically useful hybridization engine that transfers broad spectral shape without destroying source detail.
- Preserve generality across speech, singing, instruments, inharmonic sources, percussive sources, field recordings, and noisy/residual-heavy material.
- Make envelope estimation deterministic, inspectable, and testable per frame.
- Expose an expressive morph/vocode control: source self-envelope removal, target envelope application, transfer amount, normalization, smoothing, and residual treatment.
- Keep all heavy analysis and envelope baking off the audio thread.

### 3.2 Non-Goals

- No assumption that source or target has a stable F0.
- No speech-specific formant tracker as the default implementation.
- No neural model dependency in the MVP.
- No promise that a single envelope definition is correct for all material.
- No live low-latency analysis of arbitrary input as part of this feature.

---

## 4. Users and Use Cases

| User | Need |
| --- | --- |
| Electroacoustic composer | Hybridize partial structures from two analyzed sounds while preserving ATS-style editability. |
| Sound designer | Apply the resonant contour of one sample to another without simple EQ-copy artifacts. |
| Producer | Create vocoder-like blends from instruments, voice, drums, and textures inside a DAW session. |
| Research-oriented user | Compare envelope estimators and export reproducible analysis data. |

Representative workflows:

1. Analyze a bowed string and a metal object, then apply the metal object's broad spectral envelope to the string's partial residual.
2. Analyze voice and synth, then use voice envelope transfer only where voiced partial confidence is high while keeping unvoiced/noise bands separate.
3. Analyze two inharmonic sounds, align frames by time or transient markers, and morph amplitudes using weighted discrete cepstrum envelopes rather than harmonic-number assumptions.
4. Analyze a percussive source and a sustained source, preserving transient buffers while applying only the sustained spectral contour to post-attack material.

---

## 5. Functional Requirements

### 5.1 Envelope Data Model

Each frame-level envelope shall store:

- estimator id and version,
- source model id, frame index, sample rate, and frequency axis,
- frequency warp type and parameters,
- cepstral/order or estimator-specific parameters,
- regularization strength and penalty type,
- fitted log-envelope coefficients or sampled envelope values,
- fit diagnostics: number of points, weighted error, condition estimate if available, clipping/flooring count, and warnings,
- smoothing state or time-smoothing metadata when the envelope is part of a time series.

Envelope data must be serializable independently from destructive transforms so the same analysis can be reused by multiple recipes.

### 5.2 Default Estimator: Weighted Regularized Discrete Cepstrum

The default estimator shall fit a log-amplitude envelope from the active partials in an ATS frame.

Inputs:

- partial frequency,
- partial amplitude,
- optional phase is ignored by the estimator but retained for synthesis,
- SMR or partial confidence,
- track stability/confidence when available,
- user partial selection mask,
- source residual-band energy as an optional auxiliary input.

Requirements:

- Fit in log amplitude or dB, with amplitude floors to avoid undefined logs.
- Support linear, Bark, mel, ERB, and custom normalized frequency axes.
- Use Tikhonov-style smoothness regularization by default.
- Support first-derivative and second-derivative smoothness penalties.
- Support weights from SMR, amplitude, track confidence, and user overrides.
- Handle sparse frames gracefully by lowering order, increasing regularization, falling back to sampled/interpolated envelopes, or marking the frame as unreliable.
- Never require a harmonic grid or F0.

### 5.3 Estimator Interface

Envelope estimators shall share a common interface:

- `prepare(context)`: configure sample rate, frequency axis, maximum frequency, and analysis policy.
- `estimate(frame)`: produce a frame envelope plus diagnostics.
- `evaluate(envelope, frequency)`: sample envelope gain/log amplitude at arbitrary frequency.
- `serialize(envelope)`: persist estimator-specific data.
- `diagnostics()`: expose warnings and quality metrics for UI/tests.

The interface must allow dense-spectrum estimators, sparse-partial estimators, and externally supplied envelopes without changing the hybridization engine.

### 5.4 Envelope Transfer Model

The core hybridization transform shall operate in log amplitude:

```text
log_new_amp = log_source_amp
            + remove_amount * (neutral_env - log_source_env_at_source_freq)
            + apply_amount  * (log_target_env_at_mapping_freq - neutral_env)
```

Equivalent ratio-domain implementations are acceptable when numerically guarded.

The MVP shall expose simpler user-facing controls:

- **Source de-color:** how much source envelope is removed.
- **Target color:** how much target envelope is applied.
- **Envelope mix:** interpolation between source and target envelope.
- **Gain normalization:** none, frame RMS, loudness-like, or energy-preserving.
- **Frequency mapping:** direct Hz, normalized frequency, source-to-target range warp, or morph-correspondence map.
- **Smoothing:** frame-to-frame envelope smoothing and control smoothing.

The transform must avoid double-coloring by default. Applying a target envelope to already colored partial amplitudes without source-envelope removal should be an explicit creative option.

### 5.5 Frequency Mapping and Correspondence

The engine shall support target-envelope lookup at frequencies independent of target partial indices.

Mapping modes:

- direct frequency lookup,
- normalized Nyquist lookup,
- perceptual-warp lookup,
- range-warped lookup between source/target spectral extents,
- morph correspondence lookup when partial labels are available.

Harmonic-number matching may be offered only when reliable F0 or harmonicity metadata exists. It must not be required for non-speech or inharmonic material.

### 5.6 Residual and Noise Handling

Envelope transfer shall treat ATS residual data as first-class material.

Requirements:

- Estimate and transfer partial-envelope gains separately from residual/noise-band color.
- Preserve orphan residual-band energy from the modern ATSH model.
- Offer residual mapping modes: unchanged, target-colored, source-decolored-plus-target-colored, crossfaded, or independent residual-envelope morph.
- Avoid using partial-only envelopes as the sole truth for noise-heavy frames.
- Preserve residual energy normalization independently from sinusoidal partial normalization.

### 5.7 Transient Handling

Transient windows shall be protected from inappropriate smooth envelope transfer.

Requirements:

- Detect or consume transient markers from the modern ATSH analysis.
- Allow per-transient policies: preserve source transient, target-color transient, crossfade transient source, or bypass envelope transfer.
- Avoid frame smoothing across strong transients unless the user explicitly enables it.
- Store transient policy in the hybridization recipe.

### 5.8 UI/UX Requirements

The UI shall expose:

- source and target model slots,
- estimator selection and preset controls,
- visual envelope overlays for source, target, and resulting transfer gain,
- per-frame diagnostics and warnings,
- de-color/apply/mix controls,
- residual/noise handling controls,
- transient policy controls,
- A/B render preview and deterministic offline render.

Expert controls should be available, but the default preset should be musically safe: weighted regularized discrete cepstrum, Bark or ERB warp, conservative order, source de-color enabled, target color partially applied, residual preserved unless explicitly colored.

---

## 6. Technical Architecture

### 6.1 Engine Boundaries

- **Envelope analysis:** worker-thread/offline only; may allocate and solve linear systems.
- **Hybridization recipe:** serializable control object containing source ids, target ids, estimator settings, transfer controls, mappings, residual policy, transient policy, and normalization.
- **Render preparation:** creates immutable model snapshots or baked gain curves.
- **Audio thread:** reads immutable snapshots or pre-rendered buffers; no envelope fitting, matrix solves, file I/O, locks, or allocations.

### 6.2 Data Flow

1. Source and target ATS models are selected.
2. Envelope estimator analyzes frames for each source as needed.
3. Diagnostics are shown and unreliable frames are marked.
4. A hybridization recipe maps source frames/partials to target envelope samples.
5. Source amplitudes are decolored and target envelope color is applied in log domain.
6. Residual/noise and transient policies are applied.
7. The result is baked into a new ATS model snapshot or an offline render buffer.

### 6.3 Determinism

- Envelope fitting shall be deterministic for a fixed model, settings, and toolchain.
- Randomized residual/noise synthesis shall use explicit seeds.
- Fallback decisions must be recorded in diagnostics.
- Cross-platform bit identity is not required; tolerance-based tests are acceptable.

---

## 7. Quality and Acceptance

### 7.1 Fixture Suite

Use general audio fixtures:

- sustained monophonic pitched instrument,
- speech/voice fixture, but not as the only fixture,
- singing voice fixture,
- inharmonic bell/metal fixture,
- noisy flute/bowed/noise-rich fixture,
- plucked or percussive attack fixture,
- dense polyphonic excerpt,
- synthetic sparse-frame edge cases.

### 7.2 Metrics

Measure:

- envelope fit error at weighted partial points,
- smoothness and over-oscillation metrics,
- frame-to-frame envelope shimmer,
- partial amplitude discontinuities after transfer,
- residual energy preservation,
- transient sharpness before/after transfer,
- CPU/memory cost of envelope fitting and baking,
- deterministic recall of saved recipes.

### 7.3 Acceptance Criteria

- Default RDC estimator handles sparse and non-harmonic fixtures without F0.
- Source de-color plus target color produces lower double-coloring artifacts than target-only scaling in fixture comparisons.
- Residual-heavy fixtures retain controllable noise color without collapsing all energy onto partials.
- Transient fixtures do not smear attacks under the default preset.
- Hybridization recipes serialize and reload with equivalent output within tolerance.
- No envelope estimation work runs on the audio thread.

---

## 8. Risks and Open Questions

- **Envelope definition ambiguity:** different material wants different envelopes; solve with presets, diagnostics, and estimator choice.
- **Over-smoothing vs overfitting:** aggressive smoothness can erase useful resonances; low regularization can create oscillations.
- **Sparse frames:** some frames may not contain enough reliable points for a high-order envelope.
- **Noisy material:** partial-only envelopes may be misleading; residual-band envelopes need equal care.
- **Stereo/multichannel:** decide whether envelopes are mono-summed, per-channel, or mid/side.
- **State size:** storing frame-level sampled envelopes can be large; prefer coefficients plus cache invalidation where possible.
- **User expectation:** "vocoder" often implies speech; product copy should describe spectral-envelope transfer for general sounds.

---

## 9. References

- Olivier Cappe, Jean Laroche, and Eric Moulines, "Regularized Estimation of Cepstrum Envelope from Discrete Frequency Points": https://www.di.ens.fr/olivier.cappe/Publications/Self-archive/rdc_mo95.pdf
- Axel Robel and Xavier Rodet, "Efficient Spectral Envelope Estimation and its Application to Pitch Shifting and Envelope Preservation": https://dafx.de/paper-archive/2005/P_030.pdf
- Masanori Morise, "CheapTrick, a spectral envelope estimator for high-quality speech synthesis": https://www.sciencedirect.com/science/article/pii/S0167639314000697
- Diemo Schwarz and Xavier Rodet, "Spectral Envelope Estimation and Representation for Sound Analysis-Synthesis": https://quod.lib.umich.edu/i/icmc/bbp2372.1999.417
- DXARTS ATS overview: https://dxarts.washington.edu/wiki/analysis-transformation-and-synthesis-ats

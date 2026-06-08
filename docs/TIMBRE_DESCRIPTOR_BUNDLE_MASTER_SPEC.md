# Timbre Descriptor Bundle - Repo-Fit Master Implementation Spec

**Status:** Reviewed and amended for the ATS repository (2026-06-08)
**Scope:** Deterministic timbre analysis, diagnostics, and a small set of controllable
descriptor targets for spectral-envelope hybridization and morph rendering
**Out of scope:** Symbolic/MIDI descriptors, neural timbre embeddings, and live input analysis
**Implementation tasks:** [TIMBRE_DESCRIPTOR_BUNDLE_TASKS.md](./TIMBRE_DESCRIPTOR_BUNDLE_TASKS.md)

---

## 1. Review outcome

The original draft contained useful research, but it mixed three different things:

1. descriptors that ATS can compute reliably,
2. transforms or correspondence techniques that are not descriptors, and
3. code paths that do not exist in this worktree.

This revision keeps the useful descriptor work, adds several important missing families, and
maps delivery onto the repository as it exists.

### 1.1 Important repository reality

There are currently two relevant implementation lines:

| Line | What exists |
|------|-------------|
| This `hybrid-function` worktree | General envelope analysis and hybrid transfer: `src/core/SpectralEnvelope*`, `EnvelopeFrameInput`, `FrameEnvelope`, `SpectralEnvelopeTransferRecipe`, residual and transient policies, overlays, and fixture metrics |
| Main repo / `dsp-fidelity-tier1` line | Morph recipe/render path: `src/core/AtsMorphRecipe.*`, optimal-transport correspondence, `src/core/AtsSpectralEnvelope.*`, `renderMorphTrajectoryBuffer`, Hybrid and EnvelopeMorph timeline lanes |

The descriptor core can be implemented and tested in this worktree. Audible morph-lane work
must be integrated after the generalized envelope stack and the mainline morph stack are
reconciled. The spec must not pretend that `AtsMorphRecipe.cpp`,
`renderMorphTrajectoryBuffer`, or `MorphLane::Brightness` exists on this branch today.

### 1.2 Corrections to the original proposal

- Frequency scaling does **not** preserve pitch. Scaling partial frequencies changes pitch and
  register unless a separate compensation stage is used.
- Spectral centroid, SSC, spread, and moments need an explicit weighting convention. This spec
  uses **energy weights** by default: partial weight `a_i^2`, residual-band weight `energy`.
- A single SSC difference is constant for every candidate inside a band, so it cannot improve
  within-band partial pairing by itself. SSC is useful for band alignment and diagnostics.
- Skewness crossing zero is not a CI failure. A valid morph may naturally change skewness sign.
- ICA, QPC/bispectrum priors, and a learned cluster-distance beta are research techniques, not
  suitable Phase 2 correspondence descriptors for this repo.
- Tristimulus, odd/even balance, harmonicity, and inharmonicity require reliable F0/harmonic
  assignment. They must be conditional and invalid on general inharmonic/polyphonic material.
- Brightness, tilt, sharpness, and spectral-envelope transfer overlap but are not identical.
  They must remain separately named and measured.
- Not every descriptor should become a lane. Most descriptors are diagnostics or fixture
  metrics; only a small controllable subset belongs in the render path.

---

## 2. Product goal

Add a deterministic, inspectable timbre-description layer over ATS partials, residual critical
bands, optional fitted envelopes, and transient metadata.

The bundle has three jobs:

1. **Analysis and QA:** explain why two sources or a hybrid sound different and catch regressions.
2. **Mapping and presets:** help choose envelope mapping, normalization, residual, and transient
   policies.
3. **Controlled rendering:** provide a few bounded targets for brightness, tilt, and
   tonal/noise balance without changing pitch or invalidating endpoint fidelity.

The descriptor bundle is not a replacement for the spectral envelope. The envelope is a
frequency-dependent representation; descriptors are compact measurements derived from the
same frame or series.

---

## 3. Design contract

### 3.1 Non-negotiables

| Rule | Reason |
|------|--------|
| Analysis is pure and deterministic | Same frame + options must produce the same descriptor values within floating-point tolerance |
| No heavy work on the audio thread | Series analysis, envelope fitting, roughness, and export run on workers/offline |
| Domain and weighting are serialized | `centroid_hz` is meaningless if callers do not know whether it used magnitude, power, partials, or combined residual energy |
| Validity is explicit | Harmonic descriptors must report unavailable rather than inventing F0 or harmonic numbers |
| Level-dependent and level-invariant values are separate | Gain normalization must not silently change a shape descriptor |
| Bark is a default, not a hard-coded universal grid | Reuse `EnvelopeAxisSpec`; support Bark, ERB, Mel, and Linear Hz where appropriate |
| Endpoint fidelity remains pinned | Descriptor-controlled morph amount 0/1 must reproduce A/B within existing morph tolerances |
| Existing recipes migrate safely | New lanes or fields require recipe schema migration and defaults that preserve old output |

### 3.2 Canonical energy model

For a partial with linear amplitude `a_i`:

```text
partial_energy_i = a_i^2
```

For a residual critical band, use the stored band energy directly:

```text
combined_energy = sum(partial_energy_i) + sum(residual_band_energy_b)
noisiness = residual_energy / combined_energy
```

Descriptors that use a different convention must include it in their options and serialized
metadata. The default descriptor path must not mix amplitude-weighted partial statistics with
power-weighted residual statistics.

### 3.3 Separate descriptor domains

Each value must identify its source domain:

| Domain | Input | Appropriate examples |
|--------|-------|----------------------|
| `partials` | Active sinusoidal partials only | partial centroid, inharmonicity, roughness |
| `residual_bands` | ATS critical-band energy | residual centroid, residual flatness |
| `combined` | Partial energy plus residual-band energy | perceived brightness proxy, rolloff, noisiness |
| `fitted_envelope` | Uniform samples from a `FrameEnvelope` or Bark envelope | tilt, contrast, crest, formant peaks |
| `waveform_spectrum` | Optional dense waveform spectrum | spectral flux/HFC validation, waveform-only comparisons |

### 3.4 Analysis values versus render controls

Descriptors are categorized before implementation:

- **MVP analysis/QA:** cheap, stable, and valid on general ATS material.
- **Conditional analysis:** useful only with reliable F0, harmonic labels, waveform data, or a
  fitted envelope.
- **Render target:** has a bounded transform, an auditable target error, and a clear endpoint
  contract.
- **Macro control:** a curated musical control that may route to one primary render target and
  monitor several secondary descriptors.
- **Analysis overlay:** may be useful on the timeline without implying that it is directly
  controllable.
- **Research only:** expensive, unstable, learned, or unsupported by the current representation.

### 3.5 Engine lanes versus descriptor motion

The existing mainline `MorphLane` values describe independent synthesis dimensions: frequency,
amplitude, residual/noise, time, transient, and phase. They are fixed engine controls and must not
grow one enum entry per timbre descriptor.

Descriptor motion is a separate dynamic control layer:

- descriptor curves address stable descriptor ids rather than enum positions,
- only descriptors with an approved inverse transform may affect rendering,
- analysis-only descriptors can still appear as timeline overlays,
- macros may route one gesture to several approved targets,
- arbitrary descriptor curves live in versioned recipe state rather than becoming hundreds of
  host parameters,
- a small stable macro set may be exposed to host automation and future MIDI learn.

This separation preserves the existing six-lane one-knob morph behavior while allowing the
Timbre Motion system to expand without breaking recipe arrays, APVTS parameter layouts, or host
automation sessions.

---

## 4. Descriptor inventory

### 4.1 MVP analysis and QA

These are the first implementation set.

| Descriptor | Domain | Why it matters in ATS | Initial use |
|------------|--------|-----------------------|-------------|
| Partial, residual, and combined centroid | all three energy domains | Separates deterministic brightness from noise brightness | QA; future brightness target |
| Centroid on Bark/ERB axis | combined | More perceptually uniform than raw Hz for comparison/interpolation | Fixture comparison |
| Spectral spread | partials and combined | Detects bandwidth collapse/expansion | QA |
| Spectral skewness and kurtosis | partials and combined | Describes distribution shape; useful as diagnostics, not hard gates | QA |
| Spectral rolloff 85% and 95% | combined | Robust upper-bandwidth/brightness measure | QA and preset hints |
| Spectral tilt | fitted envelope | Direct measure of broad damping/warmth; less pitch-sensitive than centroid | QA; future tilt target |
| Noisiness / residual dominance | combined | Directly matches ATS partial-plus-residual representation | QA; future tonal/noise target |
| Spectral flatness | residual bands or uniform envelope grid | Distinguishes diffuse/flat color from concentrated energy; complements residual dominance | QA |
| Spectral crest | uniform envelope grid | Detects a dominant resonance/peak | QA |
| Spectral contrast / peak-to-valley depth | fitted envelope | Measures resonance strength and hollow/peaky color | Hybrid QA |
| Envelope deviation / irregularity | partials relative to fitted envelope | Measures fine partial structure left after broad color is removed | Transfer QA |
| Spectral flux | consecutive normalized energy grids | Detects motion/onsets and transfer shimmer | Series QA |
| Spectral variation / cosine distance | consecutive normalized energy grids | Measures shape change while discounting global level change | Series QA |
| Descriptor confidence/coverage | every domain | Prevents sparse or residual-heavy frames from producing false certainty | UI/tests |

### 4.2 High-value missing descriptors added by this review

The original draft underweighted these:

#### Envelope deviation / spectral irregularity

For each usable partial, compare its dB amplitude to the fitted broad envelope:

```text
deviation_rms_db =
    sqrt(sum(w_i * (partial_db_i - envelope_db(f_i))^2) / sum(w_i))
```

This is directly aligned with the repo's de-color/re-color model. It tells us whether a transfer
preserved fine structure or flattened the source into the target envelope. It should ship before
tristimulus or higher-order residual statistics.

#### Rolloff

The frequency below which 85% or 95% of total energy lies is robust, cheap, and easier to
interpret than high-order moments. Compute it from a sorted combined-energy distribution.

#### Flatness and crest

Noisiness says how much energy is in the ATS residual path. Flatness says how distributed the
energy is within a regularly sampled or critical-band representation. They answer different
questions and should both be present.

Do not compute flatness directly over sparse partial amplitudes; missing frequencies would be
mistaken for observations.

#### Formant/resonance peak descriptors

The repo already has fitted envelopes and mainline formant-aligned morphing. Extract:

- peak position on Bark/Hz axis,
- prominence in dB,
- approximate bandwidth,
- inter-peak spacing,
- peak count above a configured prominence.

These are better integration points for formant-aware hybrid QA than SSC weighting.

#### Temporal shape

Attack identity is not adequately described by an attack-window centroid alone. Add:

- log attack time,
- temporal centroid,
- attack slope / attack energy ratio,
- decay slope,
- effective duration,
- attack high-frequency energy or positive spectral flux.

These are sequence descriptors and require valid frame times or a waveform energy envelope.

### 4.3 Conditional descriptors

These must carry a non-`Available` status unless their prerequisites are satisfied.

| Descriptor | Prerequisite | Use |
|------------|--------------|-----|
| Harmonicity / harmonic energy ratio | reliable F0 or harmonic labels | Distinguish harmonic, quasi-harmonic, and inharmonic sources |
| Inharmonicity | reliable F0 and harmonic assignment | Bell/piano/string-stretch diagnostics; possible future target |
| Odd/even harmonic energy ratio | reliable harmonic assignment | Instrument-color diagnostic |
| Tristimulus N1/N2/N3 | reliable harmonic assignment | Harmonic source identity and level-dependent QA |
| Harmonic spectral deviation | reliable harmonic assignment and envelope | Harmonic fine-structure diagnostic |
| F0-normalized descriptor residuals | reliable F0 plus a documented normalization model | Cross-register fixture comparison only |
| Partial AM/FM summaries | tracked partial histories or mainline DDM slope arrays | Motion and modulation QA |
| Dense-spectrum HFC | waveform spectrum | Attack/percussiveness validation |

F0 normalization must start with simple, transparent normalization such as centroid/F0 ratios or
log-frequency offsets. A learned per-instrument polynomial is not appropriate until the repo has
a labeled corpus and a tested training/export process.

### 4.4 Experimental perceptual descriptors

| Descriptor | Implementation note | Status |
|------------|---------------------|--------|
| Roughness / sensory dissonance | Pairwise partial interaction on a critical-band model; O(N^2), worker/offline only | Future analysis, possible future lane |
| Sharpness | Compute from Bark/critical-band energy with a documented psychoacoustic weighting; do not alias it to centroid | Future analysis |
| Spectral entropy | Normalized entropy of a regular energy grid; overlaps flatness but can be useful for texture | Optional |
| Spectral decrease | High-frequency decay statistic; useful for comparison but overlaps fitted-envelope tilt | Optional |
| Spectral complexity | Peak count above an energy-relative threshold; in ATS this is related to active partial count | Optional diagnostic |
| Semantic hardness/depth/warmth/boominess | Requires validated perceptual models and usually waveform features | Offline experiment only |
| MFCC/GFCC or learned embedding | Useful for retrieval/distance, not directly invertible as a render control | Offline experiment only |

### 4.5 Research-only items removed from the critical path

The following may remain in research notes, but must not block descriptor delivery:

- ICA fallback for correspondence,
- QPC/SOS/bispectrum correspondence priors,
- residual higher-order statistics as a release gate,
- learned timbral-distance beta without a corpus and training pipeline,
- cluster-weighted synthesis as an alternative engine.

These are not simple descriptors, do not map cleanly onto the current deterministic ATS render
contract, and would add substantial complexity before the basic descriptor layer is proven.

### 4.6 Additional classic-era descriptors supported by ATS data

A second review of SMS, MPEG-7, CUIDADO/Peeters, Beauchamp, and early Timbre Toolbox descriptor
families found the following additional descriptors from roughly the 1980s-2000s era. These are
computable from ATS partial tracks or residual critical-band trajectories and are distinct enough
from the MVP set to retain in the roadmap.

#### Partial and harmonic shape

| Descriptor | ATS input | Notes |
|------------|-----------|-------|
| Adjacent-partial spectral irregularity | Frequency-sorted partial amplitudes | Compute one documented definition at first; Jensen/Krimphoff-style variants may be comparison metrics |
| Harmonic spectral centroid and spread | Harmonic-assigned partials | Conditional MPEG-7 descriptors; distinct from general partial centroid because only assigned harmonics contribute |
| Harmonic spectral deviation | Harmonic-assigned amplitudes | Local jaggedness relative to neighboring harmonics or a smooth harmonic envelope |
| Harmonic spectral variation | Consecutive harmonic-amplitude vectors | Normalized frame-to-frame harmonic-shape change; useful for dynamic instrument identity |
| Odd and even harmonic centroids | Harmonic-assigned partials | Complements odd/even energy ratio by describing where each family is concentrated |
| Fundamental dominance / low-harmonic energy ratios | Harmonic-assigned partials | Includes fundamental-to-total and configurable low/mid/high harmonic energy ratios |
| Spectral strong-peak ratio | Partials or regular envelope grid | Dominance of the strongest peak relative to neighboring/remaining energy |
| Partial-density and birth/death rate | Active partial tracks | Existing active-partial counts become a texture/complexity trajectory when normalized by band and time |

Adjacent-partial irregularity must not be called harmonic irregularity unless reliable harmonic
labels exist. It captures local jaggedness that broad envelope deviation RMS can miss.

#### Partial-track temporal behavior

| Descriptor | ATS input | Notes |
|------------|-----------|-------|
| Partial onset asynchrony | Track birth times or threshold crossings | Spread of partial onsets around the event onset; useful for struck versus bowed attacks |
| Partial release asynchrony | Track death times or release threshold crossings | Describes coordinated versus diffuse decay |
| Track lifetime distribution | Track birth/death frames | Mean, spread, and energy-weighted lifetime; useful for stable tone versus granular/noisy texture |
| Per-partial temporal centroid | Partial amplitude trajectory | Describes when each component carries its energy |
| Partial-envelope modulation rate/depth | Partial amplitude trajectory or DDM AM slope series | Detects tremolo, beating, and component-level movement |
| Cross-partial envelope coherence | Normalized correlation of important partial amplitude trajectories | Distinguishes common-envelope motion from independent flutter |
| Frequency modulation rate/depth | Partial frequency trajectories or DDM FM slope series | Vibrato/glide descriptor; keep separate from timbral brightness |
| Attack spectral flux | Partial/residual energy grids in the detected attack window | More direct attack descriptor than whole-sound mean flux |

These are series descriptors. They require reliable frame times and track continuity; they should
not be synthesized from unrelated per-frame peak lists.

#### Residual critical-band color and motion

| Descriptor | ATS input | Notes |
|------------|-----------|-------|
| Bandwise noisiness | Partial energy and residual energy per critical band | Locates breath/bow/noise instead of reducing it to one global ratio |
| Residual centroid, spread, rolloff, slope/decrease, flatness, crest, and entropy | Residual critical-band energies | Apply the same distribution helpers as combined descriptors, with `ResidualBands` domain |
| Residual spectral flux and variation | Consecutive normalized residual-band vectors | Measures moving noise color and stochastic attack motion |
| Residual stationarity / self-similarity | Correlation or cosine similarity of residual-band vectors over time | Distinguishes steady hiss from evolving scrape/noise texture |
| Residual modulation spectrum | Per-band energy trajectories | Energy in slow tremolo, flutter, and faster roughness-rate ranges; use explicit frame-rate limits |
| Residual temporal centroid per band | Per-band energy trajectories | Describes where low-, mid-, and high-band noise occurs in the event |
| Residual color distance | Source/target residual-band vectors on a shared grid | Useful for Hybrid QA and preset similarity, not a render lane by itself |

Residual-band energy supports color and modulation-envelope descriptors, but it does **not**
retain the residual waveform distribution or phase. Therefore residual kurtosis, zero-crossing
rate, bispectrum/HOS, AR coefficients, and sample-level impulsiveness cannot be recovered from the
current `band_energy` representation. They require retained residual audio or additional analysis
metadata.

#### Conditional perceptual and basis descriptors

| Descriptor | Prerequisite | Notes |
|------------|--------------|-------|
| Harmonic-to-noise ratio | Reliable separation/calibration | The inverse view of noisiness; useful for compatibility with classic descriptor sets |
| Specific-loudness spread and sharpness | Calibrated levels plus a documented critical-band loudness model | ATS bands are a useful input, but raw band energy is not yet specific loudness |
| Spectral-basis/PCA projections | A fixed, serialized basis trained or fitted on a defined corpus/session | Classic MPEG-7 compact descriptor; useful for retrieval and distance, not directly invertible |
| Partial phase coherence / phase deviation | Retained reliable phases, usually harmonic assignment | Conditional attack diagnostic; lower priority than amplitude/frequency trajectories |

### 4.7 Recommended prioritization of the additional set

Add after the MVP, in this order:

1. adjacent-partial irregularity,
2. harmonic spectral variation plus odd/even centroids behind the reliable-F0 gate,
3. bandwise noisiness and residual spectral variation/stationarity,
4. partial onset/release asynchrony and lifetime statistics,
5. partial/residual modulation-rate summaries,
6. calibrated perceptual or spectral-basis descriptors only when their prerequisites exist.

### 4.8 Additional descriptors added by this amendment

A second pass against MPEG-7, Essentia, and voice-quality literature surfaced descriptors that
the previous review undercounted. They are computable from existing repo data with no model
extension.

#### Relative and ratio diagnostics (general)

| Descriptor | Domain | ATS input | Notes |
|------------|--------|-----------|-------|
| Partial-versus-residual centroid offset | partials and residual_bands | partial and residual energy distributions | Diagnostic for tonal/noise layering; "tonal in lows, noise in highs" or vice versa. Cheap and complements global noisiness |
| Dominant-partial energy fraction | partials | sorted partial energies | Fraction carried by the single loudest partial. Closely related to strong-peak ratio but partial-domain rather than grid-domain |
| Audio spectrum flatness vector | combined or residual_bands | bandwise non-negative energy | MPEG-7 style per-band flatness vector; bandwise noisiness says where noise sits, this says how diffuse each band's energy is |
| Centroid trajectory velocity / acceleration | combined | centroid trajectory plus frame hop | First/second time derivatives of centroid. Captures gestural brightness motion that flux/variation conflate with amplitude movement |
| Frame energy stability (coefficient of variation) | partials, residual_bands, or combined | per-frame total energy series | Series-level steadiness diagnostic; useful for QA of sustain stability |

These descriptors are all `general`. They must use the same energy-weighted convention as the
rest of the MVP set and must be invariant to uniform global gain when expressed as a normalized
ratio.

#### Voice-quality descriptors (conditional)

| Descriptor | Prerequisite | Use |
|------------|--------------|-----|
| F0 jitter (period perturbation) | reliable per-frame F0 plus track continuity | Cycle-to-cycle F0 instability; classic voice-quality measure |
| Amplitude shimmer | reliable fundamental/track amplitude trajectory | Cycle-to-cycle amplitude perturbation paired with jitter |
| Cepstral peak prominence (CPP) | `waveformSpectrum` present and sufficient bandwidth | Robust noise/breathy-quality measure; smoothed cepstral peak height relative to regression baseline |
| Harmonic-tracking phase coherence (voice) | reliable harmonic assignment plus retained phases | Lower priority than amplitude/frequency trajectories; cross-listed with section 4.6 phase coherence |

These descriptors must report `MissingReliableF0`, `MissingWaveform`, or `TooSparse` when their
prerequisites are not satisfied. They must never silently default to a numeric placeholder.

### 4.9 Repo-specific descriptor input contract

The descriptor core consumes inputs that already exist on this branch. The following input-side
rules clarify how repo data feeds into status, confidence, and validity reporting:

- **Partial confidence aggregation.** `EnvelopePartialPoint::confidence` is honored. A
  descriptor's reported confidence is the energy-weighted mean of contributing partial
  confidences, optionally combined with the residual-side confidence below. Confidence is
  serialized alongside the value and is independent of the descriptor status.
- **Residual confidence aggregation.** `EnvelopeResidualBandPoint::confidence` is honored the same
  way. The combined-domain descriptor confidence is the energy-weighted mean across partials and
  bands using the canonical energy model in section 3.2.
- **Orphan residual ratio.** `EnvelopeResidualBandPoint::orphanBand` defines
  `orphan_residual_ratio = sum(orphan_band_energy) / sum(all_band_energy)`. Descriptors that
  consume residual energy must serialize this ratio as part of noisiness coverage even when it is
  zero.
- **Selection masks.** `EnvelopePartialPoint::selected = false` excludes a partial from descriptor
  energy distributions. Selection masks must be applied before energy-weighting; they must not
  silently zero out confidence.
- **Sample-rate / Nyquist clamp.** Descriptor analysis must clamp grid frequencies to
  `min(sampleRate / 2, axis.maxFrequencyHz)` and refuse to assert energy above that. Series
  modulation-spectrum descriptors must also clamp to the series Nyquist defined by
  `1 / (2 * frameHopSeconds)`.
- **Waveform availability.** `EnvelopeFrameInput::waveformAvailable` and a non-empty
  `waveformSpectrum` are required for `WaveformSpectrum`-domain descriptors. CPP and dense HFC
  must return `MissingWaveform` otherwise.
- **Sourced metadata.** `sourceModelId` and `frameIndex` are propagated into every
  `TimbreDescriptorFrame`/`TimbreDescriptorSeries` so reports can be correlated against fixtures
  and recordings without an external join.

These rules are part of the descriptor contract and must be reflected in fixture coverage and the
report schema (sections 6.4 and 10).

---

## 5. Descriptor definitions

### 5.1 Centroid and moments

For energy samples `(f_i, e_i)`:

```text
centroid_hz = sum(f_i * e_i) / sum(e_i)
spread_hz = sqrt(sum(e_i * (f_i - centroid_hz)^2) / sum(e_i))
```

Skewness and kurtosis use the corresponding normalized central moments. Report them only when
there are enough non-zero samples and spread is above a numerical floor.

Also compute centroid and spread on the configured perceptual axis:

```text
z_i = warp(f_i)  // Bark, ERB, or Mel
centroid_axis = sum(z_i * e_i) / sum(e_i)
```

Use linear interpolation on the perceptual coordinate or geometric interpolation in Hz for
positive centroid targets. Linear interpolation in Hz is allowed for diagnostics; it is not the
preferred default for a perceptual brightness control.

### 5.2 Spectral tilt

Fit a robust line on a uniformly sampled fitted envelope:

```text
envelope_db(f) = intercept + tilt_db_per_octave * log2(f / reference_hz)
```

Use envelope samples rather than sparse raw partials where possible. Normalize or separately
record level so a global gain change does not change tilt.

### 5.3 Rolloff

Sort energy samples by frequency and find the first frequency whose cumulative energy reaches the
requested fraction:

```text
rolloff_p = min f_j where cumulative_energy(f_j) >= p * total_energy
```

Compute at `p=0.85` and `p=0.95`.

### 5.4 Flatness, crest, and entropy

Use a non-negative regularly sampled energy vector `E_k`:

```text
flatness = geometric_mean(E_k + floor) / arithmetic_mean(E_k + floor)
crest = max(E_k) / arithmetic_mean(E_k)
entropy = -sum(p_k * log(p_k)) / log(K), where p_k = E_k / sum(E_k)
```

The vector may be residual critical-band energy or a uniform sampled envelope grid. Serialize the
domain and floor.

### 5.5 Spectral contrast and formant/resonance peaks

Sample a reliable fitted envelope on a perceptual grid. Detect local peaks with minimum
prominence in dB and minimum separation on the same axis. For each peak, report:

```text
position_hz
position_axis
prominence_db
bandwidth_axis
```

The mainline `BarkSpectralEnvelope::findPeakBarks` is a useful starting point, but the generalized
`FrameEnvelope` path also needs an estimator-independent peak extractor.

### 5.6 Noisiness

```text
noisiness = residual_energy / (partial_energy + residual_energy)
```

Report:

- total partial energy,
- total residual energy,
- noisiness,
- orphan residual ratio when available,
- validity when no residual analysis exists.

Do not infer a zero residual from missing residual data.

### 5.7 Flux and temporal shape

Map consecutive frames to the same normalized energy grid before comparison:

```text
positive_flux_t = sqrt(sum(max(0, E_t[k] - E_(t-1)[k])^2))
signed_flux_t = sqrt(sum((E_t[k] - E_(t-1)[k])^2))
spectral_variation_t =
    1 - dot(E_t, E_(t-1)) / (norm(E_t) * norm(E_(t-1)))
```

Normalize by frame energy or loudness when comparing different source levels. Spectral variation
is preferable to raw flux when the question is whether spectral **shape**, rather than level,
changed.

Sequence-level temporal descriptors use a documented energy envelope and threshold policy:

- `log_attack_time = log10(t_attack_end - t_attack_start)`
- temporal centroid = energy-weighted time centroid
- attack and decay slopes in dB/s
- effective duration above threshold

### 5.8 Harmonic descriptors

Only compute after F0/harmonicity gating:

```text
tristimulus_1 = E(h1) / E_harmonic
tristimulus_2 = E(h2..h4) / E_harmonic
tristimulus_3 = E(h5+) / E_harmonic
odd_even_ratio = E_odd / max(E_even, floor)
inharmonicity = weighted deviation of partial frequencies from assigned harmonic frequencies
```

Store harmonic assignment confidence and the fraction of partial energy explained by assigned
harmonics.

### 5.9 Adjacent-partial irregularity

For frequency-sorted usable partial amplitudes, use a named and versioned definition. A simple
local-deviation form is:

```text
local_mean_i = (a_(i-1) + a_i + a_(i+1)) / 3
irregularity = sum(abs(a_i - local_mean_i)) / sum(a_i)
```

Compute in linear amplitude unless the selected definition explicitly says otherwise. Do not
silently change formulas between releases; classic irregularity definitions are not numerically
equivalent.

### 5.10 Harmonic spectral variation

Align harmonic-amplitude vectors by harmonic number, floor or omit unavailable harmonics according
to a serialized policy, then compute normalized shape change:

```text
harmonic_variation_t =
    1 - dot(H_t, H_(t-1)) / (norm(H_t) * norm(H_(t-1)))
```

This is conditional on reliable harmonic assignments in both frames.

### 5.11 Partial onset/release asynchrony

For an event-level reference onset `t_0`, derive per-track threshold-crossing times and summarize
the energy-weighted offsets:

```text
onset_offset_i = t_onset_i - t_0
onset_asynchrony = weighted_stddev(onset_offset_i)
```

Use a corresponding event release reference for release asynchrony. Serialize the amplitude
threshold, minimum track energy, and event-boundary policy.

### 5.12 Residual modulation and stationarity

For each residual band, analyze its non-negative energy trajectory after converting it to a
documented amplitude, power, or dB domain. Report:

- normalized frame-to-frame spectral variation,
- lagged self-similarity/stationarity,
- modulation energy in frame-rate-supported frequency ranges,
- temporal centroid and effective duration per band or grouped band range.

Do not report a modulation band whose upper frequency is above the descriptor-series Nyquist
rate.

---

## 6. Core data model and APIs

### 6.1 New files

```text
src/core/TimbreDescriptors.h
src/core/TimbreDescriptors.cpp
tests/timbre_descriptors_test.cpp
```

Keep this module independent of JUCE and `ATS_SOUND`. It consumes normalized frame inputs already
used by the envelope stack.

### 6.2 Proposed structures

```cpp
namespace ats
{

enum class TimbreDescriptorDomain
{
    Partials,
    ResidualBands,
    Combined,
    FittedEnvelope,
    WaveformSpectrum,
    TemporalSeries
};

enum class TimbreDescriptorStatus
{
    Available,
    EmptyInput,
    TooSparse,
    MissingResidual,
    MissingEnvelope,
    MissingReliableF0,
    MissingWaveform,
    Undefined,
    Unsupported
};

struct TimbreDescriptorValue
{
    double value = 0.0;
    TimbreDescriptorDomain domain = TimbreDescriptorDomain::Combined;
    TimbreDescriptorStatus status = TimbreDescriptorStatus::EmptyInput;
    double confidence = 0.0;
};

struct TimbreResonancePeak
{
    double frequencyHz = 0.0;
    double axisPosition = 0.0;
    double prominenceDb = 0.0;
    double bandwidthAxis = 0.0;
};

struct TimbreDescriptorFrame
{
    static constexpr std::uint32_t kVersion = 1;

    std::string sourceModelId;
    std::uint64_t frameIndex = 0;

    TimbreDescriptorValue partialEnergy;
    TimbreDescriptorValue residualEnergy;
    TimbreDescriptorValue noisiness;

    TimbreDescriptorValue partialCentroidHz;
    TimbreDescriptorValue combinedCentroidHz;
    TimbreDescriptorValue combinedCentroidAxis;
    TimbreDescriptorValue combinedSpreadAxis;
    TimbreDescriptorValue combinedSkewness;
    TimbreDescriptorValue combinedKurtosis;
    TimbreDescriptorValue rolloff85Hz;
    TimbreDescriptorValue rolloff95Hz;

    TimbreDescriptorValue tiltDbPerOctave;
    TimbreDescriptorValue flatness;
    TimbreDescriptorValue crest;
    TimbreDescriptorValue spectralContrastDb;
    TimbreDescriptorValue envelopeDeviationRmsDb;

    TimbreDescriptorValue harmonicity;
    TimbreDescriptorValue inharmonicity;
    TimbreDescriptorValue oddEvenRatio;
    std::array<TimbreDescriptorValue, 3> tristimulus;

    std::vector<TimbreDescriptorValue> subbandCentroidsHz;
    std::vector<TimbreResonancePeak> resonancePeaks;
};

struct TimbreDescriptorOptions
{
    EnvelopeAxisSpec comparisonAxis;
    int comparisonGridSize = 64;
    double amplitudeFloor = 1.0e-12;
    double minimumPeakProminenceDb = 3.0;
    double rolloffLowFraction = 0.85;
    double rolloffHighFraction = 0.95;
    bool includeResidualInCombined = true;
    bool enableHarmonicDescriptors = false;
};

struct TimbreDescriptorSeriesOptions
{
    double frameHopSeconds = 0.0;
    double attackThresholdDb = -20.0;
    double effectiveDurationThresholdDb = -40.0;
    bool resetFluxAtTransientBoundaries = true;
    std::vector<bool> transientBoundaries;
};

struct TimbreDescriptorSeries
{
    static constexpr std::uint32_t kVersion = 1;

    std::string sourceModelId;
    std::vector<TimbreDescriptorFrame> frames;
    std::vector<TimbreDescriptorValue> positiveFlux;
    std::vector<TimbreDescriptorValue> spectralVariation;
    TimbreDescriptorValue temporalCentroidSeconds;
    TimbreDescriptorValue logAttackTime;
    TimbreDescriptorValue attackSlopeDbPerSecond;
    TimbreDescriptorValue decaySlopeDbPerSecond;
    TimbreDescriptorValue effectiveDurationSeconds;
};

} // namespace ats
```

The exact field layout may be adjusted during implementation, but the domain, status, confidence,
and version contracts are required.

### 6.3 Core API

```cpp
TimbreDescriptorFrame analyzeTimbreDescriptorFrame(
    const EnvelopeFrameInput& frame,
    const TimbreDescriptorOptions& options,
    const FrameEnvelope* fittedEnvelope = nullptr);

TimbreDescriptorSeries analyzeTimbreDescriptorSeries(
    const std::vector<EnvelopeFrameInput>& frames,
    const std::vector<FrameEnvelope>* fittedEnvelopes,
    const TimbreDescriptorOptions& options,
    const TimbreDescriptorSeriesOptions& seriesOptions);
```

Important implementation boundary:

- `TimbreDescriptors` computes descriptors.
- Envelope estimators continue to fit `FrameEnvelope`.
- Hybrid functions continue to transform partials/residuals.
- Plugin/mainline morph adapters extract frames from `ATS_SOUND` and schedule worker work.

Do not put descriptor calculation inside `RegularizedCepstrumEnvelopeEstimator::estimate`; that
would couple metrics to one estimator and duplicate work when other estimators are selected.

### 6.4 Serialization

Descriptor fixture/export JSON must include:

```json
{
  "schema_version": 1,
  "source_model_id": "example",
  "weighting": "energy",
  "comparison_axis": "bark",
  "comparison_grid_size": 64,
  "frames": [
    {
      "frame_index": 0,
      "combined_centroid_hz": {
        "value": 2412.5,
        "domain": "combined",
        "status": "available",
        "confidence": 0.93
      },
      "noisiness": {
        "value": 0.18,
        "domain": "combined",
        "status": "available",
        "confidence": 1.0
      }
    }
  ]
}
```

Recorded fixture baselines are regression references, not universal truth. Synthetic fixtures
with analytical inputs are the correctness tests.

### 6.5 Descriptor-control and Timbre Motion model

The descriptor-control layer belongs in core recipe/render code after branch reconciliation. It
must use stable ids and vectors rather than extending `MorphLane::Count`.

```cpp
enum class TimbreControlRole
{
    AnalysisOnly,
    DirectTarget,
    CompositeMacro,
    ConditionalTarget,
    ExperimentalTarget
};

enum class TimbreTargetMode
{
    FollowNatural,
    InterpolateAB,
    OffsetNatural,
    AbsoluteTarget
};

enum class TimbreTargetStatus
{
    Disabled,
    Available,
    MissingPrerequisite,
    Unreachable,
    Conflict,
    Clamped,
    Error
};

struct TimbreMotionPoint
{
    double timeNorm = 0.0;
    double valueNorm = 0.0;
    MorphCurveShape curve = MorphCurveShape::Linear;
};

struct TimbreTargetCurve
{
    std::string descriptorId;
    TimbreTargetMode mode = TimbreTargetMode::FollowNatural;
    bool enabled = false;
    bool endpointLocked = true;
    int priority = 0;
    double depth = 1.0;
    double smoothingMs = 40.0;
    std::vector<TimbreMotionPoint> points;
};

struct TimbreMacroRoute
{
    std::string targetDescriptorId;
    double depth = 0.0;
    bool bipolar = true;
};

struct TimbreMotionMacro
{
    std::string macroId;
    bool enabled = false;
    double amount = 0.0;
    std::vector<TimbreMacroRoute> routes;
};

struct TimbreTargetResult
{
    std::string descriptorId;
    TimbreDescriptorValue natural;
    TimbreDescriptorValue requested;
    TimbreDescriptorValue achieved;
    TimbreTargetStatus status = TimbreTargetStatus::Disabled;
    double normalizedError = 0.0;
};

struct TimbreMotionPlan
{
    static constexpr std::uint32_t kVersion = 1;

    std::vector<TimbreTargetCurve> targetCurves;
    std::vector<TimbreMotionMacro> macros;
    std::vector<std::string> pinnedDescriptorIds;
    std::string focusedDescriptorId;
};
```

The exact UI-only focus/pin state may remain under deterministic `ui.*` recipe metadata, but
anything that changes rendered audio must be typed, versioned recipe state. Unknown descriptor ids
and future fields must round-trip without affecting old renders.

### 6.6 Descriptor control registry

Each descriptor exposed to Timbre Motion must have one registry record containing:

- stable descriptor and display ids,
- family, units, display range, normalization, and interpolation domain,
- control role and prerequisite policy,
- approved inverse-transform id, or no transform for analysis-only descriptors,
- safe physical bounds, smoothing policy, and default priority,
- hard-invariant and conflict groups,
- whether it may be pinned, routed from a macro, or exposed to host automation,
- computational cost and whether target solving is available in snapshot, timeline, or both.

The registry is the source of truth for UI availability and render capability. The UI must never
infer controllability merely because a descriptor value exists.

---

## 7. Repository integration

### 7.1 This worktree: generalized envelope and hybrid stack

| Concern | Existing integration point | Descriptor work |
|---------|----------------------------|-----------------|
| Frame inputs | `src/core/SpectralEnvelopeEstimator.h` (`EnvelopeFrameInput`) | Reuse directly |
| Envelope values and axes | `src/core/SpectralEnvelope.h` (`FrameEnvelope`, `EnvelopeAxisSpec`) | Reuse directly |
| Residual/noise | `EnvelopeResidualBandPoint`, `SpectralEnvelopeHybrid.*` | Compute residual and combined descriptors |
| Envelope transfer | `applyEnvelopeTransferDirect`, `applyEnvelopeTransferMapped`, `applyResidualEnvelopeTransfer` | Compare source/output descriptors in tests |
| UI overlay | `EnvelopeOverlayData`, `EnvelopeOverlayPanel` | Add compact descriptor readout after core tests |
| Frame extraction | `PluginProcessor::buildEnvelopeOverlayData` currently has a local `collectFrame` lambda | Reuse/factor when series analysis is wired |
| Fixtures | `spectral_envelope_fixture_metrics_test.cpp`, `SPECTRAL_ENVELOPE_FIXTURES.md` | Extend rather than create a competing fixture system |

This branch's envelope transfer controls are represented in APVTS and overlays, but descriptor
delivery must not claim a completed two-source audible morph renderer here.

### 7.2 Mainline morph integration after branch reconciliation

The mainline morph stack already contains:

- `src/core/AtsMorphRecipe.*`
- `src/core/AtsMorphTransport.*`
- `src/core/AtsMorphTransportCorrespondence.*`
- `src/core/AtsSpectralEnvelope.*`
- `renderMorphTrajectoryBuffer` in `src/plugin/PluginProcessor.cpp`
- `morph_metric_harness.py` and morph fixture/tolerance JSON

After the generalized envelope stack is reconciled with mainline:

1. Add descriptor analysis during render preparation or fixture generation, not in the audio
   callback.
2. Extend the existing morph metric report instead of creating a second unrelated morph report.
3. Reuse the existing optimal-transport correspondence baseline.
4. Use descriptor differences as diagnostics first. Do not add a descriptor cost to transport
   until it proves lower churn or better listening results without breaking energy conservation.
5. Add new recipe lanes only with schema migration and old-session output-preserving defaults.

### 7.3 Envelope implementation reconciliation

Mainline `AtsSpectralEnvelope.*` and this branch's generalized `SpectralEnvelope*` stack overlap.
Before audible descriptor controls ship:

- preserve mainline `BarkSpectralEnvelope`/`FormantAlignedEnvelopeMorph` behavior and tests,
- port or merge the generalized `FrameEnvelope` estimator/recipe APIs,
- avoid two independently evolving definitions of Bark conversion, envelope floors, peak
  prominence, and transfer math,
- add adapters where a full replacement would change existing morph output.

Descriptor analysis should be the first shared consumer that proves both envelope paths agree on
common fixture measurements.

### 7.4 Repo-fit Timbre Motion ownership

Mainline already has:

- six fixed `MorphLane` settings in `AtsMorphRecipe.*`,
- a `MorphTimelineEditor` using normalized breakpoint points and `MorphCurveShape`,
- undo-coalesced timeline edits,
- prepared timeline preview/bounce rendering,
- APVTS-backed stable performance controls and recipe `ui.*` metadata.

Timbre Motion must extend these patterns rather than create a competing timeline:

- reuse `MorphCurveShape`, normalized time, timeline preview/bounce, undo, and stale/rebuilding
  states,
- add typed `TimbreMotionPlan` recipe state for audio-affecting curves and routes,
- keep fixed synthesis lanes and descriptor target curves visually and structurally distinct,
- migrate existing opaque timeline metadata only where needed; do not break old timeline recall,
- expose only stable macros through APVTS; store arbitrary descriptor curves/routes in the recipe,
- evaluate and solve target curves during prepared snapshot/timeline work, never in
  `processBlock`.

---

## 8. Render-control design

Descriptor motion is a target-and-feedback layer over the existing morph renderer. It does not
replace source A/B morphing, fixed synthesis lanes, Hybrid, or EnvelopeMorph.

Only three inverse transforms are recommended for the first audible release: brightness, tilt,
and tonal/noise balance. The UI and recipe architecture should support additional controls without
claiming that every analysis descriptor can be inverted.

### 8.1 Brightness target

Target the **combined perceptual-axis centroid**, but do not scale partial frequencies.

Recommended transform:

1. Compute the current combined centroid.
2. Interpolate the target centroid between A and B on Bark/ERB or geometrically in Hz.
3. Apply a bounded low-order gain curve/tilt around a pivot frequency.
4. Use deterministic bisection or another bounded solver to find the gain slope that reaches the
   target centroid within tolerance.
5. Renormalize energy according to the selected normalization policy.

This preserves pitch and is safer than frequency scaling. Full target-envelope transfer remains a
separate Hybrid operation.

### 8.2 Tilt target

Apply a bounded dB-per-octave gain curve to partial amplitudes and residual-band amplitudes, then
apply partial/residual normalization independently. Tilt should be measured and targeted on a
fitted or uniform envelope grid.

### 8.3 Tonal/noise balance

Interpolate a noisiness target and redistribute gain between partial and residual paths while
preserving configured total power:

```text
target_partial_energy = total_energy * (1 - target_noisiness)
target_residual_energy = total_energy * target_noisiness
```

If residual data is unavailable, report that the target cannot be reached. Do not synthesize a
new residual model silently.

Do not silently reinterpret the mainline `MorphLane::ResidualNoise` A/B interpolation lane as a
noisiness-target lane. Either add explicit target semantics with recipe migration or add a
separate tonal/noise-balance control.

### 8.4 Control capability classes

| Class | Initial examples | Timeline behavior |
|-------|------------------|-------------------|
| Direct bounded target | brightness, tilt, tonal/noise balance | Editable requested curve plus achieved feedback |
| Composite macro | opening/closing, warmth, texture | Routes one gesture to multiple approved targets |
| Conditional target | resonance/formant, harmonicity, inharmonicity, attack character | Enabled only when prerequisites and transform are valid |
| Analysis overlay | flux, deviation, churn, confidence, birth/death rate | Inspectable curve; never presented as directly editable |
| Experimental target | focus/spread, roughness, transient hardness | Hidden or opt-in until numerical and listening gates pass |

### 8.5 Descriptor target modes

For descriptor value `d`, define source endpoints `d_A`, `d_B`, the unmodified render result
`d_natural(t)`, and a saved normalized curve `c(t)`.

| Mode | Requested behavior | Default use |
|------|--------------------|-------------|
| `FollowNatural` | No target transform; show `d_natural(t)` | Output-preserving default |
| `InterpolateAB` | Interpolate from `d_A` to `d_B` using `c(t)` in the descriptor's registered domain | Let one timbral property reach B earlier/later than the main morph |
| `OffsetNatural` | Apply a bounded bipolar offset around `d_natural(t)` | Safest expressive edit |
| `AbsoluteTarget` | Map `c(t)` into a registered physical range | Expert mode; may be unreachable |

Endpoints are locked to source A/B by default. The first release must not expose endpoint unlock.
Interpolation domains are descriptor-specific: geometric/perceptual for frequency-like values,
linear for ratios, and dB-linear for tilt or gain-like values.

### 8.6 Target resolver and conflict policy

The first release must use a bounded deterministic resolver, not a general unbounded optimizer:

1. Render the existing morph/Hybrid/EnvelopeMorph base frame.
2. Measure the natural descriptor values.
3. Evaluate enabled target curves and macro routes at normalized time.
4. Apply hard invariants first: endpoint locks, finite values, pitch protection, configured total
   energy, residual availability, and gain/frequency bounds.
5. Resolve direct targets in a versioned order. Start with tonal/noise balance, then tilt, then
   brightness, because earlier operations affect combined brightness.
6. Allow a small fixed number of bounded correction passes during render preparation.
7. Re-analyze the result and emit natural, requested, achieved, error, clamp, prerequisite, and
   conflict status for every active target.

Priorities break conflicts between soft targets; they may never override hard invariants. Disabled
or unreachable targets leave the last valid/base render intact. The renderer must not silently
degrade one target to satisfy another or report requested values as achieved values.

### 8.7 Timbre Motion panel

The primary UX is a dedicated **Timbre Motion** panel inside the existing morph workspace:

```text
[Timbre Macro Rack: Brightness | Warmth | Tone/Noise | Focus | Resonance | Texture]
[Family filter] [Focused descriptor] [mode] [enable] [pin] [reset]

Natural trajectory   ----- ghost/reference
Requested trajectory ===== editable target
Achieved trajectory  ..... measured render result

[Shared motion curves] [route depth/polarity/range/smoothing] [target status]
```

The panel has three progressive-disclosure layers:

1. **Macro rack:** a compact set of musical cards, not every descriptor.
2. **Focused editor:** one large reusable timeline editor for the selected macro, target, or
   analysis overlay.
3. **Pinned lanes:** up to four compact read-only/editable lanes for comparison while the focused
   editor remains the sole detailed editor.

Each macro/target card shows enable/bypass, source A/B values, current/requested/achieved value,
units, a mini trajectory, availability/confidence, modulation depth, and pin/focus actions.
Families include spectral color, energy structure, spectral shape, resonance, texture, temporal,
and diagnostics.

### 8.8 Macro rack and shared motion curves

Initial macro cards:

- **Brightness:** primary target combined perceptual centroid; monitor rolloff and tilt.
- **Warmth/Tilt:** primary target fitted-envelope tilt; monitor centroid and low/high balance.
- **Tonal/Noise:** primary target noisiness; monitor residual color and bandwise noisiness.
- **Focus/Spread:** analysis/experimental until its inverse transform passes listening gates.
- **Resonance/Formant:** analyze and route to existing EnvelopeMorph behavior before adding a new
  transform.
- **Texture/Roughness:** analysis/experimental and conditional.
- **Attack Character:** event-level/conditional; not a normal per-frame target initially.
- **Harmonicity:** conditional/experimental.

Users may create reusable shared motion curves such as "open", "pulse", or "late bloom" and route
one curve to multiple approved targets. Every route has depth, polarity, normalized/physical
range, smoothing, enabled state, and target status. Cyclic routing and macro-to-analysis-only
routing are rejected.

Macro definitions and factory mappings are versioned. User macro routes live in the recipe.
Changing a macro mapping must not silently change old saved recipes.

### 8.9 Focused editor and pinned lanes

- Selecting a card or overlay loads it into the focused editor.
- The editor reuses normalized timeline time and `MorphCurveShape`.
- The vertical scale supports normalized view and physical-unit view.
- Editing affects the requested curve only; natural and achieved curves remain non-editable.
- Analysis-only descriptors may be focused or pinned but do not show editable target handles.
- A maximum of four compact pinned lanes is the initial UX limit; this is a presentation limit,
  not a recipe limit.
- Family filtering, favorites, search, and "show available only" prevent descriptor overload.
- Reset returns a target to `FollowNatural`, preserving the base morph.

### 8.10 Natural, requested, and achieved feedback

Every active target must expose:

- **Natural:** descriptor trajectory produced by the existing renderer with no descriptor target.
- **Requested:** the target trajectory after mode, curve, macro routes, bounds, and endpoint locks.
- **Achieved:** descriptor trajectory measured from the prepared render.
- **Error/status:** normalized target error plus available, clamped, missing-prerequisite,
  unreachable, or conflict state.

Requested/achieved feedback is required because descriptors are coupled and inverse transforms are
imperfect. Confidence and target error should be visually encoded without relying on color alone.
The UI must never imply that an unavailable or low-confidence descriptor was reached.

### 8.11 Persistence, automation, and evaluation

- Audio-affecting target curves, macro routes, priorities, modes, bounds, and versions are typed
  recipe state.
- Focus, family filter, favorites, and pinned presentation state may use deterministic `ui.*`
  metadata.
- Arbitrary descriptor ids must not become APVTS parameters. This avoids unstable host parameter
  layouts and automation overload.
- The first stable host-automatable macro set is brightness, warmth/tilt, and tonal/noise depth,
  plus the existing main morph/performance controls.
- Host macro automation modulates saved curves/routes; it does not rewrite breakpoint data.
- Timeline edits use existing undo coalescing and invalidate prepared descriptor-target caches.
- Preview, bounce, prepared snapshots, and trajectory rendering evaluate the same motion plan.

### 8.12 UI states and accessibility

Cards, routes, and focused curves must visibly distinguish:

- disabled/follow-natural,
- available and within tolerance,
- conditional but currently unavailable,
- stale or rebuilding,
- clamped/unreachable,
- conflicting with a higher-priority target,
- experimental.

All controls require accessible names, keyboard focus/editing, textual units/status, reset
actions, and tooltips that state what the control changes physically. Color is supplementary.

### 8.13 Controls deferred

- Formant/resonance motion: use the existing EnvelopeMorph path and descriptor-based QA first.
- Spread: changing spread without moving pitch generally requires frequency remapping or selective
  gains and needs listening validation.
- Roughness: requires partial detuning/AM behavior and is a separate synthesis feature.
- Harmonicity/inharmonicity: requires creating, deleting, or moving partials and must remain
  experimental.
- Attack hardness: belongs with the explicit transient layer and temporal policies.

---

## 9. Sequential implementation plan

### Phase 0 - Shared descriptor contract

**Goal:** Add deterministic analysis-only descriptor APIs with no audible change.

| Step | Deliverable | Files |
|------|-------------|-------|
| 0.1 | Add `TimbreDescriptors.h/.cpp` and status/domain/options structures | `src/core/` |
| 0.2 | Add energy distribution helpers for partials, residual bands, combined grids, and uniform envelope samples | `src/core/TimbreDescriptors.cpp` |
| 0.3 | Add analytical unit fixtures | `tests/timbre_descriptors_test.cpp` |
| 0.4 | Register source files and test target | `src/plugin/CMakeLists.txt`, `tests/CMakeLists.txt` |

**Acceptance:**

- no plugin output change,
- no allocations or descriptor work in `processBlock`,
- exact/near-exact analytical checks for centroid, spread, rolloff, flatness, crest, tilt, and
  noisiness,
- invalid/missing inputs return explicit status and finite values.

### Phase 1 - Envelope-relative and temporal metrics

**Goal:** Add descriptors that exploit the existing envelope and fixture infrastructure.

| Step | Deliverable |
|------|-------------|
| 1.1 | Envelope deviation RMS, spectral contrast, and resonance peak extraction |
| 1.2 | Series analysis: flux, spectral variation, centroid trajectory, noisiness trajectory, temporal centroid, attack/decay metrics |
| 1.3 | Extend `spectral_envelope_fixture_metrics_test.cpp` |
| 1.4 | Extend `docs/SPECTRAL_ENVELOPE_FIXTURES.md` with descriptor expectations |

**Acceptance:**

- de-color/re-color fixtures show lower envelope-deviation error than target-only transfer where
  expected,
- transient-preserve policy retains attack metrics better than an intentionally smoothed
  baseline,
- sequence metrics reset or gate at transient boundaries as documented.

### Phase 2 - Analysis UI and export

**Goal:** Make descriptors inspectable before adding audio-affecting Timbre Motion controls.

| Step | Deliverable |
|------|-------------|
| 2.1 | Add worker-built current-frame/series descriptor cache keyed by model revision and options |
| 2.2 | Add a compact readout near the envelope overlay: centroid, tilt, noisiness, rolloff, confidence |
| 2.3 | Optional centroid/noisiness trajectory overlay |
| 2.4 | Add deterministic descriptor JSON export for fixtures/research |

**Acceptance:**

- no UI-thread envelope fitting or series analysis,
- sparse/unavailable descriptors are visibly marked,
- cache invalidates on model revision, estimator settings, axis, or descriptor options.

### Phase 3 - Conditional harmonic descriptors

**Goal:** Add harmonic measurements without leaking harmonic assumptions into general material.

| Step | Deliverable |
|------|-------------|
| 3.1 | Reliable-F0 gate and harmonic assignment confidence |
| 3.2 | Harmonicity, inharmonicity, odd/even ratio, and tristimulus |
| 3.3 | Harmonic and inharmonic synthetic fixtures |

**Acceptance:**

- inharmonic/polyphonic or low-confidence frames report unavailable,
- harmonic fixtures recover expected ratios within tolerance,
- no default hybrid/morph path requires these values.

### Phase 4 - Mainline morph metric integration

**Prerequisite:** Reconcile this branch's generalized envelope stack with the mainline morph
stack.

| Step | Deliverable |
|------|-------------|
| 4.1 | Extend existing morph metric schema/report with descriptor trajectories and endpoints |
| 4.2 | Add descriptor analysis to prepared/offline morph renders |
| 4.3 | Compare baseline, Hybrid, and EnvelopeMorph using centroid, tilt, noisiness, deviation, formant, flux, and endpoint metrics |
| 4.4 | Evaluate lifetime track centroid and DDM AM/FM summaries as correspondence diagnostics |

**Acceptance:**

- existing endpoint, transport-cost, churn, and per-band power gates remain pinned,
- descriptors at amount 0/1 match source A/B within tolerance,
- no descriptor is added to correspondence cost without a measured benefit.

### Phase 5 - Timbre Motion foundation and first descriptor targets

**Goal:** Add scalable descriptor-motion UX and ship bounded brightness, tilt, and tonal/noise
targets.

| Step | Deliverable |
|------|-------------|
| 5.1 | Add descriptor control registry and typed `TimbreMotionPlan` recipe state |
| 5.2 | Add target modes, curve evaluation, macro routing, bounds, priorities, and status reports |
| 5.3 | Extend the existing Morph Timeline with the macro rack, focused editor, pinned lanes, and analysis overlays |
| 5.4 | Add shared motion curves and route matrix with depth/polarity/range/smoothing |
| 5.5 | Add natural/requested/achieved analysis and conflict/unreachable feedback |
| 5.6 | Brightness target through bounded gain-curve solve, not frequency scaling |
| 5.7 | Tilt target with independent partial/residual normalization |
| 5.8 | Tonal/noise balance target using existing residual path |
| 5.9 | Wire motion plans into main trajectory, preview/bounce, and prepared snapshot paths consistently |

**Acceptance:**

- old recipes render unchanged by default,
- fixed engine lanes remain separate from dynamic descriptor target curves,
- arbitrary descriptor curves do not expand the APVTS/host parameter surface,
- the same saved motion plan evaluates identically in preview, bounce, and snapshot paths,
- natural/requested/achieved curves and target status are available for every active target,
- conflicting, clamped, or unavailable targets are visible and never silently reported as met,
- brightness and tilt controls do not move partial frequencies,
- tonal/noise control preserves configured total power,
- main trajectory and snapshot paths agree within documented tolerance,
- manual listening validation accompanies numerical tests.

### Phase 6 - Experimental perceptual controls

Prototype only after the first bundle is stable:

- roughness/sensory dissonance,
- sharpness,
- inharmonicity transforms,
- attack hardness tied to the transient layer,
- semantic descriptor models for preset search.

---

## 10. Testing strategy

### 10.1 Unit fixtures

Add deterministic synthetic cases:

| Fixture | Expected checks |
|---------|-----------------|
| Single partial | centroid equals frequency; spread zero; moments unavailable where undefined |
| Two equal-energy partials | known centroid/spread/rolloff |
| Flat critical-band energy | flatness near 1, crest near 1, high entropy |
| Single dominant band | low flatness, high crest, low entropy |
| Partial plus residual split | exact noisiness and combined centroid |
| Known dB/octave envelope | recovered tilt |
| Envelope plus known fine deviations | recovered deviation RMS |
| Moving spectral bump | flux and centroid trajectory |
| Harmonic and stretched-harmonic frames | conditional harmonicity/inharmonicity/tristimulus |
| Coupled brightness/tilt/noise targets | deterministic priority, bounds, target error, and conflict status |
| Missing residual/F0/envelope target | unavailable target leaves the base render intact |

### 10.2 Existing test integration

This worktree:

- add `timbre_descriptors_test`,
- extend `spectral_envelope_fixture_metrics_test`,
- extend `hybrid_transfer_test` and `hybrid_residual_transient_test` only where descriptor
  invariants clarify existing behavior.

After mainline reconciliation:

- extend `morph_metric_harness.py` and its existing tolerance/report JSON,
- extend `morph_transport_correspondence_test` for diagnostics only,
- extend `spectral_envelope_transfer_test`,
- add a focused descriptor-controlled morph test rather than overloading recipe serialization
  tests.
- add target-mode, curve-evaluation, macro-route, priority/conflict, pin/focus-state, and
  natural/requested/achieved report tests.

### 10.3 Required invariants

- All serialized numbers are finite.
- Empty/sparse/missing-domain inputs return explicit statuses.
- Descriptor calculation is order-independent after frequency sorting.
- Uniform global gain does not change normalized shape descriptors.
- Noisiness changes only when partial/residual energy balance changes.
- Brightness/tilt targets preserve partial frequencies.
- Follow-natural and disabled Timbre Motion plans are output preserving.
- Target curves are endpoint locked by default.
- Unreachable/conflicting targets preserve a valid render and report explicit status.
- Preview, bounce, prepared snapshot, and timeline motion-plan evaluation agree.
- Morph endpoints and existing energy/transport gates do not regress.
- Synthetic analytical expectations are correctness tests; recorded baselines are regression
  tests.

### 10.4 Performance budgets

| Work | Budget |
|------|--------|
| Basic frame descriptors | O(partials + residual bands + grid size) |
| Formant/contrast extraction | O(grid size) after envelope sampling |
| Series flux | O(frames * grid size), worker/offline |
| Roughness | O(partials^2), worker/offline and opt-in |
| Descriptor-controlled render | O(active targets * (partials + residual bands)) per prepared frame/chunk with small fixed correction-pass count; no fitting or unbounded solve in audio callback |

---

## 11. Decisions still requiring listening validation

1. Whether Bark or ERB centroid gives the most useful cross-source brightness target.
2. Whether a bounded tilt-only brightness solver is sufficient or a low-order envelope gain curve
   is audibly better.
3. Whether descriptor targets should be applied before or after optimal-transport energy
   correspondence. Start after correspondence to preserve the existing plan; test feature-first
   ordering separately.
4. Whether spread can become a useful gain-only "focus" control without moving partial
   frequencies.
5. Whether residual flatness/entropy adds enough value beyond noisiness and residual color.
6. Whether the default focused editor should open in normalized or physical-unit view.
7. Whether four pinned descriptor lanes is the right initial presentation limit.
8. Which composite macro mappings are musically useful enough to ship as versioned factory
   mappings.
9. Whether a future joint optimizer materially improves coupled target accuracy over the bounded
   ordered resolver without harming predictability.

---

## 12. References

Repository references:

- `docs/SPECTRAL_ENVELOPE_PRD.md`
- `docs/SPECTRAL_ENVELOPE_EVALUATION.md`
- `docs/SPECTRAL_ENVELOPE_FIXTURES.md`
- `docs/SPECTRAL_ENVELOPE_PERFORMANCE.md`
- mainline `docs/MORPH_TO_ATS_ANALYSIS_PRD.md`
- mainline `docs/DSP_FIDELITY_PRD.md`

External references:

- Ableton Live Manual, Automation and Editing Automation Envelopes. Useful precedent for
  separating automation from modulation and revealing additional lanes on demand:
  https://www.ableton.com/en/live-manual/12/automation-and-editing-envelopes/
- Ableton Live Manual, Instrument/Drum/Effect Racks. Macro controls and Macro Variations are useful
  precedents for curated one-to-many control and recall:
  https://www.ableton.com/en/live-manual/12/instrument-drum-and-effect-racks/
- Bitwig Studio Userguide, Unified Modulation System and Remote Controls. Useful precedent for
  reusable modulation sources, target routing, depth, and compact page-based control:
  https://www.bitwig.com/userguide/latest/the_unified_modulation_system/
  https://www.bitwig.com/userguide/latest/midi_controllers/
- A. Hunt, M. M. Wanderley, and M. Paradis, *The Importance of Parameter Mapping in Electronic
  Instrument Design*. Supports treating mapping design as a musical interaction problem rather
  than exposing every low-level parameter:
  https://www.nime.org/proc/hunt2002/
- JUCE `AudioProcessorValueTreeState` documentation. Relevant to keeping a stable host parameter
  surface while storing richer versioned state separately:
  https://docs.juce.com/master/classAudioProcessorValueTreeState.html
- G. Peeters, *A Large Set of Audio Features for Sound Description*:
  https://discussion.forum.ircam.fr/uploads/default/original/1X/ffa6a82f823873f864681994c271fb157e41a627.pdf
- MPEG-7 timbral descriptors: log attack time, temporal centroid, harmonic spectral centroid,
  harmonic spectral deviation, harmonic spectral spread, and harmonic spectral variation.
- Essentia algorithm reference and descriptor taxonomy:
  https://essentia.upf.edu/documentation/documentation.html
- Audio Commons perceptual timbral models (useful for future semantic/preset experiments, not a
  dependency for the deterministic core):
  https://audiocommons.github.io/2018/09/05/timbre-sound.html
- A. Roebel and X. Rodet, *Efficient Spectral Envelope Estimation and its Application to Pitch
  Shifting and Envelope Preservation*.
- R. Plomp and W. J. M. Levelt, *Tonal Consonance and Critical Bandwidth*.
- Serra/Bonada spectral-modeling and high-level transformation work for sinusoidal plus residual
  representations.

---

## 13. Document history

| Date | Change |
|------|--------|
| 2026-06-08 | Initial descriptor-bundle draft |
| 2026-06-08 | Repo-fit review: corrected branch/code assumptions, added missing descriptor families, removed speculative critical-path items, and replaced the implementation plan with a staged integration path |
| 2026-06-08 | Added scalable Timbre Motion architecture: macro rack, dynamic descriptor target curves, focused editor, shared motion routing, natural/requested/achieved feedback, bounded conflict-aware resolver, persistence, automation, and UI requirements |
| 2026-06-08 | Amendment: added partial-vs-residual centroid offset, dominant-partial fraction, audio spectrum flatness vector, centroid velocity/acceleration, frame energy stability, voice-quality jitter/shimmer/CPP descriptors; added `MissingWaveform` status; documented repo-specific input contract for partial/residual confidence aggregation, orphan ratio, selection masks, Nyquist clamps, waveform availability, and metadata propagation |

# Product Requirements Document: Modern ATSH Analysis, Morphing, and Synthesis Engine

**Document status:** Draft
**Last updated:** 2026-04-21
**Related docs:** [PRD.md](../PRD.md), [MODERN_ATSH_TASKS.md](./MODERN_ATSH_TASKS.md), [SPECTRAL_ENVELOPE_PRD.md](./SPECTRAL_ENVELOPE_PRD.md)

---

## 1. Executive Summary

This PRD defines a modernized ATSH-style engine for the ATS plugin. The goal is not a museum-perfect clone of ATSH, but a faithful and testable reimplementation of the sound-design affordances that made ATS useful: editable partial trajectories, critical-band residual control, and musically meaningful morphing. The engine should preserve classic ATS compatibility while adding improvements from later sinusoidal, sines-plus-transients-plus-noise, reassigned bandwidth-enhanced, and DDSP-adjacent work only where they improve a VST workflow.

The product should ship with a classic ATS analysis path first, then expose modern analysis modes behind explicit switches. This keeps old ATSH results reproducible while giving new morphing algorithms a more robust representation for attacks, crossing partials, dense spectra, and residual-only energy.

---

## 2. Source Review and Plan Corrections

The original plan remains sound: keep the ATS hybrid partial/noise representation, modernize tracking and transient handling, and separate offline analysis quality from real-time synthesis. The review adds these corrections:

1. **Do not collapse all residual energy onto tracked partials.** The ATSH paper notes that critical-band energy in regions with no tracked partial can use a complementary noise model. The plugin data model needs both per-partial noise and orphan band residual energy.
2. **Treat assignment-based tracking as an engine option, not a permanent replacement until measured.** Neri and Depalle's DAFx 2018 tracker supports real-time-capable assignment via the Hungarian algorithm and directly targets spurious peaks, crossing partials, and non-stationary models. That is a strong modernization candidate, but the classic ATS tracker should remain available for parity tests.
3. **Add transient preservation before full transient modeling.** A complete S+T+N engine is valuable, but a VST can get a large practical gain sooner by detecting onsets, protecting short transient buffers, breaking/resetting partials around attacks, and crossfading transient regions before implementing a full DCT/TMS lane.
4. **Make morph correspondence a first-class feature.** Loris shows that morphing works best when partial labels/correspondences are managed deliberately. ATSH's future-improvement note also explicitly calls out spectral morphing and intelligent selection algorithms, so the plugin should not treat morphing as an afterthought on top of a renderer.
5. **Keep neural/DDSP work as a future assistive path.** Recent DDSP and neural STN work is relevant, but the main product value is the editable parameter space. Neural pieces should be optional helpers, not the core representation for the first modern ATSH engine.

---

## 3. Goals

### 3.1 Product Goals

- Recreate the core ATSH workflow inside the plugin: analyze, inspect, transform, morph, and synthesize.
- Preserve compatibility with classic ATS/ATSH concepts: partials, amplitude/frequency/phase, 25 Bark-band residual energy, SMR-guided relevance, and ATS-style file interchange where practical.
- Enable high-quality morphing with independent frequency, amplitude, noise, time, and transient controls.
- Improve robustness for real musical material: transients, vibrato, close/crossing partials, residual-only bands, and dense or noisy spectra.
- Keep all analysis and destructive transforms off the audio thread.

### 3.2 Non-Goals

- No neural model training in the MVP.
- No promise of transparent reconstruction for all polyphonic music.
- No live low-latency analysis of arbitrary input in the first release.
- No replacement of the existing VST architecture; this PRD specifies the analysis/morph/synthesis engine that plugs into it.

---

## 4. Users and Use Cases

| User | Need |
| --- | --- |
| Electroacoustic composer | Rebuild old ATSH morphing algorithms with modern host recall and automation. |
| Sound designer | Stretch, freeze, hybridize, and morph samples without attack blur. |
| Plugin user | Load a sound, choose a preset analysis mode, automate musically useful controls, and save the DAW session reliably. |
| Research-oriented user | Compare classic ATS analysis with modernized modes and export reproducible model data. |

Representative workflows:

1. Analyze two sources, manually or automatically align partial labels, morph frequency/amplitude/noise independently, and render the result in the host.
2. Analyze a percussive or plucked source, preserve the attack transient, stretch the sustained component, and blend residual bands to taste.
3. Load an old ATS file, synthesize it with classic parity mode, then re-analyze with modern mode for improved morphing.

---

## 5. Functional Requirements

### 5.1 Analysis Modes

The engine shall expose named analysis modes:

- **Classic ATS:** STFT peak picking, ATS-style SMR calculation, recursive frequency/SMR tracking, short-gap fill, short-segment pruning, residual extraction, Bark-band noise analysis, and per-partial noise distribution.
- **Modern ATS:** classic pipeline with improved peak estimation, assignment-capable tracking, onset-aware track handling, orphan residual bands, and richer analysis diagnostics.
- **Experimental:** future mode for reassignment, bandwidth-enhanced partials, or learned/DDSP-assisted parameter estimation. This mode must never be the default for compatibility-critical workflows.

Each analysis result shall record the mode, parameters, engine version, source hash, sample rate, channel handling, and analysis warnings.

### 5.2 Peak Estimation

- Detect spectral peaks within user-configured low/high frequency and magnitude thresholds.
- Provide deterministic interpolation for frequency, magnitude, and phase.
- Store enough diagnostic information to compare peak estimation variants.
- Support a future reassignment/DDM estimator without changing the public model schema.

### 5.3 Partial Tracking

- Preserve a **Classic ATS tracker** for parity and regression tests.
- Add a **tracker interface** with shared track lifecycle semantics: birth, active, sleep, death, gap fill, and pruning.
- Implement a **Modern assignment tracker** whose cost function can combine frequency continuity, amplitude continuity, phase/frequency-slope continuity, SMR continuity, spurious-peak penalties, and birth/death penalties.
- Allow tracker choice per analysis preset.
- Emit confidence and reason codes for debugging: matched, born, slept, interpolated, pruned, or rejected.

### 5.4 Residual and Noise Model

- Extract residual by subtracting phase-aware sinusoidal resynthesis from the source.
- Analyze residual energy in the classic 25 Bark bands by default.
- Store residual in two forms:
  - **Partial noise:** residual energy distributed to partials in the band.
  - **Orphan band noise:** residual energy for critical-band regions with no active partial.
- Keep Bark as the compatibility default; ERB or warped multiresolution bands may be added as non-default modern modes.
- Synthesis shall expose independent partial and residual/noise gain paths.

### 5.5 Transient Handling

Phase 1 transient handling shall be preservation-oriented:

- Detect onset candidates using spectral flux/energy and model discontinuity cues.
- Mark transient windows in analysis data.
- Break or reset partial interpolation rules around strong onsets when smoother continuation would smear the attack.
- Store short transient buffers or residual grains that can be mixed during synthesis.
- Crossfade transient buffers against sinusoidal/noise synthesis to avoid clicks.

Phase 2 may add a full S+T+N lane:

- Dedicated transient component with compact parameters.
- Morph controls for transient timing, gain, and source selection.
- Optional DCT/TMS-style representation if it proves musically useful and testable.

### 5.6 Morphing

The morph engine shall be a model-domain transform, not only an audio crossfade.

- Support two-source morphing at minimum.
- Maintain explicit partial labels and correspondence maps.
- Provide automatic correspondence strategies:
  - harmonic-number matching when F0 or harmonicity is reliable,
  - nearest-frequency matching with slope/SMR constraints,
  - user-overridable labels for non-harmonic sources,
  - orphan handling for unmatched partials.
- Expose independent morph envelopes for time, frequency, amplitude, partial noise, orphan residual, and transient gain.
- Preserve phase sanity at track births, deaths, and transient windows.
- Store morph recipes separately from source models so edits are reproducible and automatable.

### 5.7 Synthesis

- Render classic ATS models and modern models through one real-time-safe synthesis core.
- Support normalized oscillator-bank synthesis for partials, noise-modulated partial synthesis, orphan residual-band noise synthesis, and transient-buffer playback.
- Apply parameter smoothing for host automation.
- Avoid allocations, locks, file I/O, logging, and analysis work on the audio thread.
- Provide offline render parity tools for analysis, regression testing, and documentation examples.

### 5.8 UI/UX Requirements

- Expose analysis mode and tracker mode clearly.
- Show model diagnostics: partial count, frame count, transient markers, orphan residual energy, and warnings.
- Provide an A/B morph workspace with source slots, correspondence view, and independent morph envelopes.
- Keep expert analysis controls available, but ship musical presets for common sources: voice, sustained instrument, plucked/percussive, noisy texture, and polyphonic material.

---

## 6. Technical Architecture

### 6.1 Engine Boundaries

- **Analysis engine:** offline/worker-thread only; deterministic; owns FFT, peak estimation, tracking, residual extraction, and model serialization.
- **Morph engine:** offline or control-rate; produces immutable model snapshots or morph render state.
- **Synthesis engine:** audio-thread-safe; reads immutable snapshots; no ownership of analysis memory.
- **UI/plugin layer:** parameter mapping, host state, progress/cancel, preset management, and visualization.

### 6.2 Data Model

The model shall store:

- Header: version, mode, sample rate, hop size, window size, source metadata, parameter hash.
- Frames: time, active partial ids, transient markers, residual-band energy.
- Partials: id, label, channel/group, amplitude, frequency, phase, SMR, confidence, per-frame noise energy.
- Orphan residual: band energy not assigned to a partial.
- Transients: onset time, window bounds, gain, optional transient buffer/grain metadata.
- Morph metadata: source ids, correspondence maps, envelopes, and unmatched-partial policies.

### 6.3 Compatibility

- Classic mode should render old ATS files as closely as the existing engine permits.
- Modern mode may add fields, but serialized data must be versioned and forward-compatible.
- Importers must not discard unknown future fields unless the user explicitly saves to a legacy format.

---

## 7. Quality and Acceptance

### 7.1 Golden Tests

- Classic analyzer/synthesizer output compared against ATSA/Qatsh references within documented tolerance.
- ATS file round-trip tests for all supported file types.
- Fixed seed/noise determinism tests for resynthesis.
- Morph recipe snapshot tests to ensure automation and session recall reproduce the same model.

### 7.2 Audio Evaluation

Use a small fixture suite:

- sustained monophonic pitched tone,
- vibrato tone,
- crossing/close partial synthetic test,
- plucked string or piano-like attack,
- noisy/breathy voice or flute,
- dense polyphonic excerpt,
- percussive hit.

Measure and listen for:

- reconstruction error and spectral distance,
- partial track continuity,
- transient sharpness/spectral flux preservation,
- residual/orphan noise energy preservation,
- CPU and memory budgets at 48 kHz / 256 samples.

### 7.3 Acceptance Criteria

- Classic mode can reproduce the existing offline CLI tests without regressions.
- Modern tracker improves or matches classic tracker on the crossing/close-partial fixture without materially worsening sustained monophonic fixtures.
- Transient preservation mode improves attack sharpness on plucked/percussive fixtures without clicks.
- Morphing two analyzed sources is reproducible from saved plugin state.
- Synthesis remains real-time-safe under plugin stress tests.

---

## 8. Risks and Open Questions

- **Licensing:** confirm whether ATSA/Qatsh-derived code can ship under the intended product strategy.
- **Parity vs modernization:** avoid changing defaults in ways that make old ATSH users lose familiar results.
- **Transient storage size:** short buffers improve attacks but may enlarge plugin state.
- **Tracker complexity:** assignment tracking is promising, but dense frames can still be costly; cap candidate counts and profile early.
- **Morph UI scope:** automatic correspondence is useful, but old ATS-style algorithmic editing still needs manual overrides.
- **Stereo/multichannel analysis:** decide whether to analyze mono sum, per-channel models, or mid/side models.

---

## 9. References

- DXARTS ATS overview: https://dxarts.washington.edu/wiki/analysis-transformation-and-synthesis-ats
- Juan Pampin, "ATS: A System for Sound Analysis Transformation and Synthesis Based on a Sinusoidal plus Critical-Band Noise Model and Psychoacoustics": https://dxarts.washington.edu/sites/dxarts/files/documents/wiki/ats_theory.pdf
- Oscar Pablo Di Liscia and Juan Pampin, "Spectral analysis based synthesis and transformation of digital sound: the ATSH program": https://compmus.ime.usp.br/sbcm/2003/researchpapers/sbcm-researchpaper-2003-10.pdf
- Julian Neri and Philippe Depalle, "Fast Partial Tracking of Audio with Real-Time Capability through Linear Programming": https://www.music.mcgill.ca/~julian/wp-content/uploads/2023/09/2018_DAFx_Neri_Fast_Partial_Tracking_Assignment.pdf
- Kelly Fitz et al., "Sound Morphing using Loris and the Reassigned Bandwidth-Enhanced Additive Sound Model": https://quod.lib.umich.edu/i/icmc/bbp2372.2002.079
- T. Verma and T. Meng, "Extending spectral modeling synthesis with transient modeling synthesis": https://research.aalto.fi/en/publications/extending-spectral-modeling-synthesis-with-transient-modeling-syn/
- Leonardo Fierro and Vesa Valimaki, "Enhanced Fuzzy Decomposition of Sound Into Sines, Transients, and Noise": https://research.aalto.fi/en/publications/enhanced-fuzzy-decomposition-of-sound-into-sines-transients-and-n/
- Jesse Engel et al., "DDSP: Differentiable Digital Signal Processing": https://arxiv.org/abs/2001.04643
- Riccardo Simionato and Stefano Fasciani, "Sines, transient, noise neural modeling of piano notes": https://www.frontiersin.org/journals/signal-processing/articles/10.3389/frsip.2024.1494864/full

# ATS / ATSH Feature Matrix

## Implementation status (plugin, May 2026)

Reviewed against: **Session**, **Analysis**, **Synthesis**, **Partials**, **Math** tabs; `src/plugin/*`; `src/core/AtsMorphRecipe.*`; classic ATSA analysis via `tools/ats_offline.c` / Qatsh.

### Legend

| Status | Meaning |
| ------ | ------- |
| **Done** | End-to-end in product: UI (where applicable) + engine + audible preview or export |
| **Partial** | Some layer works (analysis-only, offline-only, UI stub, tests-only, or subset of classic ATSH) |
| **Not started** | No meaningful implementation in this repo |

### Summary

| Area | Done | Partial | Not started |
| ---- | ---- | ------- | ----------- |
| Analysis model | 7 | 0 | 0 |
| Pitch & frequency | 4 | 1 | 2 |
| Time | 2 | 3 | 1 |
| Amplitude & envelope | 2 | 3 | 1 |
| Noise | 4 | 2 | 0 |
| Spectral editing | 2 | 4 | 0 |
| Hybridization & morphing | 1 | 5 | 0 |
| Psychoacoustic | 3 | 1 | 0 |
| ATSH graphical editing | 4 | 2 | 0 |
| Modern extensions | 0 | 1 | 7 |

**Recent (May 2026):** Synthesis **Frequency shaping** adds Stretch, Compress, and Harmonic modes with live spectrogram warp display, pivot/F0 overlay, auto pivot readout, preview resynth, and factory presets. Residual / Iso 3D Residual modes also include a **Bark-band noise editor**: left Hz gutter, per-band envelope draw, per-cell dB edit, smooth, tilt, copy/paste/swap band — all writing `band_energy` on Current / Source A / B via `mutateResidualBandFrames`.

**Largest gaps vs classic ATSH:** formant-preserving pitch, partial trajectory *drawing*, true real-time resynthesis in `processBlock` (preview is pre-rendered buffers), formant/spectral-warp transforms, and multi-source (>2) hybridization.

**Strongest completed vertical:** dual-source ATSA analysis → `.ats` import/export → morph recipe (Bark/Mel, correspondence, lanes, timeline) → offline morph snapshot preview → spectrogram editing (partials + **Bark residual bands**: mute/scale/delete, envelope draw, tilt, clipboard).

---

## Overview

ATS (Analysis Transformation Synthesis) and ATSH (ATS Hybrid) were spectral modeling systems developed primarily by Juan Pampin and researchers at DXARTS. Unlike traditional phase vocoders, ATS models a sound as:

1. Deterministic sinusoidal partial tracks
2. Stochastic noise bands

This separation allows transformations to be performed independently on tonal and noisy components while maintaining perceptual coherence.

---

# Analysis Model

| Feature              | Technical Description                                               | Notes                                  | Status      | Plugin evidence                                                                                                                                                                     |
| -------------------- | ------------------------------------------------------------------- | -------------------------------------- | ----------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Partial Tracking     | Tracks sinusoidal components across time as continuous trajectories | Core ATS representation                | **Done**    | ATSA `trackLen`, `minSegLen`, `minGapLen`; Analysis tab + profiles                                                                                                                  |
| Frequency Estimation | Peak interpolation and trajectory linking between frames            | More stable than frame-local FFT peaks | **Done**    | `freqDev`, `lastPeakContribution`, window/hop controls                                                                                                                              |
| Amplitude Tracking   | Stores amplitude envelopes per partial                              | Enables independent editing            | **Done**    | Analysis type 1+; Partials tab cell edit; spectrogram scale                                                                                                                         |
| Phase Tracking       | Maintains phase continuity during resynthesis                       | Reduces artifacts                      | **Partial** | Analysis type 2+; Synthesis “Use partial phases”; morph phase *policies* in recipe, not full frame phase morph in UI                                                                |
| Noise Modeling       | Residual energy stored separately in critical-band representation   | Major ATS innovation                   | **Done**    | Analysis type 3/4; residual audition; band_energy in model                                                                                                                          |
| Bark Band Analysis   | Noise energy grouped into psychoacoustic Bark bands                 | Perceptually motivated                 | **Done**    | ATSA `band_energy` + critical-band edges; Residual / Iso 3D Residual views with Hz gutter, band selection, and Bark-band edit toolbar (draw / dB / smooth / tilt / copy-paste-swap) |
| SMR Analysis         | Signal-to-Mask Ratio calculation for perceptual weighting           | Used for pruning and optimization      | **Done**    | `SMR threshold`, `min segment SMR`, `SMR weight`; profiles tune these                                                                                                               |

---

# Pitch & Frequency Transformations

| Feature                        | Technical Description                               | Potential Modern Equivalent | Status          | Plugin evidence                                             |
| ------------------------------ | --------------------------------------------------- | --------------------------- | --------------- | ----------------------------------------------------------- |
| Pitch Shift                    | Scales partial frequencies by ratio                 | Spectral transposition      | **Done**        | Synthesis `freq`; MIDI note-on offset in `processBlock`     |
| Formant-Preserving Pitch Shift | Pitch changes while spectral envelope remains fixed | Vocal processing            | **Not started** | —                                                           |
| Frequency Stretching           | Expands frequency relationships non-linearly        | Inharmonic timbres          | **Done** | Synthesis Frequency shaping: Stretch mode + live trace warp + preview resynth |
| Frequency Compression          | Compresses spectral spacing                         | Spectral clustering         | **Done** | Synthesis Frequency shaping: Compress mode + live trace warp + preview resynth |
| Harmonic Scaling               | Adjusts harmonic series relationships               | Instrument morphing         | **Done** | Synthesis Frequency shaping: Harmonic mode, rank-based v1 + auto F0 pivot |
| Partial Detuning               | Individual partial offsets                          | Chorus-like effects         | **Partial**     | Per-cell freq in Partials tab; no detune spread UI          |
| Spectral Warping               | Arbitrary frequency mapping function                | Advanced timbral design     | **Not started** | Morph freq domain (log/Bark) is interpolation, not warp map |

---

# Time Transformations

| Feature | Technical Description | Notes | Status | Plugin evidence |
| ------- | --------------------- | ----- | ------ | ----------------- |
| Time Stretch | Rescales temporal evolution of trajectories | Preserves pitch | **Done** | Synthesis `timeStretch` |
| Time Compression | Shortens sound duration | Opposite of stretch | **Done** | Same control (< 1.0) |
| Spectral Freeze | Holds partial states over time | Infinite sustain | **Not started** | — |
| Temporal Interpolation | Smooth transition between temporal states | Morphing tool | **Partial** | Morph timeline A→B; morph *lanes* include Time; not arbitrary state A/B/C |
| Trajectory Resampling | Reconstructs partial motion at different rates | Useful for generative systems | **Partial** | Implicit via time factor + analysis hop; no dedicated resample tool |
| Scrubbing | Non-linear traversal through spectral data | Interactive playback | **Partial** | Spectrogram playhead during preview; no live scrub-driven resynth |

---

# Amplitude & Envelope Operations

| Feature | Technical Description | Notes | Status | Plugin evidence |
| ------- | --------------------- | ----- | ------ | ----------------- |
| Gain Scaling | Scale all amplitudes | Global adjustment | **Done** | `partGain`, `resGain`, `outputTrim` (Session) |
| Partial Gain Scaling | Scale selected partials | Spectral EQ | **Partial** | Vertical drag scale on selected traces (spectrogram) |
| Envelope Remapping | Modify amplitude trajectories | Dynamic reshaping | **Partial** | MathExpr scripts; Partials tab cell edit; **residual band envelope draw** (Draw mode, dB vs time per Bark band) |
| Spectral Tilt | Emphasize high or low partials | Brightness control | **Partial** | Residual **Tilt** control (dB per band step across selection); partial-trace tilt not implemented |
| Harmonic Weighting | Adjust harmonic distribution | Instrument coloration | **Not started** | — |
| Spectral Filtering | Operates directly on partials | Alternative to FFT filtering | **Partial** | Mute/delete partials & residual bands |

---

# Noise Operations

| Feature | Technical Description | Notes | Status | Plugin evidence |
| ------- | --------------------- | ----- | ------ | ----------------- |
| Noise Only Resynthesis | Playback stochastic component alone | Texture extraction | **Done** | Audition Residual; Synthesis “Residual only” |
| Harmonic Only Resynthesis | Playback sinusoidal component alone | Tonal isolation | **Done** | Audition Partials; combined modes |
| Noise Scaling | Amplify or attenuate noise bands | Texture control | **Done** | `resGain`; vertical drag scale; per-cell **dB** read/write on `band_energy` |
| Noise Redistribution | Move energy between Bark bands | Spectral coloration | **Partial** | **Copy / paste / swap** band clipboard over frame range; no automatic rebalancing or drag-between-bands |
| Noise Morphing | Interpolate stochastic structures | Sound design | **Partial** | Morph **Noise** lane (independent of Freq/Amp) + `morphResidualNoiseBands`; offline snapshot |
| Residual Manipulation | Direct editing of non-tonal energy | Advanced processing | **Done** | Residual / Iso 3D Residual: lasso, mute, scale, envelope draw, smooth, tilt, copy/paste/swap; `mutateResidualBandFrames` |

---

# Spectral Editing

| Feature | Technical Description | Notes | Status | Plugin evidence |
| ------- | --------------------- | ----- | ------ | ----------------- |
| Partial Deletion | Remove selected tracks | Spectral cleanup | **Done** | Spectrogram Delete → `mutePartials` |
| Partial Isolation | Solo individual trajectories | Analysis tool | **Partial** | Multi-select + audition; no solo-one-track button |
| Partial Cloning | Duplicate spectral structures | Experimental | **Not started** | — |
| Partial Group Editing | Operate on harmonic groups | Musical editing | **Partial** | Lasso multi-partial scale/delete |
| Spectral Pruning | Remove low-SMR components | Data reduction | **Partial** | At analysis time via SMR thresholds, not post-hoc prune UI |
| Harmonic Extraction | Isolate harmonic subsets | Timbre simplification | **Not started** | — |

---

# Hybridization & Morphing

| Feature | Technical Description | Notes | Status | Plugin evidence |
| ------- | --------------------- | ----- | ------ | ----------------- |
| Spectral Interpolation | Blend partial trajectories | Smooth morphing | **Done** | `AtsMorphRecipe`, timeline, lanes, snapshot playback |
| Timbre Morphing | Transition between analyses | Signature ATS capability | **Partial** | Dual source A/B + Session morph UI; stale-snapshot UX; offline render path |
| Partial Replacement | Swap partial sets between sounds | Hybrid instruments | **Partial** | Unmatched source/target policies (fade, hold, merge residual, discard) |
| Noise Transfer | Apply stochastic model from one sound to another | Texture borrowing | **Partial** | Merge-to-residual policies; noise lane |
| Cross-Synthesis | Merge properties from multiple analyses | Creative design | **Partial** | Morph lanes (Freq/Amp/Time/Transient/Noise/Phase) |
| Multi-Sound Hybridization | Combine multiple ATS datasets | Composite timbres | **Not started** | Two slots only (A, B) |

---

# Psychoacoustic Features

| Feature | Technical Description | Notes | Status | Plugin evidence |
| ------- | --------------------- | ----- | ------ | ----------------- |
| SMR Weighting | Prioritize perceptually important partials | Efficiency | **Done** | Analysis SMR params + tracking weight |
| Masking-Based Pruning | Remove inaudible information | Compression | **Partial** | Threshold-driven at analysis; no interactive SMR map |
| Critical Band Modeling | Noise represented psychoacoustically | Natural perception | **Done** | ATSA critical bands in model; morph Bark/Mel grid; editable residual band map in spectrogram |
| Perceptual Resynthesis | Focus resources on audible content | Optimization | **Not started** | No adaptive partial budget at playback |

---

# ATSH Graphical Editing Features

| Feature | Technical Description | Status | Plugin evidence |
| ------- | ----------------------------- | ------ | ----------------- |
| Trajectory Editing | Draw and modify partial paths | **Partial** | Partial traces: delete/scale only; **residual Bark bands**: envelope draw mode |
| Spectral Visualization | View frequency trajectories | **Done** | `SpectrogramPanel` trace + isometric modes |
| Noise Visualization | Inspect stochastic content | **Done** | Residual / iso residual; **Bark Hz gutter** + hover readout (band, frame, dB) |
| Bark-band noise editor | Edit stochastic energy per critical band | **Done** | Second toolbar in Residual modes: Draw, Smooth, Tilt, Copy/Paste/Swap, Cell dB |
| Partial Selection Tools | Edit subsets of tracks | **Done** | Click, lasso, band/time residual selection |
| Interactive Resynthesis | Immediate auditory feedback | **Partial** | Background resynth → preview buffer → `processBlock` plays buffer (not live oscillator bank) |
| Morphing Interface | Visual interpolation controls | **Done** | Session timeline, lanes, policies, band strip |

---

# Interesting Modern Extensions

These ideas are particularly relevant for modern AI music systems, spectral VSTs, and generative composition.

| Modern Feature | ATS Foundation | Status | Plugin evidence |
| -------------- | -------------- | ------ | ----------------- |
| AI Timbre Morphing | Spectral interpolation | **Not started** | Classical morph recipe only |
| Neural Instrument Hybridization | Partial replacement | **Not started** | — |
| Spectral Diffusion Models | Noise + partial separation | **Not started** | — |
| Constraint-Based Timbre Generation | Partial trajectory rules | **Partial** | Morph recipe + tests (`morph_recipe_test`, metric harness) |
| Generative Spectral Composition | Trajectory synthesis | **Not started** | — |
| Timbre Grammars | Harmonic/noise transformations | **Not started** | — |
| Spectral Canon Generation | Time-shifted trajectory structures | **Not started** | — |
| On-Chain Spectral NFTs | Compact ATS parameter storage | **Not started** | `.ats` / preset export only |
| Differentiable Spectral Synthesis | Partial trajectory representation | **Not started** | — |

---

# Priority queue (recommended build order)

Ordered for **musical payoff**, **dependency order**, and **closing gaps visible in current UI**. Items marked *(in progress)* already have UI or core code that needs finishing rather than greenfield work.

| Prio | Feature / milestone | Why now | Status today |
| ---- | ------------------- | ------- | ------------ |
| **P0** | **Real-time morph preview** — drive morph amount from timeline/lanes without full offline bounce per tweak | Session tab promises live morph; snapshot stale warnings indicate gap | **Partial** (offline `evaluateMorphRecipeSnapshot` + buffer playback) |
| **P0** | **Morph fixture audio + metric CI** — populate `morph_fixture_manifest` WAVs; tighten harness in CI | Locks recipe quality before more UI | **Partial** (harness exists; assets placeholders) |
| **P0** | **Assignment matrix strategy in UI** | Correspondence combo includes “Assignment matrix”; ensure parity with rank/greedy in audible path | **Partial** (core + tests; verify plugin render path) |
| **P1** | **Partial trajectory draw/edit** (freq vs time) | Residual Bark bands have envelope draw; partial paths still view + scale/delete only | **Partial** |
| **P1** | **Spectral freeze + scrub-driven preview** | Performance / sound-design staples | **Not started** / **Partial** |
| **P2** | **Noise redistribution** (automatic / drag between bands) | Copy-paste-swap done; full redistribution TBD | **Partial** |
| **P2** | **Partial solo + harmonic group ops** | Faster editing than lasso-only | **Partial** |
| **P2** | **Spectral tilt on partial traces** | Residual-band tilt done; partial harmonic weighting not started | **Partial** |
| **P2** | **Frequency stretch / compression / warp maps** | Inharmonic + creative mapping | **Partial** (Stretch/Compress/Harmonic shipped; arbitrary warp maps TBD) |
| **P2** | **Multi-source hybrid (>2)** | Extends working A/B morph | **Not started** |
| **P3** | **Partial cloning** | Experimental ATSH | **Not started** |
| **P3** | **Perceptual resynthesis** (partial budget) | Optimization / CPU | **Not started** |
| **P3** | **Modern ML extensions** (AI morph, diffusion, differentiable synth) | Research track | **Not started** |

### Already in good shape (maintain, don’t reorder)

- Dual-source workflow (Analysis + Session): choose, re-analyze, play, export ATS, audition Both/Partials/Residual  
- Analysis profiles + full ANARGS parameter surface  
- Synthesis SPARAMS mirror (freq, time, window, phase, partials/residual modes)  
- Morph recipe schema, lane policies, timeline bounce, variation slots  
- Spectrogram + iso 3D: partial selection, delete, gain drag  
- **Bark residual editor** (Residual / Iso 3D Residual): Hz gutter, envelope draw, cell dB, smooth, tilt, copy/paste/swap band  
- Morph **Noise lane** independent of partial morph (same timeline; unlink in Session)  
- Presets, `.ats` I/O, MathExpr offline transforms  

---

# Core Insight

ATS is best understood not as an effect processor but as a structured representation of sound.

Instead of operating on FFT frames, ATS represents sound as:

- Partial trajectories through time
- Independent noise structures
- Psychoacoustic importance measures

This makes it particularly suitable for:

- Generative timbre systems
- Machine learning
- Spectral morphing
- Hybrid instruments
- Rule-based spectral composition
- Real-time timbre manipulation
- On-chain procedural audio representations

# Product Requirements Document: ATS / Qatsh as a VST Plugin

**Document status:** Draft  
**Last updated:** 2026-04-21
**Related upstream projects:** [Qatsh](https://gitlab.com/dxarts/projects/ats/qatsh) (Qt front-end for ATS), [ATSA](https://gitlab.com/dxarts/projects/ats/atsa) (C library for ATS analysis API)
**Related modernization plan:** [Modern ATSH analysis, morphing, and synthesis PRD](docs/MODERN_ATSH_PRD.md)

---

## 1. Executive summary

This document defines requirements for a **DAW plugin** that exposes the **Analysis–Transformation–Synthesis (ATS)** workflow today implemented in **Qatsh** (the Qt successor to the original **ATSH** Gtk application). The plugin will let users analyze audio into an ATS representation (sinusoidal partials plus critical-band noise), optionally transform partial and residual data, and **resynthesize** audio inside a host, with parameters and state suitable for mixing, sound design, and recall in projects.

**Naming note:** “ATSH” refers to the historical Gtk application; **Qatsh** is its maintained Qt port and the primary reference implementation in this repository. The product is colloquially “ATS as a plugin”; marketing name TBD.

---

## 2. Background

### 2.1 What ATS is

ATS models sound as **sinusoidal tracks** (partial amplitudes, frequencies, optional phases) plus **critical-band noise** (residual) envelopes. Analysis is non-trivial and CPU-intensive; synthesis is comparatively lighter but still depends on model size and host buffer sizes.

### 2.2 Current software baseline

| Component | Role |
|-----------|------|
| **Qatsh** | Desktop GUI: sound I/O, analysis UI, spectrogram/table editors, math-expression partial transforms, synthesis parameters, file workflows. |
| **ATSA** (`atsa.h`) | C API: analysis parameters (`ANARGS`), sound/model types (`ATS_SOUND`), synthesis parameters (`SPARAMS`), analysis and resynthesis entry points. |
| **MathExpr** (bundled in Qatsh) | Expression language for batch partial/residual transformations. |

### 2.3 Why a plugin

- **In-context processing:** analyze a clip or stem without round-tripping through a standalone app.  
- **Automation:** DAW automation of synthesis and musically relevant transforms.  
- **Recall:** presets and project save/load aligned with host expectations (VST3 state).  
- **Distribution:** single installable binary per platform in standard plugin formats.

---

## 3. Product vision and goals

### 3.1 Vision

A **professional-grade** effect/instrument-style plugin that brings **Qatsh-class ATS capabilities** into the DAW: faithful enough for researchers and composers who already use ATS, and streamlined enough for producers who want spectral “freeze,” morphing, and partial-based sculpting.

### 3.2 Primary goals (MVP)

1. **Load or capture audio** in-plugin (file import and/or sidechain / record-arm capture—exact source TBD per §6.2).  
2. **Run ATS analysis** using parameters equivalent to Qatsh’s standard analysis (`ANARGS` / UI parity).  
3. **Store the ATS model** in memory; optional **export/import** of native ATS or project-side blob (format TBD).  
4. **Real-time or quasi-real-time synthesis** of the model through the host signal path with stable latency reporting.  
5. **VST3** on **macOS** and **Windows** (architecture: Apple Silicon + Intel where applicable).  
6. **Parameter exposure** for the main synthesis levers (see §5.2) and **host state persistence** (VST3 `IComponent::getState` / `setState`).

### 3.3 Non-goals (initial releases)

- Full pixel-perfect reproduction of every Qatsh dialog and niche feature on day one.  
- **CLAP / AU / AAX** unless explicitly added in a later phase (AU often required for Logic; track as Phase 2).  
- Guaranteed **live analysis** of arbitrary live input at low latency (may be phased; see §7).  
- iOS plugin or web audio target.

---

## 4. Target users and use cases

| Persona | Needs |
|---------|--------|
| **Electronic / sound artist** | Freeze spectral content, stretch, transpose partials, blend residual. |
| **Film / game audio** | Design evolving textures from samples; automate morphs. |
| **Research / pedagogy** | Inspect partial tracks; reproducible analysis settings; export data for study. |

**Representative scenarios**

1. Import a vocal clip → analyze → reduce residual → transpose partials + automate noise level during a chorus.  
2. Analyze a short instrumental hit → “freeze” → play chromatically from MIDI (if instrument mode is in scope).  
3. Batch-compare analysis presets (window, hop, SMR weighting) from saved `.apf`-equivalent presets.

---

## 5. Functional requirements

### 5.1 Analysis

- **Parameter set** shall match **ATSA / Qatsh standard analysis** semantics: start, duration, lowest/highest frequency, frequency deviation, window cycles, hop ratio, magnitude threshold, track length, segment/gap cleanup, SMR thresholds, last-peak and SMR contribution weights, window type, etc. (see ATSA README / `ANARGS` in `atsa.h`).  
- **Progress and cancellation:** long runs shall report progress to the UI and allow **cancel** without crashing the host.  
- **Determinism:** same input file + same parameters + same host sample rate → bitwise-identical model (within floating-point tolerance), documented if not achievable.  
- **Sample rate:** analysis shall use the **host processing sample rate** or a clearly documented internal rate with SRC (tradeoff: quality vs CPU).

### 5.2 Synthesis (MVP parameters)

Expose at minimum the controls mirrored by Qatsh’s `ATSSynthesis` / `SPARAMS` conceptual model:

- Start time, duration, partial selection mode (all vs selected indices if UI supports it).  
- Use partial phases (on/off).  
- Partial amplitude factor, residual amplitude factor.  
- Frequency factor, time factor.  
- Output sample rate follows host; internal oversampling TBD.

### 5.3 Transformation (phased)

- **Phase 1 (optional MVP):** simple global transforms (transpose, time stretch factors already in synthesis).  
- **Phase 2:** partial table editing parity with Qatsh (selection, frame editing).  
- **Phase 3:** **MathExpr**-style formula transforms with the same variable semantics as `README.txt` in Qatsh (P, T, p, t, `A(p,t)`, etc.), executed **offline** or on explicit user action—not on the audio thread without a hard safety review.
- **Modern ATSH extension:** source-to-source morphing, assignment-capable tracking, orphan residual-band preservation, and transient-aware synthesis are specified separately in [docs/MODERN_ATSH_PRD.md](docs/MODERN_ATSH_PRD.md).

### 5.4 Persistence and interchange

- **Plugin state** includes: last analysis parameters, synthesis parameters, model blob (or path + checksum if external file).  
- **Preset browser:** factory + user presets; names and tags.  
- **Optional file interchange:** load/save ATS files compatible with Qatsh/ATSA ecosystem where formats allow.

### 5.5 UI / UX

- **Clear mode indication:** “No model,” “Analyzed,” “Synthesizing.”  
- **CPU guardrails:** warn when partial count / frame count may exceed real-time budget.  
- **Accessibility:** keyboard focus order, readable contrast, scalable UI (vector or DPI-aware raster).

---

## 6. Technical architecture

### 6.1 Recommended direction

- **Plugin shell:** [VST3 SDK](https://developer.steinberg.help/) or a framework (**JUCE**, **iPlug2**) that targets VST3 and can wrap a **DSP core** callable from a non-UI thread.  
- **DSP / analysis core:** reuse **ATSA** C library (same code as [standalone ATSA](https://gitlab.com/dxarts/projects/ats/atsa) or the copy vendored inside Qatsh) behind a thin C++ façade; avoid dragging **Qt** into the real-time path.  
- **UI:** either **JUCE**-native UI, **WebView**-based UI, or a **minimal Qt** build used only for the editor window (evaluate binary size, GPL implications, and host sandbox rules—macOS hardened runtime/notarization).

### 6.2 Plugin category

Decision required:

- **Effect (audio in → audio out):** simplest for MVP; analysis from **recorded buffer** or **imported file**.  
- **Instrument (MIDI in):** optional later for pitched playback of frozen models.

### 6.3 Threading model

- **Audio thread:** synthesis only; **no** analysis, file I/O, or expression evaluation.  
- **Worker thread(s):** analysis, model rebuild, heavy transforms.  
- **Lock-free handoff:** immutable model snapshots or double-buffered model swap after analysis completes.

### 6.4 Build and platforms

- **CI:** build matrix macOS (arm64, x86_64), Windows (x64); code signing and notarization for macOS distribution.  
- **Dependencies:** libsndfile or host-provided decode path for file import; static vs dynamic linking decision recorded in release notes.

---

## 7. Real-time, latency, and performance

| Concern | Requirement |
|---------|-------------|
| **Real-time safety** | No allocations, locks, I/O, or logging on the audio thread in release builds. |
| **Latency** | Report PDC (`IAudioProcessor::getLatencySamples`) if synthesis introduces delay; default **zero** or fixed lookahead documented. |
| **Analysis** | May be **non-real-time** in MVP; UI must not block host audio indefinitely. |
| **CPU** | Document maximum recommended partials/frames for 48 kHz / 256 samples on reference hardware. |

---

## 8. Licensing and compliance

- **Qatsh GUI** components are **GPL v2+**; **icons** mix GPL and LGPL (see `COPYRIGHT.txt`).  
- **ATSA** carries its own license (see `src/atsa/README.txt` in Qatsh / ATSA repo).  
- **MathExpr** has a GPL-like license (see `mathexpr.h`).  

**Action item:** legal review for **combined work** in a closed-source commercial plugin is **out of scope** for engineering alone; the PRD assumes either **GPL-compliant distribution** or a **re-licensing / clean-room** path negotiated with rights holders.

**VST3:** comply with Steinberg VST3 SDK licensing and trademark guidelines.

---

## 9. Quality, testing, and acceptance

- **Unit tests:** parameter marshaling, model serialization round-trip, synthesis invariants.  
- **Integration tests:** headless render in reference hosts (e.g., Reaper) or validator tools.  
- **Golden tests:** compare short synthesis outputs against Qatsh/ATSA CLI references within tolerance.  
- **Acceptance:** no denormals/noise burst on enable/bypass; passes pluginval-style stress where applicable.

---

## 10. Milestones (indicative)

| Phase | Scope |
|-------|--------|
| **P0 – Spike** | Static lib ATSA + offline render to WAV from command-line using host SR; CPU profiling. |
| **P1 – MVP VST3** | Import WAV → analyze → synthesize in effect slot; core parameters; state save/restore. |
| **P2 – UX** | Spectrogram/partial view read-only; preset system; better progress/cancel. |
| **P3 – Transform** | Table editing + MathExpr offline transforms. |
| **P4 – Platform** | AU / additional formats; instrument mode if desired. |

---

## 11. Risks and open questions

1. **GPL vs product strategy:** default assumption of open-source distribution unless rights are cleared.  
2. **Qt dependency:** large footprint; consider replacing Qt-only code paths with JUCE equivalents.  
3. **Analysis on live input:** may require ring-buffer capture + large latency; defer or scope explicitly.  
4. **Model size:** memory caps for very long files; streaming synthesis vs full in-RAM model.  
5. **Numerical parity:** confirm `SPARAMS` / internal ATS version alignment between vendored ATSA and standalone ATSA.

---

## 12. Glossary

- **ATS:** Analysis–Transformation–Synthesis spectral modeling system (UW DXARTS lineage).  
- **ATSH:** Original Gtk front-end; superseded in this codebase by **Qatsh**.  
- **ATSA:** Analysis library / API in C.  
- **Partials / residual:** Sinusoidal tracks vs critical-band noise component of the model.

---

## 13. References

- Qatsh upstream: https://gitlab.com/dxarts/projects/ats/qatsh  
- ATSA upstream: https://gitlab.com/dxarts/projects/ats/atsa  
- ATS overview: https://dxarts.washington.edu/research/technology/ats  

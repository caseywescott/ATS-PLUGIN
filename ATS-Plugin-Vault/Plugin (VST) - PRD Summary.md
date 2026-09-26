# Plugin (VST) — PRD Summary

**Source:** `PRD.md` (main repo and `spectral-envelope` worktree are aligned as of last sync).  
**Full doc:** [PRD.md](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/PRD.md)

## One sentence

A **professional DAW plugin** that brings **Qatsh-class ATS** (analyze → transform → resynthesize) into the host, with **VST3** state/recall and parameters suited to production and research.

## What you are building (MVP)

1. **Audio in:** file import and/or capture path (effect-first vs instrument later — see architecture ADRs in worktree `docs/adr/`).
2. **Analysis** matching **ATSA / Qatsh `ANARGS`** semantics, with **progress + cancel**, **determinism** where documented, host SR (or documented SRC).
3. **In-memory ATS model**; optional interchange / project blob (format TBD).
4. **Synthesis** in the signal path with correct **latency reporting** if non-zero.
5. **VST3** on **macOS** (arm64 + Intel) and **Windows x64**.
6. **Parameters** for core **`SPARAMS` / ATSSynthesis** levers + **full plugin state** persistence.

## Synthesis parameters (minimum surface)

Start/duration, partial selection mode, phases on/off, partial amp, residual amp, frequency factor, time factor; host SR.

## Transform roadmap (product)

- **Phase 1:** global factors (overlap with synthesis).
- **Phase 2:** partial table editing like Qatsh.
- **Phase 3:** **MathExpr** offline / explicit user action only.
- **Parallel track:** modern engine — see [[Modern ATSH Engine - PRD Summary]]; spectral envelope work — see [[Spectral Envelope - PRD Summary]].

## Architecture (non-negotiable direction)

- **Shell:** VST3 SDK or JUCE / iPlug2 — RT-safe DSP boundary.
- **Core:** **ATSA** C behind a thin C++ layer; **no Qt on the audio thread**.
- **UI:** JUCE, WebView, or minimal Qt editor — decide per footprint, GPL, notarization.

## Threading

- **Audio:** synthesis only; hand off **immutable model snapshots** or double-buffer after worker completes analysis/transform.

## Milestones (from PRD)

| Phase | Content |
| --- | --- |
| P0 | ATSA static lib + offline CLI render + profile |
| P1 | MVP VST3: WAV → analyze → synthesize + state |
| P2 | Read-only spectro/partial UX, presets, progress |
| P3 | Table edit + MathExpr offline |
| P4 | AU / more formats; optional instrument mode |

## Top risks / open questions

1. **GPL vs shipping model** — default assumption in PRD: open distribution unless rights cleared.
2. **Qt** — avoid dragging full GUI stack into RT; size and license.
3. **Live input analysis** — latency and scope; defer or specify.
4. **Model size** — memory caps, streaming vs full RAM.
5. **Numerical parity** — vendored ATSA vs upstream alignment.

## Related

- [[Modern ATSH Engine - PRD Summary]]
- [[Worktrees and canonical paths]]

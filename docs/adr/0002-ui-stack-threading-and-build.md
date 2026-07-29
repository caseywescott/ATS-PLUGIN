# ADR 0002: UI stack, threading, and build system

## Status

Accepted (Phase 0; implementation details may evolve)

## Context

The codebase must stay **real-time safe** on the audio thread while running **heavy ATS analysis** and optional **MathExpr** transforms. Qatsh is Qt-based; the PRD warns about Qt size and GPL coupling.

## Decision

1. **Build system:** **CMake** for the DSP/tooling core, tests, and native
   plugin artifacts. The production graph uses the pinned Visage backend and
   raw Steinberg VST3 / Apple AudioUnit SDK projections; the retired JUCE
   target is not selected by `BUILD_PLUGIN`.
2. **Qt in the plugin editor:** **Avoid for MVP.** The Visage editor is the
   selected native UI backend. Revisit Qt only if a hard requirement emerges
   to reuse large portions of Qatsh widgets (accept GPL and binary size
   tradeoffs).
3. **Threading contract:**
   - **Audio thread:** **synthesis only** (read a frozen/immutable model snapshot; no analysis, file I/O, logging, or expression evaluation).
   - **Worker thread(s):** analysis, model rebuild, heavy transforms, file decode/encode.
   - **Handoff:** **immutable snapshot** or **double-buffered** model swap after analysis completes.

## Consequences

- The legacy JUCE source tree remains only as migration reference until the
  final widget-port deletion commit; it is not part of any production build.
- Future UI work should not entangle Qt with the RT synthesis path.

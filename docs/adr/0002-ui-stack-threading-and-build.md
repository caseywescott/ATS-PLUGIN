# ADR 0002: UI stack, threading, and build system

## Status

Accepted (Phase 0; implementation details may evolve)

## Context

The codebase must stay **real-time safe** on the audio thread while running **heavy ATS analysis** and optional **MathExpr** transforms. Qatsh is Qt-based; the PRD warns about Qt size and GPL coupling.

## Decision

1. **Build system:** **CMake** for the DSP/tooling core and tests (Phase 0). **JUCE** is the **preferred default** for the VST3 shell and editor UI in Phase 1+ unless profiling shows a blocker, at which point **iPlug2** or raw **VST3 SDK** become alternatives documented in a superseding ADR.
2. **Qt in the plugin editor:** **Avoid for MVP.** Prefer JUCE-native UI for the effect shell. Revisit Qt only if a hard requirement emerges to reuse large portions of Qatsh widgets (accept GPL and binary size tradeoffs).
3. **Threading contract:**
   - **Audio thread:** **synthesis only** (read a frozen/immutable model snapshot; no analysis, file I/O, logging, or expression evaluation).
   - **Worker thread(s):** analysis, model rebuild, heavy transforms, file decode/encode.
   - **Handoff:** **immutable snapshot** or **double-buffered** model swap after analysis completes.

## Consequences

- Phase 0 does not vendor JUCE yet; the offline CLI validates ATSA linkage only.
- Future UI work should not entangle Qt with the RT synthesis path.

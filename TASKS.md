# Task list: PRD → coherent commits

**Source:** [PRD.md](./PRD.md)  
**How to use:** Complete the checkbox items within a section, then make **one commit** with the suggested subject line. Adjust wording to your conventional-commit style; merge sections only when a change set truly belongs together.

---

## Conventions

- **One commit** = one reviewable story (buildable, tests green where applicable).
- **PRD refs** point to sections in `PRD.md` for traceability.
- Order respects dependencies (spike before plugin shell; façade before RT synthesis).

---

## Phase 0 — Repository, compliance, and spike

### Commit 1 — `chore: document licenses and third-party obligations`

**PRD:** §8, §2.2, §11.1  

- [x] Add `NOTICE` / `THIRD_PARTY.md` (or equivalent) listing ATSA, MathExpr, Qatsh-derived code, VST3 SDK terms, Steinberg compliance notes.
- [x] Record distribution assumption (e.g. GPL-compliant release vs re-license path).
- [x] No functional code required.

---

### Commit 2 — `chore: scaffold build system and vendor boundaries`

**PRD:** §6.1, §6.4  

- [x] Choose and document build tool (CMake + JUCE, or CMake-only, etc.).
- [x] Directory layout: `vendor/atsa`, `src/core`, `src/plugin`, `tools/` (names flexible).
- [x] Submodule or subtree policy for upstream `atsa` / qatsh reference (align with repo strategy).

---

### Commit 3 — `build: compile ATSA as static library with documented deps`

**PRD:** §6.1, §6.4, §11.5  

- [x] Build static (or clearly scoped shared) ATSA from vendored or submodule source.
- [x] Link libsndfile (or document alternative) for file I/O in tools/tests.
- [x] macOS + Windows targets compile in CI or locally; document compiler baselines.

---

### Commit 4 — `feat(tools): offline CLI analyze → synthesize → WAV`

**PRD:** §10 P0, §5.1, §5.2, §7  

- [x] CLI: input WAV path, output WAV path, sample rate flag or file SR.
- [x] Populate `ANARGS` from defaults / JSON or flags (parity path toward full UI later).
- [x] Run analysis off main thread not required for CLI; document CPU/memory use.
- [x] Run synthesis; write output via sndfile.

---

### Commit 5 — `test: golden/tolerance checks against reference renders`

**PRD:** §9  

- [x] Short fixture audio + expected output (or tolerance-based compare).
- [x] Document determinism expectations per PRD §5.1.

---

### Commit 6 — `docs(adr): architecture decisions (effect vs instrument, UI stack)`

**PRD:** §6.1, §6.2, §6.3, §3.3  

- [x] ADR: effect-first MVP vs instrument; live analysis in/out of scope.
- [x] ADR: JUCE vs iPlug2 vs raw VST3; Qt in editor yes/no.
- [x] Threading contract: audio thread = synthesis only.

---

## Phase 1 — MVP VST3

### Commit 7 — `feat(core): C++ façade for ANARGS, SPARAMS, and model lifecycle`

**PRD:** §5.1, §5.2, §6.1  

- [x] Types mapping to `atsa.h` (`ANARGS`, `SPARAMS`, model handles).
- [x] No Qt; no allocations on paths intended for audio thread (document which entry points are RT-safe).

---

### Commit 8 — `feat(core): thread-safe model handoff for processing`

**PRD:** §6.3, §7  

- [x] Immutable model snapshot or double-buffered swap after analysis completes.
- [x] Clear ownership: who frees model; no use-after-free on parameter change.

---

### Commit 9 — `feat(plugin): VST3 skeleton (buses, bypass, process stub)`

**PRD:** §3.2 item 5, §7  

- [x] Correct I/O bus layout for chosen effect design.
- [x] `process` passes silence or dry through until synthesis lands.
- [x] Release build: no logging/alloc on audio thread in hot path.

---

### Commit 10 — `feat(plugin): background analysis job with progress and cancel`

**PRD:** §5.1, §7  

- [x] Queue analysis on worker thread; UI/message thread updates progress.
- [x] Cancel without host crash; partial results discarded cleanly.

---

### Commit 11 — `feat(plugin): WAV import for analysis source`

**PRD:** §3.2 item 1, §6.4  

- [x] Host sample rate alignment or documented SRC policy (PRD §5.1).
- [x] Error handling for unsupported formats / empty files.

---

### Commit 12 — `feat(plugin): connect import → analysis → in-memory model`

**PRD:** §3.2 items 2–3  

- [x] Successful path stores model for synthesis step.
- [x] Mode state: “No model” → “Analyzed” (see Commit 14 for UI polish).

---

### Commit 13 — `feat(plugin): real-time synthesis from active model`

**PRD:** §3.2 item 4, §5.2, §7  

- [x] `process` reads current model snapshot; outputs synthesized audio (or dry/wet mix per product decision).
- [x] Denormal / silence-edge acceptance criteria (PRD §9).

---

### Commit 14 — `feat(plugin): expose MVP synthesis parameters to the host`

**PRD:** §3.2 item 6, §5.2  

- [x] Parameters: start, duration, partial mode (if MVP includes), use phases, partial/residual gains, freq/time factors (match `ATSSynthesis` semantics).
- [x] Smoothing / ramping policy documented to avoid zipper noise.

---

### Commit 15 — `feat(plugin): VST3 state save and restore`

**PRD:** §3.2 item 6, §5.4  

- [x] `getState` / `setState`: analysis params, synthesis params, model blob (or embedded checksum + path if using sidecar—prefer self-contained blob for MVP).
- [x] Version field in serialized state for forward compatibility.

---

### Commit 16 — `feat(ui): minimal editor — mode indicator and analysis controls`

**PRD:** §5.5, §3.2  

- [x] Visible states: No model / Analyzed / Synthesizing (or equivalent).
- [x] Trigger analyze, import, cancel; display rough CPU / partial count warnings (baseline for Commit 22).

---

### Commit 17 — `feat(plugin): latency / PDC reporting if synthesis adds delay`

**PRD:** §7  

- [x] `getLatencySamples` accurate when non-zero lookahead exists; else zero with doc.

---

### Commit 18 — `ci: matrix builds and plugin stress smoke`

**PRD:** §6.4, §9  

- [x] CI: Ubuntu (tools, `BUILD_PLUGIN=OFF`) + macOS (plugin + tools, `BUILD_PLUGIN=ON`). Windows x64 deferred.
- [ ] Optional pluginval or headless render step in CI.

---

## Phase 2 — UX and presets

### Commit 19 — `feat(ui): preset browser (factory and user presets)`

**PRD:** §5.4  

- [x] Factory presets ship with binary or resource bundle.
- [x] User preset directory; names and optional tags.

---

### Commit 20 — `feat(ui): read-only partial/residual spectrogram view`

**PRD:** §10 P2, §4  

- [x] Visualize model data without full editor parity.
- [x] Performance: heavy work off audio thread.

---

### Commit 21 — `feat(ui): analysis progress, cancel, and CPU guardrails`

**PRD:** §5.1, §5.5, §7  

- [x] Warnings when partial/frame counts exceed documented soft limits.
- [x] Cancel UX polished; host remains responsive.

---

### Commit 22 — `feat(a11y): keyboard focus, contrast, DPI-aware layout`

**PRD:** §5.5  

- [x] Tab order; minimum contrast; scaling under host DPI.

---

## Phase 3 — Transforms

### Commit 23 — `feat(transform): global partial/time/frequency transforms in UI`

**PRD:** §5.3 Phase 1–2 (incremental)  

- [x] Expose transforms already implied by synthesis where appropriate; add any missing global ops.

---

### Commit 24 — `feat(transform): partial table editing (parity with Qatsh workflows)`

**PRD:** §5.3 Phase 2  

- [x] Selection, frame-level edits; validate against model invariants.

---

### Commit 25 — `feat(transform): offline MathExpr batch with Qatsh variable semantics`

**PRD:** §5.3 Phase 3, §2.2  

- [x] Integrate or port MathExpr; run only off audio thread; user-triggered apply.
- [x] Document same variables/functions as Qatsh `README.txt`.

---

## Phase 4 — Interchange and additional platforms

### Commit 26 — `feat(io): optional ATS file load/save compatible with ecosystem`

**PRD:** §5.4  

- [x] Round-trip with Qatsh/ATSA where formats allow; tests for interchange.

---

### Commit 27 — `feat(plugin): AU (or CLAP) target if adopted`

**PRD:** §3.3, §10 P4  

- [x] Second format behind same core; shared serialization story.

---

### Commit 28 — `feat(plugin): instrument mode with MIDI (if ADR selects in scope)`

**PRD:** §6.2, §10 P4, §4 scenario 2  

- [x] MIDI note → pitch/time mapping for frozen model; latency documented.

---

## Phase 5 — Hardening and release

### Commit 29 — `docs: performance budgets and user-facing limitations`

**PRD:** §7, §11.3–11.4  

- [ ] Recommended max partials/frames at 48 kHz / 256 samples (reference HW).
- [ ] Live analysis stance; memory caps.

---

### Commit 30 — `release: signing, notarization (macOS), and packaging`

**PRD:** §6.4, §8  

- [ ] Signed binaries; notarization checklist; Windows installer or zip.
- [ ] Release notes with license pointers.

---

## Quick index: PRD section → first commit that addresses it

| PRD section | Start here |
|-------------|------------|
| §5.1 Analysis | 4 (CLI), then 10–12 |
| §5.2 Synthesis | 4 (CLI), then 13–14 |
| §5.3 Transforms | 23–25 |
| §5.4 Persistence | 15, 19, 26 |
| §5.5 UI/UX | 16, 20–22 |
| §6 Technical architecture | 2, 6, 7–9 |
| §7 Real-time / latency | 8, 9, 13, 17, 21, 29 |
| §8 Licensing | 1 (ongoing: 30) |
| §9 Quality / testing | 5, 18 |
| §10 Milestones P0–P4 | Commits 3–5 (P0), 7–18 (P1), 19–22 (P2), 23–25 (P3), 26–28 (P4) |

---

## Optional housekeeping commits (any time, keep small)

- `chore: git submodule add for qatsh and atsa` (if tracking upstream in-repo).
- `chore: .gitignore for build dirs and IDE files`.
- `style: apply formatter/linter once` (single mass-format commit to avoid noise in feature commits).

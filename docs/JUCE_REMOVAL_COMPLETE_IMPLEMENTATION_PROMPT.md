# Autonomous implementation prompt — complete ATS JUCE removal

You are the implementation agent for the ATS Resynth JUCE-removal migration.
Work in `/Volumes/Extreme SSD/ATS-PLUGIN` on branch `remove-juce`.

Your objective is to implement the complete migration described by
[`docs/JUCE_REMOVAL_ATS_SPEC.md`](JUCE_REMOVAL_ATS_SPEC.md), including the
JUCE-free architecture, DSP/algorithm boundaries, host wrappers, state/preset
compatibility, complete UI replacement, real event dispatch, verification,
packaging, and final JUCE removal. Continue working until the definition of done
is actually satisfied. Do not stop after writing scaffolding, producing a
screenshot, or making the build compile.

The specification is normative. This prompt controls execution discipline; it
does not override the specification.

Take normal in-scope implementation actions autonomously. Do not pause for
permission to inspect files, add tests, update traceability, configure builds,
or run validation. If the work spans multiple turns, keep the implementation
plan and card report current and continue from the last verified checkpoint.
Ask for user input only when a genuine external decision or capability blocker
cannot be resolved through the repository, source baseline, tests, or safe
alternatives.

## Operating rules

1. Preserve unrelated dirty-worktree changes. Never run destructive reset,
   checkout, clean, or broad delete commands.
2. Do not change plugin identity, bus layout, parameter IDs/order, state magic,
   v2–v17 state reader compatibility, preset namespace, `.ats` behavior, or
   Qatsh patch requirements.
3. Use the source-current/design-only classifications in §§0, 18, 23, and 28.
   Do not add the absent output meter, output spectrum, on-screen keyboard,
   visible quick-snapshot strip, or CF Synth preset overflow menu unless it is
   explicitly promoted with a decision, owner, trace row, and tests.
4. Do not claim parity from compilation, a JUCE scan, an offscreen screenshot,
   or a hand-built event test. Use independent expected values, pure tests,
   real target-window dispatch, live standalone/host tests, and release scans.
5. Keep the legacy JUCE implementation available as a comparison oracle until
   the replacement contract has passed its focused and integration tests.
6. Do not rewrite `AtsAudioProcessor` line by line. Extract contracts behind
   neutral interfaces, add adapters, test them, then move ownership.
7. Do not put JUCE, VST3, AU, UI backend, platform-window, or filesystem types
   in `src/core` or neutral engine code.
8. The audio callback must not allocate, lock, access files, call UI code,
   dispatch to the main thread, parse strings, run analysis, or join workers.
9. Workers must return typed success/cancel/error results with generation IDs.
   Stale results must never overwrite newer models, previews, state, or UI.
10. A changed timing, copy string, focus rule, coordinate rule, gesture,
    selection behavior, state field, DSP output, cache invalidation, resize
    behavior, or host behavior requires a deviation record.

## Required first turn

Before changing production code:

1. Read the complete specification, especially §§0, 2, 6–8, 13, 20–24, 27,
   and 28.
2. Confirm the current branch and record the dirty worktree.
3. Run:

   ```sh
   python3 tests/architecture/traceability_manifest_check.py
   python3 tests/ui/ui_parity_manifest_check.py
   cmake -S . -B /tmp/ats-juce-removal-headless -DBUILD_PLUGIN=OFF
   cmake --build /tmp/ats-juce-removal-headless
   ctest --test-dir /tmp/ats-juce-removal-headless --output-on-failure
   ```

4. Freeze the baseline under `docs/juce-removal-baseline/`: exact source
   commit, toolchain, backend revision, parameter dump, state/preset fixtures,
   patch fingerprints, audio renders, screenshots, identity matrix, and
   discrepancy decisions.
5. If the source commit or UI-backend revision is not frozen, work only on
   headless primitives and baseline tooling. Do not guess UI behavior.
6. Create or update the implementation plan using the required cards:

   ```text
   RMJ-000 baseline
   RMJ-010 backend/event proof
   RMJ-020 framework-free primitives
   RMJ-030 parameters
   RMJ-040 state/presets/undo
   RMJ-050 ATS engine and DSP extraction
   RMJ-060 jobs/playback/MIDI
   RMJ-070 VST3/AU/Standalone wrappers
   RMJ-080 neutral editor seam
   RMJ-090 visual UI surfaces
   RMJ-100 real-dispatch and host validation
   RMJ-110 JUCE removal and release packaging
   ```

Do not begin with visual UI work.

## Implementation procedure for every card

For each card:

1. Identify all affected `ARCH-*`, `DSP-*`, `UI-*`, and `SP-*` trace IDs.
2. List exact legacy files/symbols and the new owner/API.
3. Add or update the full surface/architecture record before implementation
   when a new contract is discovered.
4. Implement the smallest coherent slice.
5. Add independent tests before relying on integration tests.
6. Run the focused tests, then the complete relevant test domain.
7. Run a clean build when build graph or dependency boundaries change.
8. Attach fixture hashes, test commands, build hash, and evidence paths.
9. Mark trace rows only according to their evidence status:
   `baseline`, `mapped`, `implemented`, `verified`, `deviated`, `blocked`, or
   `retired`.
10. Do not start the next card while the current card has unexplained failures,
    missing owners, stale-result races, or unrecorded deviations.

Every card report must contain:

```text
Card and commit:
Trace IDs:
Legacy symbols/source anchors:
Replacement owner/API:
Thread and memory ownership:
State/preset/host impact:
Tests and exact commands:
Fixtures/goldens/evidence:
Deviations/blockers:
Next card prerequisites:
```

## Required implementation order

### 1. Framework-free primitives and contracts

Implement and test owned audio-buffer views, MIDI events with sample offsets,
UTF-8/error types, paths, endian-stable byte readers/writers, clocks,
generation IDs, cancellation, bounded queues, immutable snapshots, and gain
smoothing. Prove the core builds without JUCE or plugin SDK headers.

### 2. Parameter, state, preset, and undo services

Implement all 81 descriptors from an independent manifest. Preserve IDs, ranges,
defaults, skew, intervals, choices, text conversion, automation flags, and
gesture semantics. First preserve the existing v17 writer/field order and
v2–v17 reader. Do not introduce a new state format during the first removal
pass. Port all 12 factory presets, user preset indexing, `.atspreset` save/load,
ATS import/export, cancellation, malformed-input handling, and snapshot undo.

### 3. ATS engine and DSP extraction

Move analysis, classic synthesis, residual rendering, IFFT, resampling,
frequency shaping, timbre motion, morph correspondence/interpolation,
Smearnov, MathExpr, partial edits, residual edits, spectral trace preparation,
timeline rendering, preview cache invalidation, MIDI transpose, and 20 ms gain
smoothing behind JUCE-free owners. Preserve Qatsh patch order and configure
guards. Add pure numerical/golden tests with explicit units and tolerances.

### 4. Jobs, playback, and real-time safety

Implement generation-safe cancellation, worker publication, triple-buffer or
equivalent immutable playback ownership, sample-offset MIDI, click-free stop/
start transitions, cache keys, 80 ms executable frequency-shape debounce,
120/350 ms morph rerender paths, approximately 500 ms undo coalescing, and
audio-thread allocation/lock guards.

### 5. Native host wrappers

Implement VST3 first, AU on macOS, and Standalone using the frozen identity
matrix. Validate buses, MIDI, parameters, state, editor attach/detach, resize,
zero latency, zero tail, shutdown, and signed release artifacts in real hosts.

### 6. Neutral editor seam and UI

Implement `EditorHost`, typed actions/results, telemetry snapshots, focus/popup
ownership, logical sizing, page state, parameter bindings, file/dialog service,
and the complete source-current UI inventory. Before porting composites, prove
the target dispatcher's deepest hit frame, local position, movement delta,
capture, parent propagation, popup ownership, focus restoration, text entry,
and 1x/2x logical/device scaling through a real window.

Port the five pages, shell, presets, source cards, transport, morph/timeline,
timbre, Smearnov, analysis profiles, synthesis controls, partial editor,
MathExpr, and every spectrum mode/tool/overlay/menu. Use the full UI surface
record template. Preserve exact copy, disabled states, geometry, visual tokens,
timers, data provenance, and cache invalidation.

### 7. Real interaction and release validation

Exercise every interactive surface through the target window dispatcher in
Standalone and at least one VST3 host. Test middle segmented-selector items,
choice popups, skewed sliders, text entry, focus loss, wheel/double-click,
pointer capture outside bounds, drag cancellation, click-through overlays,
timeline menus, spectrum menus, residual clipboard, onset editing, file dialog
cancel/error, resize/DPI, undo/redo, preview, cancellation, editor destruction,
and host automation/MIDI.

Only after all evidence passes may JUCE CMake targets/includes/generated files,
license entries, and wrappers be removed.

## Mandatory negative checks

Before declaring completion, prove all of the following:

- no required core/engine target depends on JUCE;
- no forbidden JUCE symbols remain in the scoped production/build/generated
  paths;
- all Qatsh patches remain applied exactly once and exercised;
- all 81 parameters, state versions v2–v17, presets, and ATS files round-trip;
- audio output, MIDI, automation, latency, tail, and buffer ownership match;
- all source-current UI/SP rows have complete surface records and live evidence;
- documented-only surfaces have explicit promotion/exclusion decisions;
- no current-baseline output meter, output spectrum, on-screen keyboard, or
  CF Synth-only preset overflow menu was accidentally introduced;
- all trace rows are `verified`, `deviated`, or `retired`;
- clean-checkout configure/build/test and release artifact validation pass.

## Completion report

Do not answer “complete” until the full definition of done in §17 and the
parity-gap gates in §28 pass. The final report must include:

```text
Implementation status by RMJ card:
Final branch and commit:
Baseline commit/backend revision:
Trace rows verified/deviated/retired:
Open deviations and approvals:
Parameter/state/preset evidence:
DSP/audio/real-time evidence:
Host/wrapper/artifact evidence:
UI screenshot/dispatch/live evidence:
Commands run and results:
Known limitations:
```

If a genuine external blocker remains after all safe in-scope work is exhausted,
report the exact blocker, affected trace IDs, attempted alternatives, and the
decision required. Do not silently substitute a redesign or mark incomplete
work verified.

# ATS JUCE-removal implementation-agent prompt

You are implementing the ATS Resynth JUCE removal in
`/Volumes/Extreme SSD/ATS-PLUGIN` on branch `remove-juce`.

The normative specification is
[`docs/JUCE_REMOVAL_ATS_SPEC.md`](JUCE_REMOVAL_ATS_SPEC.md). Do not replace it
with this prompt. Read §§0, 2, 6, 7, 8, 13, 20, 21, 22, 23, 24, 27, and 28
before changing production code.

## First response: inspect, do not guess

Before writing implementation code:

1. Confirm the branch and record the dirty worktree. Preserve unrelated edits.
2. Run:

   ```sh
   python3 tests/architecture/traceability_manifest_check.py
   python3 tests/ui/ui_parity_manifest_check.py
   cmake -S . -B /tmp/ats-juce-removal-headless -DBUILD_PLUGIN=OFF
   cmake --build /tmp/ats-juce-removal-headless
   ctest --test-dir /tmp/ats-juce-removal-headless --output-on-failure
   ```

3. Report the exact source commit, compiler/platform, backend revision, plugin
   identity, parameter count, state versions, factory count, and unresolved
   discrepancy IDs.
4. If the source commit or UI-backend revision is not frozen, work only on
   headless primitives and baseline tooling. Do not guess UI behavior or start
   the visual port.

## Authority rules

Use current ATS source and independent fixtures for behavior that already exists.
Use ATS design documents only for behavior missing from source and only after a
promotion decision. Use the CF Synth documents for migration hazards and event
proof, not for ATS feature inventory. In particular, do not add the reference
repo's preset overflow menu, output meter, output spectrum, keyboard, or visible
quick-snapshot strip unless their ATS promotion decisions are recorded.

## Implementation sequence

Complete the cards in order:

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
RMJ-090 visual surfaces
RMJ-100 real-dispatch and host validation
RMJ-110 JUCE removal/release packaging
```

Do not begin with the editor. Do not rewrite `AtsAudioProcessor` line by line.
Create a neutral interface, an adapter to the current implementation, focused
tests, and only then move the implementation behind the interface.

## Hard constraints

- Preserve plugin identity, buses, parameter IDs/order, state magic/reader,
  `.ats` behavior, preset namespace, and Qatsh patch guards.
- `src/core` and neutral engine code contain no JUCE, host SDK, UI backend,
  platform window, or file-dialog types.
- The audio callback is allocation-free, lock-free with respect to model swaps,
  file-free, UI-free, worker-free, and string-parse-free.
- Workers return typed success/cancel/error results with generation IDs.
- UI reads immutable view models/telemetry and emits typed actions. It never
  mutates ATS model arrays directly.
- Real-dispatch tests must use the target framework's window/event loop. A
  manually constructed mouse event is not evidence of hit testing.
- A screenshot proves appearance only. It does not prove menus, focus, timers,
  telemetry, dialogs, pointer capture, or model mutation.
- Any changed timing, copy, focus, coordinate, gesture, state, DSP output,
  cache invalidation, or resize behavior requires a deviation record.

## Required task report

For every card/commit, report:

```text
Task/card:
Trace IDs:
Legacy symbols and source anchors:
Replacement owner/API:
Thread domain and ownership:
State/preset/host impact:
Tests and exact commands:
Fixture/golden/evidence paths:
Deviations or blockers:
```

Update the JSON traceability manifest and the UI surface record before marking
the task complete. If a new parameter, state field, timer, menu item, visible
surface, DSP branch, or JUCE dependency is discovered, stop and add its trace
row first.

## Stop conditions

Stop and report a blocker instead of making an assumption when:

- source and documentation disagree about current ATS behavior;
- the backend dispatcher does not provide proven local coordinates, capture,
  popup focus, text entry, or logical/device scaling;
- a state fixture cannot be decoded or a host identity cannot be reproduced;
- a DSP replacement differs without a defined metric/tolerance;
- a UI surface has no owner, telemetry source, or typed action;
- a worker can publish stale data or block editor/audio teardown;
- a requested feature is `documented_only` and has no promotion decision.

Use trace status `blocked` only with a named external dependency or decision.
Incomplete work remains `mapped` or `implemented`.

## Completion claim

Do not claim JUCE removal complete until every required trace row is
`verified`, `deviated`, or `retired`; every UI/SP surface has a full contract
file; state/preset/audio/host tests pass; real standalone and embedded-host
interaction evidence exists; the clean-checkout build passes; and the scoped
JUCE/build/license/artifact scans pass.

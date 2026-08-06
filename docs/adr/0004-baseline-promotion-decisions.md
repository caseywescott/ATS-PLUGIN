# ADR 0004: Baseline promotion and deferral decisions (card 03)

## Status

Accepted — 2026-08-06

## Context

The ATS JUCE-removal spec (§4, §18, §27) requires every surface in the legacy
editor to be explicitly classified as one of:

- `source_current` — port faithfully from existing source
- `promoted_target` — promoted beyond the legacy baseline
- `documented_only` — described in the spec but excluded from this parity cycle
- `design_reference` — geometry or layout change relative to legacy source

Five surfaces have been flagged as requiring explicit decision before UI work
begins. Leaving them in `planned` state would allow silent scope creep or
silent omission.

## Decisions

### 1. Editor geometry

**Decision: promote 1280×940 as the new product target.**

The legacy source (`PluginEditor.cpp`) constructs 1180×820 with limits
960×680–1600×1200. The UI spec states 1280×940 with 1100×860–1900×1400.
Since the editor is being rebuilt from scratch in Visage, adopting the
designed geometry is the right moment to close the discrepancy. The legacy
1180×820 construction will not be ported.

Discrepancy: D5-GEOMETRY (resolved — promote).

### 2. On-screen keyboard

**Decision: defer to P1 (documented-only for this removal cycle).**

The on-screen keyboard is described in `ats-mearcstapa-ui-spec.md` but has no
implementation in the current `PluginEditor.h` (no keyboard member). Adding it
in the same commit chain that removes JUCE would mix scope. If promoted later,
it must use the monophonic-transpose semantics documented in §27 and route
through `NoteAction` with the same MIDI semantics.

Parity manifest record: `UI-KEYBOARD-01`, `baseline_scope: documented_only`.
Deviation: D-UI-KEYBOARD.

### 3. Output spectrum

**Decision: defer to P1 (documented-only for this removal cycle).**

The Synthesis-page output spectrum is described in the UI spec but is absent
from the current `SpectrogramPanel`/`SynthTabPanel` source. Building it
requires an FFT path on the render-output bus that is not yet wired. Including
it now would block the removal gate. If promoted in a future cycle, it must
use the Accelerate FFT path documented in spec §4.

Parity manifest record: `UI-OUTPUT-01`, `baseline_scope: documented_only`.
Deviation: D-OUTPUT-SPECTRUM.

### 4. Output meter

**Decision: defer to P1 (documented-only for this removal cycle).**

The output meter and peak-hold telemetry path exist conceptually in
`MeterPublisher.h`/`PlayheadPublisher.h` but no live meter widget exists in
the current editor. Adding a live meter widget is independent of the JUCE
removal parity gate and will not be ported in this cycle.

Parity manifest record: `UI-METER-01`, `baseline_scope: documented_only`.
Deviation: D-OUTPUT-METER.

### 5. `uiTheme` parameter

**Decision: keep as editor/session UI state; do not add a versioned parameter.**

The current parameter registry has no `uiTheme` descriptor. Adding one would
require a compatibility decision about a new parameter ID in the 81-parameter
contract and a new state version. This is out of scope for the removal parity
cycle. The theme is persisted in the editor session layer if implemented.

Parity manifest record: `UI-SHELL-03` (`theme-combo/editor`). No deviation
required — the surface port can proceed as editor-only state without a
parameter binding.

### 6. CLAP, noisc, and CF-Synth-only preset actions

**Decision: exclude from removal parity scope.**

CLAP is not a current production format for this plugin. The noisc node editor
is described as a "documented future expansion" (spec §19). CF-Synth-only
preset actions have no source implementation. All three are out of scope for
this removal cycle and are not required by the parity gate.

## Consequences

- The native UI rebuild targets 1280×940 (1100×860–1900×1400 resize range).
- Three `documented_only` surfaces remain deferred; no silent omission.
- `UI-SHELL-03` proceeds without a parameter binding; theme is editor state.
- All six decisions are recorded and closed; no open promote/defer questions
  remain for the card 03 gate.

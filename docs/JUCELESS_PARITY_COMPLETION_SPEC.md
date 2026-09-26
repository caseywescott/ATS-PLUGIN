# ATS Resynth JUCE-less parity review and completion specification

**Review date:** 2026-08-22  
**Legacy oracle:** `ui-refactor` at `36244a3ff041491d9a8cc4f5bf641105b52c8e35`  
**JUCE-less implementation reviewed:** `remove-juce` at `a8b9357` plus the
current dirty working tree  
**Purpose:** define the remaining work required for behavioral parity with the
last JUCE implementation. This document supersedes the completion claims in
the 2026-08-05 audit where current source evidence disagrees with them.

## 1. Executive verdict

The framework-removal foundation is credible, but the product is not yet at
parity. The neutral parameter registry, DSP engine, native wrapper skeletons,
job scheduling, three-layer playback, state codec, and pure UI models are
substantial and testable. The main remaining problem is integration: several
replacement components are proven only in isolation and are not the components
used by the shipped editor or by host state/telemetry paths.

Parity must not be claimed until all P0 work below is complete. In particular,
a passing renderer test, widget test, manifest validator, or host artifact test
does not prove the production editor-to-host-to-engine path.

Current ledger state also reflects this incompleteness:

| Ledger | Status at review |
|---|---|
| Architecture/DSP | 30 `implemented`, 8 `deviated`, 1 `verified` |
| UI surfaces | 38 `implemented`, 12 `verified`, 3 deliberately `blocked` |

The three blocked documented-only surfaces—on-screen keyboard, output spectrum,
and output meter—remain valid P1 deferrals under ADR 0004 and do not block
legacy-source parity.

Review verification performed on the dirty working tree:

```text
python3 tests/architecture/traceability_manifest_check.py  # 39 valid records
python3 tests/ui/ui_parity_manifest_check.py                # 53 valid records
cmake --build build-review-ui -j4
ctest --test-dir build-review-ui --output-on-failure        # 84/84 passed
```

The green suite confirms that the configured unit/component contracts build and
pass. It does not negate the production wiring gaps identified below; several
of those gaps exist precisely because an isolated component is tested while a
different component is instantiated by the artifact.

## 2. What is already suitable to retain

- The 81-parameter registry and stable numeric host IDs.
- The framework-free ATS/Qatsh engine and the current DSP service boundaries.
- Native VST3, AU, and standalone target structure and pinned SDK revisions.
- The `LegacyStateCodec` version reader, extended v18 writer, and bounded byte
  primitives, subject to the production integration fixes below.
- Owned job schedulers, generation/cancellation primitives, immutable playback
  publication, and per-sample gain smoothing.
- `VisageWidgets.h`, `AtsEditorPages.h`, `SpectrumRenderer.h`, and the headless
  view models as implementation material. They are not production parity
  evidence until mounted and exercised through the real editor.
- The real-dispatch harness as a useful harness, after it is redirected from
  probe-only coordinates to production widgets and cross-layer assertions.

## 3. Release-blocking findings

### P0-1 — The shipped editor does not use the ported widget/page tree

`VisageEditor::Impl` constructs `ProbeFrame`. The five page classes in
`AtsEditorPages.h` and the reusable controls in `VisageWidgets.h` have no
production consumer. They are referenced by isolated widget tests and
documentation, not by `VisageEditor`.

`ProbeFrame` paints controls directly and routes events through hard-coded
coordinate ranges. This preserves the original test probe but not the legacy
component hierarchy, layout behavior, focus model, tooltips, or control
semantics. The UI surface records that describe `AtsPageContainer`,
`AtsSlider`, `AtsCombo`, `AtsSegmented`, and the page frames therefore overstate
the shipped implementation.

**Required outcome**

- Replace `ProbeFrame` as the production root with an explicit editor shell
  containing the top bar, `AtsPageContainer`, and five mounted page frames.
- Keep any coordinate probe in test-only source; it must not be the artifact's
  editor implementation.
- Give every visible control its own local bounds and event owner. Parent
  frames may coordinate layout, but must not infer unrelated child actions
  from global `x/y` thresholds.
- Implement the promoted 1280×940 design geometry and enforce ADR 0004's
  1100×860–1900×1400 limits. Verify narrow, tall, wide, 1×, and 2× layouts.

**Acceptance evidence**

- A production-root ownership test fails if `ProbeFrame` is instantiated by
  `VisageEditor::Impl` or if `AtsEditorPages.h` has no production reference.
- Native event tests click the actual child frame bounds after resize and DPI
  changes; they do not use a separate probe menu or magic global coordinates.
- Screenshots and an accessibility/control-tree dump cover all five pages.

### P0-2 — Displayed parameter and session state is disconnected from the host

`ProbeFrame` initializes its analysis, synthesis, and session arrays from
descriptor defaults. `VisageEditor` exposes no parameter-snapshot update API.
The VST3/AU/standalone refresh loops send only `TelemetrySnapshot`, and that
snapshot does not contain the 81 parameter values. Consequently host
automation, project restore, factory/user preset load, undo/redo, and changes
made by another editor do not update the displayed controls.

Session cards are similarly disconnected. `setTelemetry()` updates worker and
"has source" booleans but never calls `SessionPageModel::setSource`,
`setPreset`, `setPlaying`, `setPlayhead`, `setBandCount`, `setTimeline`, or
`setTimbre`. The source cards therefore continue to display their empty/default
state after real imports and analysis.

**Required outcome**

- Define one immutable `EditorSnapshot` containing a revision, all displayable
  parameter values, host-normalized values where needed, source-card metadata,
  transport state, preset state, history labels/enablement, timeline, timbre,
  Smearnov overlay, selection state, progress/error data, and spectrum revision.
- Publish it from `HostAdapter`; project it through every wrapper without
  reinterpreting product semantics in the wrapper.
- Make widgets read the latest snapshot and emit actions only. Local optimistic
  values may exist during a gesture but must reconcile on the next accepted or
  rejected snapshot.
- Notify the editor when automation arrives and when state/preset/history
  operations replace values. VST3 `setComponentState` must consume state and
  update controller parameter values rather than returning success without
  reading the stream.

**Acceptance evidence**

- For all 81 parameters: host change → visible value; UI gesture → host
  begin/perform/end → processor value → visible value; state restore and preset
  load → visible values.
- Source import/analysis updates name, duration, partial count, residual badge,
  availability, progress, and errors in the production source cards.
- Reopening a second editor produces the same visible state without touching a
  control first.

### P0-3 — The production spectrum has no data publication path

`VisageEditor::setSpectrumSnapshot` exists, but no VST3, AU, standalone, or
`HostAdapter` production path calls it. Only `setTelemetry` is pumped by the
wrappers. The production editor therefore renders its default empty
`SpectralTraceSnapshot`, even though `SpectralTraceBuilder` and the seven-mode
renderer pass isolated tests.

**Required outcome**

- Build spectral snapshots from immutable current/A/B model snapshots on the
  worker/UI-publication side, never by reading mutable ATS arrays in paint.
- Publish source, mode, partial traces, residual columns, envelopes, onsets,
  selections, playhead, and relevant revisions through the shared editor
  snapshot or a paired revisioned spectrum publisher.
- Wire the same publication path through VST3, AU, and standalone.
- Invalidate only on source/model/view/selection/overlay revisions; playhead
  updates must not rebuild static geometry.

**Acceptance evidence**

- Import/analyze a deterministic fixture through each wrapper, open the real
  editor, and assert non-empty draw commands in all applicable seven modes.
- Current/A/B menu selection changes both the host-owned view state and the
  rendered data source.
- Empty, sparse, dense, residual-less, and phase-less models render safely.

### P0-4 — Project state and full-state presets still lose session content

`HostAdapter::captureState()` fills parameter fields plus the simplified
timeline/timbre/onset fields, but leaves these `LegacyState` fields at defaults:

- `atsModel` (the current analyzed model),
- `morphRecipe`,
- all three `variations`,
- both `morphSourcePaths`,
- both `residualStates`, and
- both `morphSoundData` embedded source models.

`applyLegacyStateToParameters()` likewise restores parameters and simplified
UI data only; it does not restore current/source models, source paths,
variations, residual diagnostics, or the morph recipe. This is not parity with
the v17 JUCE writer, which embedded the current model and both morph-source ATS
blobs and persisted recipe, variations, paths, and diagnostics.

There is a second correctness issue: `restoreState()` validates only the
8-byte ATSP header, while `applyStateChunk()` accepts and stores a header-valid
chunk even when full `LegacyStateCodec::decode()` fails. A truncated state can
therefore be reported as successfully restored.

**Required outcome**

- Add an in-memory ATS serializer/deserializer or a scoped temporary-file
  adapter outside real-time code and populate all legacy v17 fields exactly.
- Restore into a temporary aggregate first. Validate the complete payload,
  reconstruct every model/recipe/source/variation/diagnostic object, and only
  then atomically install it.
- Preserve the loaded source-current ATSP version where required, while writing
  v18 for new state. Reject malformed or truncated chunks without any mutation.
- Make DAW state and `.atspreset` files use the same full-state capture/apply
  path. The VST3 controller's preset-load path must not fall back to updating
  only its local parameter table when the processor loads full state.

**Acceptance evidence**

- Independent binary fixtures for every readable version v2–v17 plus v18.
- Round-trip assertions cover current model samples/partials/residual, A/B
  models, paths, recipe, three variations, diagnostics, all 81 parameters,
  timeline, timbre plan, and onset edits.
- For every truncation byte and corrupted length/count field: restore returns
  failure and a before/after snapshot comparison is identical.
- DAW close/reopen and preset save/load produce audible and visual equivalence
  without re-analysis.

### P0-4a — "Current" and morph-timeline source identities are conflated

The legacy processor owned three distinct model roles: the current analyzed
model plus morph Source A and Source B. `HostAdapter` owns only a two-element
`sources_` array. `AnalyzeCurrentAction` queues slot 0, the same slot used for
Source A. Conversely, `SessionPageModel::sourceIndex(SessionSource::current)`
returns 2, while `HostAdapter` interprets preview source 2 as
`renderMorphForPreview()`. The label "Current" can therefore select the morph
timeline, and analyzing current can replace Source A.

**Required outcome**

- Introduce a typed source identity (`current`, `sourceA`, `sourceB`,
  `morphTimeline`) at every UI, host, state, telemetry, spectrum, and wrapper
  boundary. Do not encode distinct meanings as overlapping integer slots.
- Give the current model and A/B sources independent storage, revisions,
  cancellation generations, paths, diagnostics, embedded state, and playback
  caches.
- Make Analyze Current, Analyze Source A/B, Use Current as A/B, import,
  preview, export, spectrum source, Smearnov, MathExpr, and partial/residual
  editing resolve the intended typed source explicitly.

**Acceptance evidence**

- Load three distinguishable fixtures, run every source-targeted action, and
  assert that only the intended model/revision changes.
- Preview Current, A, B, and Timeline produces four distinguishable expected
  outputs. Saving/reopening preserves all three stored models and the timeline.
- Wrapper serialization rejects unknown source ordinals rather than silently
  remapping them.

### P0-5 — Undo/redo is not connected to mutations

`HostAdapter` owns an `UndoHistory` and invokes it for `HistoryAction`, but no
production mutation calls `history_.begin()` or `history_.commit()`. Parameter
gestures, partial/residual edits, MathExpr, timeline, timbre, Smearnov, factory
preset, and preset load therefore create no undo entries. The pure
`undo_history_test` proves the container, not the plugin integration.

Even after transactions are added, the current incomplete state capture cannot
undo model edits losslessly.

**Required outcome**

- Complete P0-4 first, then define transaction boundaries for every mutating
  action.
- Coalesce parameter drag updates into one gesture transaction and rapid
  timeline/timbre edits according to the legacy ~500 ms behavior.
- Store model mutations as immutable snapshots or bounded deltas; preserve the
  256 MB / minimum-eight-entry policy.
- Publish `canUndo`, `canRedo`, and exact action labels after every transaction,
  restore, source replacement, and history action.

**Acceptance evidence**

- Cross-layer tests perform each mutation through the real editor or wrapper,
  then assert undo and redo in parameters, model bytes, audio output, and
  visible UI.
- Loading/restoring a session clears prior history. Failed/cancelled/stale work
  creates no entry. One drag creates exactly one entry.

### P0-6 — Spectrum editing is a reduced approximation of the legacy editor

The legacy `SpectrogramPanel` supported lasso selection, partial amplitude and
frequency drawing, harmonic selection, mute/delete, solo, freeze, prune, tilt,
residual range selection and drawing, dB entry, smoothing, tilt, copy/paste,
swap, redistribution, envelope range gain/smooth, onset handles and
double-click editing, scrollbars, pinch zoom, hover readouts, and context menus.

The production JUCE-less frame currently treats a spectrum drag as pan. A click
selects one inferred partial or writes one inferred residual cell. Some richer
operations exist only as pure `SpectrumInteractionModel` methods. There is no
production double-click onset route, lasso, editable handles, toolbar/context
menu, text dB editor, partial frequency stroke, envelope box, or selection
overlay matching the old behavior. `PartialEditAction{apply=false}` is treated
by `HostAdapter` as selection-only, so the advertised Delete path does not
delete/mute a partial.

**Required outcome**

- Port the legacy interaction state machine explicitly. Separate navigate,
  select, draw, scale, and handle-drag modes; do not overload every drag as pan.
- Expand the action protocol where the existing cell-only payload cannot
  express selection ranges, partial mute/restore/delete, frequency strokes,
  freeze/solo/prune/tilt, or envelope range edits.
- Route every operation through a real production child frame and one
  host-owned mutation service with undo/invalidation.
- Preserve mode-specific enablement, focus, keyboard shortcuts, error copy,
  and selection visuals.

**Acceptance evidence**

- A legacy-to-new interaction matrix has one live-dispatch test and one
  model/audio assertion per operation.
- Delete/mute changes rendered audio and undo restores it.
- Onset double-click and handle drags persist full onset metadata, not merely
  normalized marker times.

### P0-7 — Several complex UI payloads are simplified or truncated

The production timeline click replaces the curve with three evenly spaced
values. It does not provide arbitrary time/value points, point kinds
(Morph/Hybrid/Envelope Morph), curve shapes, endpoint protection, add/remove,
or point dragging. The production timbre editor emits a three-value normalized
curve rather than the complete `TimbreMotionPlan`. Onsets are persisted as
normalized times and reconstructed approximately from a fresh detection run.

The VST3 message bridge compounds this: it sends only
`SetMorphTimelineAction::normalizedPoints` and
`SetTimbreMotionPlanAction::normalizedPlan`, dropping typed timeline points and
serialized timbre JSON even though the action protocol carries them.

**Required outcome**

- Make the full typed payload the canonical representation. Remove the simple
  fallback from production emission; retain it only for explicit backwards
  compatibility at a decoding boundary.
- Define bounded, versioned wrapper messages for typed timeline points, timbre
  JSON/plan data, and full onset records. Reject oversize/malformed payloads.
- Implement all legacy timeline, timbre, and onset editing gestures in the
  mounted page/spectrum components.

**Acceptance evidence**

- Identical actions sent through direct/AU, VST3 message, and standalone paths
  produce byte-identical captured state and equivalent audio.
- Round trips preserve point time, amount, kind, curve, timbre target/mode/
  depth/smoothing, and onset frame/peak/pre-margin/strength fields.

### P0-8 — Evidence status is ahead of production evidence

The manifest validators validate schema and allowed status vocabulary; they do
not establish parity. Several UI records call widgets or page frames the owner
even though those types are not mounted. Spectrum records marked `verified`
rely on pure renderer/model tests while the production snapshot feed is absent.
Morph fixture assets and metric pairings remain explicitly marked
`placeholder`.

The two existing build directories also demonstrate why build provenance must
be part of evidence: `build-review` passes its 80 configured headless tests,
while `build-review-ui` initially contained only four built UI executables and
reported the remaining registered tests as missing until a full rebuild.

**Required outcome**

- Demote any row whose named production owner is not instantiated or whose
  evidence stops before the wrapper/editor/engine boundary.
- Add source/replacement commit, dirty-tree hash, CMake cache, architecture,
  SDK revisions, and exact command to every evidence report.
- Replace placeholder fixtures with committed redistributable assets and
  approved tolerances.
- Require a clean configure and full build before CTest; do not run CTest from
  a partially built directory as release evidence.

## 4. P1 parity and hardening work

These follow P0 unless a P0 implementation naturally includes them.

1. Persist the editor theme/session UI state as ADR 0004 promises, without
   adding an 82nd automatable parameter.
2. Restore exact legacy tooltip/help/status copy, focus order, keyboard
   behavior, disabled-state semantics, and error visibility.
3. Verify factory preset names, descriptions, and all values against the
   legacy recipes—not only index validity.
4. Verify `analysisType` profile values stay in descriptor range. Profile code
   must use descriptor choice ordinals and tests must catch out-of-range writes.
5. Add resize-limit enforcement and host negotiation tests for minimum,
   preferred, and maximum sizes.
6. Exercise arm64 native editor creation in a window-capable environment and
   complete standalone, AU, VST3, validator/pluginval-equivalent, and at least
   one DAW lifecycle/stress run.
7. Close release hygiene: AppleDouble files, stale build artifacts, license and
   dependency docs, clean-checkout build, signing/notarization checklist.
8. Reconsider the keyboard/output-spectrum/output-meter deferrals only after
   legacy-source parity is complete; they are feature work, not removal gates.

## 5. Ordered implementation plan

### Phase A — establish truthful production ownership

1. Add failing production-ownership tests for the mounted root, pages, widgets,
   spectrum publisher, and bidirectional parameter projection.
2. Build `EditorSnapshot` and wrapper-neutral publication APIs.
3. Replace production `ProbeFrame` with the mounted shell/page tree.
4. Move the probe-only menu and coordinate callbacks to test support or delete
   them once equivalent production-path tests exist.
5. Demote affected manifest rows until their new cross-layer tests pass.

### Phase B — restore state, preset, and history parity

6. Implement complete in-memory capture of current/A/B ATS data, recipe,
   variations, paths, diagnostics, parameters, timeline, timbre, and onsets.
7. Separate Current, Source A, Source B, and morph-timeline identities and
   migrate every action, telemetry, state, and wrapper call site to a typed enum.
8. Implement fully validated, transactional restore for v2–v18.
9. Route DAW state and full-state presets through that shared implementation in
   VST3, AU, and standalone.
10. Add mutation transactions and integrate `UndoHistory` with every action.
11. Add binary compatibility, corruption, preset, reopen, and undo fixtures.

### Phase C — complete UI behavior

12. Bind all 81 production widgets to the editor snapshot/action protocol and
    prove automation/state reflection.
13. Port source cards, transport, variations, full timeline, morph policy/band
    inspector, timbre motion, Smearnov disclosures, Analysis, Synthesis,
    Partials, and MathExpr behavior into mounted frames.
14. Feed real spectrum snapshots and port the full legacy spectrum interaction
    state machine.
15. Version complex VST3 messages so typed timeline, timbre, onset, spectrum,
    and edit payloads survive the wrapper boundary.
16. Add dialogs, drag/drop, popup, text-entry, focus-restoration, tooltip, and
    teardown tests on production components.

### Phase D — differential and release validation

17. Build the legacy and JUCE-less artifacts from the two pinned commits with
    the same fixtures, sample rates, block sizes, and architecture.
18. Run the differential matrix below and approve only documented tolerances.
19. Run native live-dispatch and screenshots for every page/theme/mode/state.
20. Run host validation and DAW reopen/automation/MIDI/editor stress.
21. Promote manifest rows only from evidence generated by those exact builds;
    publish one final parity packet.

## 6. Required differential matrix

| Contract | Minimum comparison |
|---|---|
| Parameters | 81 IDs/order/numeric IDs/ranges/defaults/skews/steps/choices/text/automation flags |
| State | v2–v18 decode; legacy v17 → new restore; new save → new restore; malformed corpus |
| Presets | 12 factory recipes; full-state user preset with embedded current/A/B models |
| Analysis | types 1–4 at 44.1/48/88.2/96 kHz; cancellation and invalid input |
| Synthesis | all flags, start/duration, time/frequency factors, shape modes, IFFT, phase, residual-only |
| Playback | current/A/B/timeline × Both/Partials/Residual/Smear; gains/trim smoothing; MIDI offsets/transpose |
| Morph | grid/correspondence/quality/policies/domains/phase/transients/lanes/timeline/timbre |
| Editing | partial, residual, spectrum, envelope, MathExpr, Smearnov, undo/redo |
| UI | five pages, two themes, seven spectrum modes, empty/data/dense/selection/error/progress states |
| Hosts | VST3/AU/standalone identity, buses, automation, state, editor attach/resize/scale/hide/show/close |

Audio comparison must use deterministic inputs and report peak error, RMS
error, correlation, duration, NaN/Inf count, and checksum. Exact equality is
required for serialized contracts and metadata; audio tolerances must be
approved per algorithm rather than chosen after observing a failure.

## 7. Definition of done

The JUCE-less implementation has parity only when all of the following are
true:

- The artifact mounts the real page/widget tree; no production probe frame or
  global coordinate router remains.
- Host automation, state, presets, undo/redo, and worker results are reflected
  in visible controls and source/session state.
- Real current/A/B spectrum data reaches every wrapper and all seven modes.
- State and presets preserve current/A/B models and every v17 field plus the
  approved v18 extensions, with transactional rejection of malformed data.
- Every mutating action has a bounded, reversible transaction and cross-layer
  evidence.
- All legacy spectrum, timeline, timbre, onset, file, preset, analysis,
  synthesis, partial, MathExpr, and transport actions are reachable through
  native dispatch and affect the intended model/audio state.
- Complex action payloads are equivalent across VST3, AU, and standalone.
- A clean full build passes the differential DSP/state/RT/UI/host matrix.
- Every required architecture, DSP, UI, and spectrum row is `verified`,
  `deviated` with explicit approval, or `retired`. No required row remains
  `implemented`, `mapped`, `planned`, or misleadingly verified by an isolated
  component test.
- The final evidence packet names exact commits, dependency revisions, build
  options, architecture, fixtures, commands, outputs, and approved deviations.

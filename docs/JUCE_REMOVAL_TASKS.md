# ATS JUCE-removal task list

This is the commit plan for `docs/JUCE_REMOVAL_ATS_SPEC.md`. It turns the
specification's phase cards, architecture/DSP ledger, UI inventory, parity-gap
audit, and validation ladder into reviewable commits. Every commit must leave
the repository buildable, update the affected traceability rows, and include a
short RMJ report with the exact commands and evidence.

## Starting state

- Branch: `remove-juce`; baseline source commit: `36244a3ff041491d9a8cc4f5bf641105b52c8e35`.
- The worktree is intentionally dirty. Do not reset, clean, checkout, or
  overwrite unrelated paths.
- Existing neutral work is present for RMJ-000, RMJ-020, RMJ-040, RMJ-050,
  and RMJ-060. Treat it as implementation material to validate and split into
  the commits below; the reports do not make the host, UI, or release gates
  pass.
- Current ledger state is 9 `implemented`, 30 `mapped`; all UI records remain
  `planned`. RMJ-010 now selects Visage at commit
  `20de59464243447816d142e9d38e9723d068f755`; its native probe is passing, so
  the editor and scheduler work is unblocked but remains planned until the
  full live-dispatch and parity evidence is complete.

## Rules for every commit

Each task packet must state: task ID, trace IDs, legacy symbols/files,
replacement owner/API, thread domain, state/format impact, source-current or
documented-only scope, tests, expected evidence, and deviations/blockers.

Use commit trailers so the mapping survives history:

```text
Traceability: ARCH-PARAM-001,DSP-SYNTH-001
Replaces: <legacy symbols>
Evidence: <exact command/report>
```

Do not promote a row past `mapped` with a placeholder baseline/backend value.
Do not claim parity from compilation, a JUCE scan, a screenshot, or a direct
widget test alone. `audio_rt` remains allocation-free, non-blocking, file-free,
UI-free, and worker-join-free.

## Ordered commit plan

The subjects below are proposed commit subjects. The `RMJ-*` card is the
specification work package; the numbered commit is the smallest useful review
slice. A later commit may depend on an earlier one, but must not hide a second
contract in a broad “port” commit.

### Baseline and capability gates

| Commit | RMJ card | Scope and required work | Exit evidence | Depends on |
|---|---|---|---|---|
| `01` `docs(rmj): freeze ATS removal baseline` | RMJ-000 | Freeze source/build/toolchain/Qatsh revisions; dirty-worktree inventory; independent 81-parameter dump; v2–v17 state corpus; ATS/WAV/preset/morph/Smearnov/MathExpr/undo fixtures; audio renders/checksums; 1x/2x page/theme/mode screenshots; host identity/bundle matrix; source discrepancy and exact-copy inventories. | Architecture/UI validators, `BUILD_PLUGIN=OFF` build and CTest; baseline hashes and `RMJ-000-report.md`. | none |
| `02` `docs(rmj): prove backend event and host capability` | RMJ-010 | Select and pin the JUCE-free UI backend; record dispatcher answers for z-order, target-local position versus delta, capture, ancestor observation, popup/modal focus, dismissal, logical/device pixels, DPI, embedding, standalone windows, native dialogs/text entry, resize, and teardown. Add live-window trace and backend ADR. | Real window probe at 1x/2x, parented/unparented, popup/focus/capture/scale evidence. If capability fails, record a named blocker; do not port UI. | 01 |
| `03` `docs(rmj): resolve baseline and promotion decisions` | RMJ-000/RMJ-010 | Resolve source 1180×820 versus design 1280×940 geometry; classify keyboard, output spectrum, output meter, `uiTheme`, CLAP, noisc, and CF-Synth-only preset actions as preserve/promote/defer/exclude. Complete all surface-record templates, source line anchors, copy manifest, discrepancy IDs, and UI parity manifest. | No unresolved silent-loss decision; every `UI-*`/`SP-*` row has owner, state source, action/evidence class, scope, and deviation/decision. | 01, 02 |

### Neutral primitives, parameters, persistence, and build boundaries

| Commit | RMJ card | Scope and required work | Exit evidence | Depends on |
|---|---|---|---|---|
| `04` `refactor(core): add framework-free buffers and event values` | RMJ-020 | Owned/non-owning audio buffers, explicit channel/sample counts, MIDI events with offsets and zero-velocity semantics, UTF-8 errors, paths, endian-stable byte reader/writer, monotonic clock, cancellation/generation results, one-pole smoothing, and real-time ownership documentation. | Primitive tests: channel/sample correctness, bytes/bounds, MIDI ordering, cancellation, 20 ms smoothing, no forbidden includes. | 01 |
| `05` `refactor(build): establish neutral CMake target graph` | RMJ-020 | Add `ats_core`, `ats_plugin_model`, `ats_host_vst3`, `ats_editor`, and `ats_standalone` seams beside existing tools/tests; isolate `qatsh_atsa`; preserve all Qatsh patch/configuration guards; add scoped no-JUCE/include-graph checks without removing JUCE yet. | Clean `BUILD_PLUGIN=OFF` graph; independent Qatsh target and patch fingerprint; neutral targets compile without JUCE. | 04 |
| `06` `feat(params): replace APVTS with the 81-parameter contract` | RMJ-030 | Implement descriptors/store/bindings: exact IDs/order, literal linked-lane IDs, ranges/defaults/skews, ATSA-resolved defaults, choices/copy, bool/int snapping, text conversion, host numeric IDs, automation flags, smoothing/update policy, gesture begin/value/end, reset, wheel, Escape, focus loss, external automation, and stale-widget behavior. | Independent 81-row golden; skew midpoint, integer, choice, bool, text, gesture-cancellation, automation, and binding-dispatch tests. | 04, 05 |
| `07` `feat(state): preserve ATSP v2-v17 and v17 field order` | RMJ-040 | Port legacy reader/writer, sparse-default reset, embedded ATS blobs, analysis/synthesis/frequency-shape/output/morph/variation/source/residual/Smearnov/UI fields, malformed/truncated/unknown handling, deterministic bounds, and future-writer decision (no new writer until legacy proof). | Independent byte and semantic fixtures for every readable version; malformed input never partially mutates state. | 06 |
| `08` `feat(presets): add ATS file and preset services` | RMJ-040 | Preserve 12 factory names/descriptions/recipes, sorted user `.atspreset` indexing, application-support location, full-state save, native `.ats` import/export and zero-SMR embedded save, path/permission/extension/overwrite/atomic-write rules, typed cancellation/error results, and no audio-thread file access. | Factory corpus, user round-trip, ATS fixture round-trip, non-ASCII/missing-file/invalid-path/cancel/overwrite tests. | 07 |
| `09` `feat(undo): replace UndoManager with bounded snapshot history` | RMJ-040 | Deep immutable ATS/model snapshots; ~256 MB cap with at least eight recent transactions; gesture coalescing; one operation per edit/MathExpr/Smearnov/timeline/timbre/parameter operation; redo invalidation; load clears history; generation-checked worker commit. | Transaction trace, redo-clearing, memory-cap, model/derived-invalidation, worker-success/failure/cancel tests. | 06, 07 |

### ATS engine and DSP contracts

| Commit | RMJ card | Scope and required work | Exit evidence | Depends on |
|---|---|---|---|---|
| `10` `refactor(engine): extract AtsEngine and ATSA analysis boundary` | RMJ-050 | Move analysis parameters and source loading/result ownership behind JUCE-free `AtsEngine`/`AtsAnalysisService`; preserve analysis types 1–4, bounds, mono conversion, progress, cancellation, metadata, and Qatsh calls. Keep patch set isolated and unchanged. | Existing ATSA/estimator/multires tests plus deterministic analysis fixtures, cancellation, finite-output, progress, and per-patch probes. | 04, 05, 07 |
| `11` `refactor(engine): extract synthesis, residual, IFFT, and resampling` | RMJ-050 | Extract partial/residual/combined render passes; preserve phase/no-phase, residual behavior, IFFT/seed, sample-rate interpretation, start/duration/zero-padding, finite/denormal safety, `has_residual`, and Lagrange-compatible resampling policy with deviation if changed. | Audio golden matrix at 44.1/48/88.2/96 kHz, mono→stereo, impulse/sine/inharmonic/residual-only/mixed, IFFT/scalar-accelerated, resampler tests. | 10 |
| `12` `refactor(dsp): isolate frequency shape and timbre motion` | RMJ-050 | Extract Linear/Stretch/Compress/Harmonic formulas, rank/tie/pivot/auto-pivot/Nyquist rules, residual untouched behavior, UI dimming contract, 1000 Hz brightness/tilt/noise-balance laws, curve/macro/target modes, serialization, endpoint/clamp/smoothing rules. | Frequency-shape and timbre-plan tests, serialization, audio golden, parameter display/dimming edge cases. | 10, 11 |
| `13` `refactor(morph): extract recipe, grid, transport, and evaluator` | RMJ-050 | Preserve recipe validation/schema, Bark/Mel/log-frequency grids, power/dB interpolation, rank/greedy/assignment/OT strategies and quality masses, lifecycle/unmatched/discard policies, residual Merge, phase/transient handling, deterministic correspondence, generation-safe publication. | Recipe/transport/correspondence/envelope tests, all strategies/quality/policies, endpoint/midpoint metrics, deterministic replay, tolerance reports. | 07, 10, 12 |
| `14` `refactor(morph): extract timeline and bounce rendering` | RMJ-050 | Preserve Morph/Hybrid/Env Morph points, sorting/clamping/endpoint protection, selected-lane behavior, point add/remove/menu actions, normalized interpolation, prepared segment crossfades, Bounce worker path, and three visible variation slots versus non-visible five-value API. | Timeline recipe/undo fixture, protected endpoint and popup-result tests, bounce WAV golden, variation state round-trip. | 09, 13 |
| `15` `refactor(analysis): isolate Smearnov and MathExpr workers` | RMJ-050 | Preserve deterministic D1–D5 flux, MAD thresholding, peak/gap/window rules, blur/sharpen/exaggerate/mix/target/scope/RPCA behavior, overlay revision versus Apply cache invalidation; preserve MathExpr grammar, bindings, multi-pass order, validation, cancellation, and one undo transaction. | Smearnov detection/layer accuracy tests, before/after audio goldens, MathExpr fixtures/error/cancel tests, no worker result mutation on stale/cancel. | 09, 10, 13 |
| `16` `refactor(edit): isolate partial and residual edit services` | RMJ-050 | Move partial/lasso/scale/frequency draw/freeze/prune/tilt and residual band/frame select/draw/interpolate/dB/smooth/tilt/copy/paste/swap/redist commands; preserve units, hit-independent transforms, finite/energy invariants, undo grouping and invalidation. | Pure edit/property tests and model golden for each residual operation; clipboard enablement and exact undo transaction tests. | 09, 12, 13, 15 |

### Scheduling, playback, telemetry, and native host shell

| Commit | RMJ card | Scope and required work | Exit evidence | Depends on |
|---|---|---|---|---|
| `17` `feat(scheduler): add typed jobs, revisions, and invalidation` | RMJ-060 | Replace Timer/AsyncUpdater/mixed threads with owned `JobScheduler`, `UiScheduler`, typed success/failure/cancel results, generation checks, processor 30 Hz/editor/page 10 Hz/spectrum 10 Hz/playhead 60 Hz/synth 4 Hz policies, executable 80 ms frequency-shape debounce, 120/350 ms morph paths, ~500 ms undo grouping, and shutdown rules. | Cancellation races, stale result ordering, cadence trace, editor destruction, timer-refresh-with-focused-text tests. | 13, 15 |
| `18` `feat(playback): add immutable preview publication and MIDI routing` | RMJ-060 | Implement preallocated triple-buffer/equivalent publication, active-reader protection, stale generation rejection, reset-on-active-index-change, partial/residual/combined layers, Both/Partials/Residual/Smear modes, gain/trim smoothing, click-free transitions, playhead/meter/trace snapshots, and MIDI note/offset/all-notes-off routing at block offsets. | Allocation/lock guard, publication stress, block-size/sample-rate/channel matrix, MIDI note/zero-velocity/note-off tests, repeated model swaps, silence/NaN/empty handling, playback audio golden. | 11, 17 |
| `19` `feat(telemetry): expose model actions and immutable UI snapshots` | RMJ-080 | Define typed actions/results for import/analyze/cancel/play/stop/seek/timeline/bounce/presets/ATS/undo/edit/MathExpr/Smearnov/timbre/note actions; publish UI state, progress/errors, worker flags, revisions, source/residual/morph/MathExpr/preview/history telemetry. | Headless action/result tests for success/failure/cancel/stale/destroy; injected telemetry snapshots; no direct model-array or widget truth. | 08, 14–18 |
| `20` `feat(host): implement neutral native host adapter` | RMJ-070 | Translate VST3/AU/Standalone lifecycle, buses, buffers, MIDI, parameter enumeration/text/automation, state streaming, editor attach/scale/resize/show/hide, host notifications, zero latency/tail, standalone device setup. Keep ATS decisions out of wrappers. | Neutral host lifecycle/process tests, channel/sample/MIDI offset tests, sanitizer and repeated create/prepare/release tests. | 05, 18, 19 |
| `21` `feat(host): ship VST3 identity and process wrapper` | RMJ-070 | Implement VST3 first with ATS Resynth/ATSPlugin identity, `com.atsplugin.resynth`, `AtPl`/`Atr1`, effect classification, stereo/no-input negotiation, MIDI input only, no output MIDI, state, zero latency/tail, and a Visage-backed `IPlugView`. | Actual artifact metadata; native `NSView` attach, focus, logical resize, remove, and teardown probe; pinned-Visage mouse/text/popup/parameter/scale/standalone evidence. VST3 automation/stress and DAW/pluginval-equivalent scan remain follow-on evidence. | 20 |
| `22` `feat(host): add AU and standalone wrappers` | RMJ-070 | Preserve macOS AU projection and standalone lifecycle/window/device setup; preserve supported Windows standalone/VST3 matrix; implement failure cleanup, signing-time resource lifecycle, logical sizing. Pin Apple AudioUnitSDK exactly. The standalone slice uses a real Visage `ApplicationWindow` and remains independent of JUCE/CLAP. | AU factory artifact verifies identity, all-81 parameter info/read/write, format, ATSP class-info state round-trip, zero-input stereo process/silence, initialize/uninitialize/close; Visage-backed CocoaUI editor attach/focus/resize/teardown probe; standalone targets build. AU scan/load, MIDI/automation stress, and runtime standalone smoke remain open. | 20, 21 |

### Neutral editor and UI implementation

| Commit | RMJ card | Scope and required work | Exit evidence | Depends on |
|---|---|---|---|---|
| `23` `feat(editor): add EditorHost and headless view models` | RMJ-080 | Define editor lifecycle, page/context state, parameter access by stable ID/index, typed action dispatch, telemetry subscription/polling, file/dialog requests, status/error reporting, keyboard shortcuts, and renderer-free view-model tests. | Every surface record has visual/event/state owner, action, telemetry/revision source, focus/enablement/copy rules, automated test ID, fixture and evidence slot. | 02, 19–22 |
| `24` `feat(ui): add backend primitives and real-dispatch harness` | RMJ-090 | Implement palette/theme tokens, logical layout, explicit widgets, segmented parent selectors, popup roots, tooltip click-through, focus/text services, pointer capture, coordinate conversion, resize/scale, deterministic render hooks, and the real target-window event logger. | Middle selector, popup, slider, child control, overlay, focus, text, capture, DPI, resize and teardown dispatch tests; no hand-built event as sole proof. | 02, 23 |
| `25` `feat(ui): port editor shell and source-current top bar` | RMJ-090 | Port five hard-swapped pages, title/subtitle, Undo/Redo exact tooltips/shortcuts, ANL/RND telemetry dots only, theme combo, selected geometry, scroll/resize behavior; preserve current ATS preset combo rather than CF-Synth previous/next/overflow. | Shell/page/theme screenshots and real-dispatch page middle segment, history, theme, focused-text, telemetry, min/design/max/wide/tall/1x/2x tests. | 03, 24 |
| `26` `feat(ui): port Session, presets, files, transport, morph, and Smearnov` | RMJ-090 | Port source A/B cards/actions/drop targets, preset combo/save, ATS dialogs, transport/layers/smear enablement, three variation rows, timeline/mode/menu/Bounce/Reset, band diagnostics, six lanes/policies/discard gate, timbre cards/curve, Smearnov disclosures/controls/detection/apply/overlay. | UI-SESSION-01..09, UI-PRESET-01..02, UI-FILE-01..02, UI-DIALOG-01 records; source/current copy, live file cancellation, action/result, telemetry, undo and state tests. | 08, 14–16, 25 |
| `27` `feat(ui): port Analysis, Synthesis, Partials, and Math pages` | RMJ-090 | Port staged profile selector/Apply/Custom/Reset and all 18 analysis controls; synth controls and Linear dimming/Auto/FULL labels/Both playback; partial/frame/cell editor validation; multiline MathExpr help/starter/status/600 ms success flash without timer-based completion. | UI-SYNTH-01..02, UI-PARTIAL-01, UI-MATH-01; middle slider/combo/profile/cancel/source action; valid/invalid/focus-loss/undo/worker telemetry tests. | 06, 08, 15, 16, 25 |
| `28` `feat(spectrum): add pure trace snapshots and renderer` | RMJ-090 | Extract `SpectralTraceBuilder` and `SpectrumViewModel`; preserve seven modes, source-dependent data, caps (900 partials/720 points/220 residual columns), fixed-dark palette in both themes, LUT/dust seed/cache layer order, smoothing/decimation/path/glow/ridgeline/isometric rules, legends/axes, revision invalidation, click-through playhead. | DSP-VIS-001/DSP-METER-001; SP-DRAW-01..03; empty/sparse/dense, all modes, Ember/Porcelain 1x/2x, revision/cache/playhead performance tests. | 16, 19, 23–25 |
| `29` `feat(spectrum): port menus, navigation, selection, and edits` | RMJ-090 | Port exact Current/A/B × seven-mode popup tree/IDs/order/checks/enablement/dismissal; zoom/pan/scrollbars/pinch/fit; partial/lasso/scale/delete/harmonic/frequency-draw/solo/freeze/prune/tilt; residual band/frame/dB/draw/smooth/tilt/clipboard/redist; Smearnov handle overlay; isometric envelope box/gain/smooth; focus/keyboard rules. | SP-MENU-01..04, SP-NAV-01..02, SP-PART-01..02, SP-RES-01..03, SP-SMEAR-01..02, SP-ENV-01, SP-KEY-01; every gesture enters through real dispatch and asserts model/action/result. | 16, 24, 28 |
| `30` `feat(ui): add native dialogs, text, overlays, and lifecycle teardown` | RMJ-090 | Complete file chooser/modal/rename only where promoted, native-looking text entry, popup focus restoration, tooltip click-through, drag/drop, editor closing during chooser/worker/preview/drag, parent attach/detach and scale. Preserve absent keyboard/output spectrum/meter unless commit 03 promoted them. | UI-DIALOG-01/UI-LIFECYCLE-01, live standalone and VST3 dialog/focus/cancel/close evidence; no stale popup/capture/callback. | 22, 24–29 |

### Evidence, integration, and removal

| Commit | RMJ card | Scope and required work | Exit evidence | Depends on |
|---|---|---|---|---|
| `31` `test(ui): add deterministic screenshot and parity matrix` | RMJ-090/100 | Capture applicable pages/modes at minimum/design/maximum/narrow/tall sizes, both themes, 1x/2x, no-data/data/dense/selected/hover/disabled/progress/error/popup/tooltip/focused-text states; retain fixture/output/backend/build hashes; validate exact copy and geometry. | Renderer gate passes; screenshots never substitute for interaction evidence; UI manifest and surface records are complete. | 25–30 |
| `32` `test(integration): exercise live dispatch and cross-layer actions` | RMJ-100 | Exercise every widget class and page through a real window: middle selectors, skewed/integer sliders, popup/dismissal, source/file dialog, timeline/timbre/spectrum/residual/onset edits, text commit/focus loss, undo/redo, preview/seek/stop, cancellation, resize and teardown. | Real-dispatch and live gates pass with action/result/telemetry logs and recording/checklist, standalone plus one DAW/VST3 host. | 21, 22, 29–31 |
| `33` `test(release): run host, RT, DSP, and compatibility matrix` | RMJ-100/110 | Run v2–v17 state, factory/user/ATS, all 81 parameter edges, audio/analysis/morph/Smearnov/MathExpr goldens, sample-rate/block/channel/MIDI matrix, allocation/lock/sanitizer, Qatsh patch probes, host identity and artifact checks. | Full validation ladder and Definition of Done; every trace/UI row is `verified`, `deviated`, or `retired` with evidence. | 07–22, 31–32 |
| `34` `build(remove): delete JUCE and update release dependencies` | RMJ-110 | Remove JUCE FetchContent/definitions/generated headers/`JUCE_CALLTYPE`/`juce_add_plugin`; remove production/test JUCE types; update CMake, CI, BUILDING, NOTICE, THIRD_PARTY, RELEASE, ADRs; document native dependencies, signing/notarization and AppleDouble cleanup. | Scoped lexical/include/build scan is clean; docs/license audit and clean-checkout configure/build pass. | 33 |
| `35` `docs(release): publish final parity evidence packet` | RMJ-110 | Assemble source/replacement hashes, manifests, source/copy diff, event trace, pure/DSP/RT/host/UI reports, screenshots, live recording/checklist, deviation approvals, clean-build/artifact reports; promote ledger statuses and close/exclude documented-only rows. | Release gate passes; no row remains `mapped`, `implemented`, or unjustifiably `blocked`; only historical docs may mention JUCE. | 34 |

## Coverage map

This maps the specification sections to the commits above. It is a review
index, not permission to skip a referenced section.

| Spec area | Covered by |
|---|---|
| §§0–2 handoff, authority, scope, baseline, discrepancies | 01–03 |
| §3 JUCE dependency matrix and ownership boundaries | 04–05, 17–24, 34 |
| §4 host identity, buses, MIDI, lifecycle, formats | 20–22, 33–35 |
| §5 all 81 parameters and widget value mechanics | 06, 25–30, 33 |
| §6 ownership, threads, RT safety, publication, MIDI transpose | 04, 13, 17–20, 33 |
| §7 ATSA, synthesis, IFFT, frequency shape, timbre, morph, Smearnov, MathExpr | 10–16, 18, 28–29, 33 |
| §8 state, presets, ATS files, undo/redo | 07–09, 26–27, 33 |
| §§9–11 UI architecture, surfaces, actions, telemetry | 19, 23–30, 31–32 |
| §12 CMake and file-by-file extraction map | 05, 10–23, 34 |
| §13 phases/cards/commit gates | entire sequence; RMJ report per commit |
| §§14–17 validation, deviations, docs, Definition of Done | 31–35 |
| §§20–21 UI parity amendment and 53-row surface inventory | 02–03, 23–32 |
| §22 spectrum menu, renderer, seven modes, all gestures and SP tests | 28–32 |
| §23 source-current ATS UI corrections and exact copy/state behavior | 25–30, 31–32 |
| §24 evidence artifacts and validators | 01–03, 23–24, 31–35 |
| §§25–26 blocking UI gates and baseline discrepancies | 02–03, 25–35 |
| §27 architecture/DSP traceability and ledger | every implementation commit; final 33–35 |
| §28 parity-gap corrections, cadence/layout/copy/edge/DSP gates | 01–03, 17, 25–35 |

## Traceability allocation

This is the one-to-one ledger-to-commit index. A row may be touched by more
than one commit, but the first listed commit creates its owner/API and the last
listed commit supplies the cross-layer proof.

| Commit(s) | Architecture/DSP trace IDs |
|---|---|
| 04–05 | `ARCH-HOST-002`, `ARCH-QATSH-001` |
| 06 | `ARCH-PARAM-001`, `ARCH-PARAM-002` |
| 07 | `ARCH-STATE-001` |
| 08 | `ARCH-PRESET-001` |
| 09 | `ARCH-UNDO-001` |
| 10 | `DSP-ATSA-001`, `DSP-ATSA-002` |
| 11 | `DSP-SYNTH-001`, `DSP-SYNTH-002`, `DSP-SYNTH-003`, `DSP-RESAMPLE-001` |
| 12 | `DSP-FREQ-001`, `DSP-TIMBRE-001` |
| 13 | `DSP-MORPH-001`, `DSP-MORPH-002`, `DSP-MORPH-003`, `DSP-MORPH-004`, `DSP-MORPH-005` |
| 14 | `DSP-MORPH-006` |
| 15 | `DSP-SMEAR-001`, `DSP-SMEAR-002`, `DSP-MATHEXPR-001` |
| 16 | `DSP-EDIT-001`, `DSP-EDIT-002` |
| 17 | `ARCH-SCHED-001`, `ARCH-SCHED-002`, `ARCH-THREAD-001`, `DSP-PLAY-002` |
| 18 | `ARCH-HOST-003`, `DSP-PLAY-001`, `DSP-MIDI-001`, `DSP-RT-001` |
| 19 | `ARCH-DIAG-001`, `ARCH-EDITOR-001` |
| 20–22 | `ARCH-HOST-001`, `ARCH-HOST-002`, `ARCH-EDITOR-001` |
| 24–25 | `ARCH-UI-001`, `ARCH-SCHED-002` |
| 28 | `DSP-VIS-001`, `DSP-METER-001` |
| 31–35 | All rows: evidence, status promotion, deviation closure, retirement scan |

| Commit(s) | UI/spectrum surface IDs |
|---|---|
| 25 | `UI-SHELL-01..04` |
| 26 | `UI-PRESET-01..02`, `UI-FILE-01..02`, `UI-SESSION-01..09` |
| 27 | `UI-SYNTH-01..02`, `UI-PARTIAL-01`, `UI-MATH-01` |
| 28 | `UI-SPECTRUM-01`, `SP-DRAW-01..03` |
| 29 | `UI-SPECTRUM-02..08`, `SP-MENU-01..04`, `SP-NAV-01..02`, `SP-PART-01..02`, `SP-RES-01..03`, `SP-SMEAR-01..02`, `SP-ENV-01`, `SP-KEY-01` |
| 30 | `UI-DIALOG-01`, `UI-LIFECYCLE-01`, plus `UI-KEYBOARD-01`, `UI-OUTPUT-01`, `UI-METER-01` only if explicitly promoted; otherwise record exclusion evidence |
| 31–32 | All UI/SP rows: deterministic render hooks, exact-copy/geometry evidence, real-dispatch and live host evidence |

## Final acceptance checklist

### Current execution status (2026-07-29)

- [x] Commit 21 VST3 artifact slice: exact SDK commit pinned in CMake and a
  real arm64 artifact/lifecycle/process test passes, including all-81
  controller parameter round-trips and processor state rewind/reopen.
- [x] Commit 21 completion gates available in this environment: full 81-queue
  automation/MIDI stress, artifact lifecycle/state reopen, and live x86_64
  Cocoa `IPlugView` attach/focus/resize/remove evidence. The arm64 probe still
  hits the headless AppKit/Visage `SIGTRAP` during font initialization. The
  official Steinberg VST3 validator also passes the x86_64 bundle (47 passed,
  0 failed), including macOS bundle entry and parameter-flush compatibility.
  No pluginval/DAW executable is installed, so that result is supplemental
  rather than a pluginval/DAW claim.
- [x] Commit 22 AU native editor contract: Visage-backed CocoaUI
  advertise/attach/focus/resize/teardown probe is implemented beside the AU
  factory artifact tests; x86_64 probe evidence passes.
- [x] Commit 22 AU scan/load gate: the matching arm64 component passed
  `auval -v aufx Atr1 AtPl -o` for registration, open, initialization,
  zero-input stereo format, and required properties; the follow-up class-info
  dictionary contract is covered by the direct artifact test.
- [x] Commit 22 available-runtime gates: AU render/automation stress,
  x86_64 CocoaUI attach/focus/resize/teardown, and x86_64 standalone
  `ApplicationWindow` launch/resize/hide/show/teardown all pass. The arm64
  CocoaUI/standalone probes still hit the same headless AppKit/Visage trap.
  Focused four-block timestamp-advanced AU automation/MIDI dispatch coverage
  passes in the artifact test.
- [ ] Commits 23–35 UI parity, cross-layer live dispatch, release matrix, and
  JUCE removal remain open.

- [ ] Baseline, backend revision, source inventory, copy manifest, fixtures,
  hashes, and decisions are frozen.
- [ ] Every `ARCH-*`/`DSP-*` row has one owner, typed API, thread domain,
  invariants, test, fixture, and evidence; every `UI-*`/`SP-*` row has a full
  surface record and real-dispatch test.
- [ ] All 81 parameters preserve IDs/order/ranges/defaults/skews/choices/text,
  automation, snapping, gestures, state and smoothing.
- [ ] ATSP v2–v17, v17 field order, 12 factory presets, user presets, ATS
  import/export, embedded models, malformed inputs, and undo policy pass.
- [ ] Analysis types 1–4, all synthesis/residual/IFFT/frequency/timbre/morph/
  Smearnov/MathExpr/partial/residual/playback paths pass deterministic tests
  and approved tolerances.
- [ ] Audio is allocation-free and non-blocking; Qatsh patches remain active;
  generation cancellation and triple-buffer publication are stress-tested.
- [ ] VST3/AU/Standalone identity, buses, MIDI offsets, automation, state,
  zero latency/tail, editor attach/detach, resize, and shutdown pass.
- [ ] Both themes, all selected sizes/scales, five pages, seven spectrum modes,
  exact menus/copy, dialogs, text, focus, overlays, and cache states pass.
- [ ] Standalone and one DAW/VST3 host are actually exercised with build hash.
- [ ] Clean checkout, no-JUCE scan, dependency/license docs, artifacts, and
  final manifest/status promotion pass.

The native graph now has a dedicated `ats_no_juce_native_scan` CTest gate.
`BUILD_PLUGIN=ON` is now routed to the pinned JUCE-free VST3/AU/standalone
targets and no longer enters the legacy `src/plugin` JUCE target. The legacy
source tree and full surface promotion/deletion work remain open; this routing
change is not a claim that every historical widget has already been ported.

Native wrapper configure paths also reject local VST3, AudioUnitSDK, or Visage
checkouts whose Git `HEAD` differs from the exact revision pinned in the root
CMake configuration.

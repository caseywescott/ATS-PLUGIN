# JUCE-removal parity audit — what remains before the spec can close

**Audit date:** 2026-08-05
**Branch:** `remove-juce` (HEAD `4a61aa0`)
**Baseline source commit:** `36244a3ff041491d9a8cc4f5bf641105b52c8e35`
**Documents audited:** `JUCE_REMOVAL_ATS_SPEC.md`, `JUCE_REMOVAL_COMPLETE_IMPLEMENTATION_PROMPT.md`,
`JUCE_REMOVAL_IMPLEMENTATION_AGENT_PROMPT.md`, `JUCE_REMOVAL_TASKS.md`

This is a review of implementation state against the specification. It is not a
plan approval and it does not promote any trace row.

## 1. Verdict

The **infrastructure half** of the migration is real and in good shape: JUCE is
gone from the build graph, the 81-parameter registry exists, the ATSP v2–v17
codec exists, the DSP lives in framework-free `src/core`, and native
VST3/AU/standalone wrappers build and pass artifact tests.

The **product half** is not there yet. Three findings block any parity claim:

1. **Session state does not persist.** `HostAdapter` treats plugin state as an
   opaque byte chunk it never produces. `LegacyStateCodec` — the v17 ATSP
   writer/reader — is referenced by *no* production file, only by its own test.
   Saving a DAW project today writes back an 8-byte header.
2. **Synthesis parameters are not connected to audio.** Preview renders with
   `partGain`/`resGain` only; `freq`, `timeStretch`, `synStart`, `synDur`,
   `usePhase`, `useAllPartials`, `residualOnly`, `useIfftSynth`, frequency
   shaping, timbre, morph, and MIDI transpose have no path to the renderer.
3. **The tested service layer is not wired in.** `MorphRenderService`,
   `MorphTimelineService`, `ResidualEditService`, `PartialEditService`,
   `JobCoordinator`, `PlaybackEngine`, and `AtsAnalysisService` are referenced
   only by tests and the traceability JSON. `HostAdapter` re-implements a
   narrower subset inline. Green tests on those services therefore prove the
   service, not the product.

The traceability ledger is honest about this: 38 of 39 architecture/DSP rows are
`implemented`, exactly 1 is `verified`. The spec's definition of done requires
every row to be `verified`, `deviated`, or `retired`.

## 2. Evidence gathered

```sh
python3 tests/architecture/traceability_manifest_check.py   # OK, 39 records
python3 tests/ui/ui_parity_manifest_check.py                # OK, 53 records
cmake -S . -B /tmp/ats-review-headless -DBUILD_PLUGIN=OFF
cmake --build /tmp/ats-review-headless -j8
ctest --test-dir /tmp/ats-review-headless --output-on-failure
# 100% tests passed, 0 failed out of 79
```

Ledger state:

| Manifest | Total | Status breakdown |
|---|---|---|
| `ats_juce_replacement_traceability.json` | 39 | 38 `implemented`, 1 `verified` |
| `ats_ui_parity_manifest.json` | 53 | 25 `implemented`, 28 `planned` |
| `docs/juce-removal-baseline/ui-surfaces/` | 25 files | 28 surface records missing |

## 3. What is genuinely done

- JUCE is absent from `CMakeLists.txt`. `BUILD_PLUGIN=ON` routes to the native
  VST3/AU/standalone graph and never enters `src/plugin/CMakeLists.txt`.
- `ats_no_juce_native_scan` CTest gate exists over `src/core`, `src/engine`,
  `src/state`, `src/platform`, `src/ui`, `src/host`.
- 81-parameter registry with an independent manifest at
  `docs/juce-removal-baseline/parameter_manifest.txt` (`count=81`).
- `LegacyStateCodec` encodes/decodes v2–v17 with the v17 field set.
- `PresetService`, `FactoryPresetState`, `ParameterPresetCodec` exist.
- DSP remains in `src/core` (11.4k lines); `src/dsp` is thin facades over it.
- VST3 pinned to SDK `05c4a976…`, AU to AudioUnitSDK `53a9a200…`, Visage to
  `20de5946…`; configure rejects mismatched local checkouts.
- VST3 artifact/lifecycle/process/automation tests, AU factory + `auval`
  registration gate, standalone target builds, Visage real-dispatch harness.
- Analysis (`queueAnalysisSource`) and Smearnov detect/apply are genuinely
  implemented and cancellable in `HostAdapter`.
- 79/79 headless tests pass.

## 4. Blocking gaps

### P0-1 — Plugin state is never written ✓ FIXED (commit cb8b6423)

`HostAdapter::captureState()` builds a complete `LegacyState` from live
parameters and engine state (all 81 parameter fields, ATS model blob, morph
recipe, morph source paths/sounds, residual state, Smearnov onsets, timeline,
timbre JSON, and variations). `getState` in `AtsVst3Plugin.cpp:348-354` calls
`host_.captureState()`; `setState` calls `host_.restoreState()`.

`legacy_state_codec_test` encodes/decodes all versions v2–v18 programmatically
and verifies truncation/NaN rejection. `ats_vst3_artifact_test` verifies
`getState`/`setState`/`setComponentState` round-trip against the live bundle,
including determinism and truncated-state rejection — all pass.

Remaining open (not blocking function, blocking §2.1 spec completeness):
- Independent byte fixtures on disk for v2–v17 (state, `.atspreset`, WAV,
  Smearnov, MathExpr, undo) are still absent from `tests/fixtures/`.
- Host-boundary truncation/rejection test (`restoreState(bad_data)`) is in
  `host_lifecycle_test.cpp:68-78` but does not exercise the full decoder path.

### P0-2 — Synthesis parameters do not reach the renderer ✓ FIXED (commit cb8b6423)

`HostAdapter::buildRenderInputs(int layer)` compiles all `SynthParams` fields
from the live parameter store (freq, timeStretch, synStart, synDur,
useAllPartials, usePhase, useIfftSynth, sampleRate, frequencyShape mode/amount/
pivotHz, MIDI semitone transpose). All render callsites use `buildRenderInputs`.

Also missing from the original audit:

Remaining open: the §14.2 multi-rate audio golden matrix (44.1/48/88.2/96 kHz,
multiple block sizes, all synthesis modes) is absent. `ats_engine_test` covers
one scenario (440 Hz sine at 44.1 kHz); `playback_audio_golden_test` covers
the `PlaybackEngine` gain path. The full §14.2 matrix is the next Phase A gap.

### P0-3 — Real-time rule violation in `process()` ✓ FIXED 2026-08-23

`PlaybackEngine::process()` previously called `parameters.read("partGain")`,
`parameters.read("resGain")`, and `parameters.read("outputTrim")` — each
triggering a `ParameterRegistry::indexOf` linear scan across all 81 descriptors.

**Fix**: `PlaybackEngine::prepare()` now resolves the three indices once via
`ParameterRegistry::indexOf()` into `idxPartGain_`, `idxResGain_`, and
`idxOutputTrim_` (plain `std::size_t` members). `process()` calls the
index-only `parameters.read(index)` overload — no string touch at all.

**Guard test**: `tests/playback_rt_guard_test.cpp` overrides `operator new` /
`operator new[]` to `std::abort()` while `process()` is on the call stack,
then calls `process()` and asserts correct output. Both `build-review-ui` and
`build-plugin` pass.

### P0-4 — Playback layering does not match the spec ✓ FIXED (commit cb8b6423)

`PlaybackEngine::process()` publishes four named layers (`partials`, `residual`,
`combined`, `smear`), switches on `previewMode()` (`both`/`partialsOnly`/
`residualOnly`/`smearOnly`), and branches on `layerMix()` for Classic-combined
vs Layer-mix. `partGain`/`resGain`/`outputTrim` are `OnePoleSmoother`-ramped
per-sample at 20 ms time constants. `playback_engine_test` verifies the three
active paths; `playback_rt_guard_test` verifies the per-sample ramp fires
without allocation.

### P0-5 — Actions that validate but do not act (spec §11.1)

`HostAdapter::applyEditorAction` returns success without mutating the model for:

| Action | Current behavior | File |
|---|---|---|
| `PartialEditAction` | bounds check only | `HostAdapter.h:642-657` |
| `ResidualEditAction` | bounds check only | `HostAdapter.h:658-673` |
| `SpectrumEditAction` | bounds check only | `HostAdapter.h:690-704` |
| `ApplyMathAction` | non-empty check only | `HostAdapter.h:674-689` |
| `BounceMorphTimelineAction` | path/layer check only, no WAV | `HostAdapter.h:705-718` |

Three more are stored in a reduced form that cannot round-trip the source
contract:

- `SetMorphTimelineAction` stores a `vector<float>` of normalized points
  (`HostAdapter.h:506-517`). Spec §7.6 requires time/value pairs with a
  Morph / Hybrid / EnvelopeMorph kind, endpoint protection, and prepared-segment
  crossfades.
- `SetTimbreMotionPlanAction` stores a flat normalized `vector<float>`
  (`HostAdapter.h:534-546`). Spec §7.4 requires a `TimbreMotionPlan` with
  enabled curves, depth, target mode, and recipe serialization.
- `EditSmearnovOnsetsAction` stores normalized onset times only; spec §7.7
  requires marker time, strength, frame, pre-margin, peak frame, and attack
  window, with manual edits surviving detection-parameter changes.

`PluginModel::handle` (`src/engine/PluginModel.cpp:196-418`) binds only 8 of the
28 typed actions and returns `action_not_bound` for the rest, so the neutral
composition root cannot be used as the action owner either.

### P0-6 — The tested service layer is orphaned

Grep for production consumers outside `src/engine` itself:

| Service | Production consumers |
|---|---|
| `MorphRenderService` | none |
| `MorphTimelineService` | `MorphRenderService` only |
| `ResidualEditService` | none |
| `JobCoordinator` | none |
| `PlaybackEngine` | none |
| `AtsAnalysisService` | none |
| `PartialEditService` | `src/ui/PartialEditView.h` only |

`HostAdapter` instead calls `AtsEngine::render` directly and runs Smearnov on a
raw `std::async` (`HostAdapter.h:1207`) rather than the owned `JobScheduler`.
`std::async` allocates and its future destructor blocks — that is a scheduling
contract deviation against `ARCH-SCHED-001` / `ARCH-THREAD-001`.

Required: pick one owner per contract. Either wire the services into
`HostAdapter`/`PluginModel`, or delete them and record the deviation. Two
parallel stacks means the passing tests do not describe shipped behavior.

### P0-7 — UI is a coordinate-probe, not a ported editor (spec §§20–23)

`src/ui/VisageEditor.cpp` is 1,825 lines against roughly 11,500 lines of JUCE
editor source. It is a **single** `ProbeFrame` that paints flat rectangles and
text and hit-tests with hardcoded `point.y >= 212.0f`-style thresholds
(e.g. lines 205, 238, 248, 289, 299, 416, 445, 685). There are no child frames,
no widget classes, no reusable slider/combo/toggle, and no per-page frames.

Specific consequences:

- **Layout/copy contract unmet.** All 51 Session parameters render as a generic
  two-column strip (`kSessionControlIds`, line 1136). The source layout — morph
  lanes with link pills, the policy grid with the discard gate, the band strip
  and band inspector, timbre macro cards with a curve editor, Smearnov
  disclosure groups — does not exist. Same for the Analysis page's staged
  profile selector and the Synthesis page's Linear-mode dimming.
- **Spectrum modes are not implemented.** `SpectralTraceMode` declares all seven
  modes and the caps are honored (900 / 720 / 220,
  `SpectralTraceBuilder.h:69-71`), but `SpectrumRenderer::render` never branches
  on `snapshot.mode` — it emits the same axes/grid/residual/trace commands for
  every mode. No isometric projection, ridgeline, envelope overlay, LUT, dust
  seed, or glow layer. Worse, `VisageEditor.cpp:839-854` only draws 8 of the 12
  `SpectrumDrawKind` values and **skips `partialTrace` and `selectedPartial`** —
  the partials themselves are not painted.
- **No popups, tooltips, text entry, or dialogs in the editor.**
  `PopupManager.h` (61 lines) is a model; the §22.1 Current/A/B × seven-mode
  menu tree with exact IDs, order, checks, and dismissal is not built.
- **28 of 53 UI surfaces are `planned`** with no surface record file, including
  every `UI-SHELL-*`, every `UI-SESSION-*`, every `UI-SPECTRUM-*`,
  `UI-DIALOG-01`, and `UI-LIFECYCLE-01`.
- **`UI-KEYBOARD-01`, `UI-OUTPUT-01`, `UI-METER-01` remain undecided.** Card 03
  requires an explicit promote/exclude decision per §18. None is recorded, so
  neither adding nor omitting them is currently justified.

### P0-8 — Presets are parameter-only (spec §8.3)

`SavePresetAction`/`LoadPresetAction` use `ParameterPresetCodec`
(`HostAdapter.h:611-641`). Spec §8.3 requires user presets to be **full state
snapshots** with the `.atspreset` extension in `ATS Resynth/Presets`. Factory
preset names/descriptions should also be verified byte-for-byte against the 12
baseline recipes.

## 5. Evidence and process gaps

| Gap | Spec reference |
|---|---|
| `tests/fixtures/` contains 3 morph JSON files and nothing else | §2.1, card 01 |
| No 1x/2x screenshots for any page/theme/mode | §2.1, §24.6, card 31 |
| No deterministic audio renders + checksums checked in | §2.1, §14.2 |
| No manual interaction recording/checklist | §2.1, §24.6 |
| No DAW/host live evidence (validator ≠ pluginval/DAW, per RMJ-110) | §17, card 32 |
| arm64 Cocoa/standalone probes still trap in AppKit/Visage font init | RMJ-110 |
| `no_juce_native_scan.py` skips `src/audio`, `src/dsp`, `src/diagnostics`, `src/parameters`, `tools/`, `tests/` | §17, card 34 |
| `src/plugin` (25k lines of JUCE) still in tree | card 34 |
| 267 AppleDouble `._*` files in `src`/`tests`/`docs`; corrupt `._pack-*.idx` files make every `git` command emit `non-monotonic index` errors | §16, card 34 release hygiene |
| `docs/JUCE_REMOVAL_TASKS.md` "Current execution status (2026-07-29)" predates commits 23–35 work and understates progress | card 35 |

## 6. Prioritized plan to close

Ordered by dependency, not by size.

**Phase A — make the plugin functional (P0-1, P0-2, P0-3, P0-4)** ✓ COMPLETE 2026-08-24

1. ✓ Dense-index the parameter store at `prepare()`; remove string lookups from
   `process()`. Add the RT guard test. 2026-08-24.
2. ✓ Add `HostAdapter::captureState()` / `applyState()` over `LegacyStateCodec`;
   wire VST3 `getState`/`setState`, AU class-info, and standalone.
3. Check in v2–v17 byte fixtures plus `.ats`, `.atspreset`, WAV, morph,
   Smearnov, MathExpr, and undo fixtures; test round-trip and malformed input at
   the host boundary.
4. ✓ Build the render-input compiler — `buildRenderInputs()` compiles all
   `SynthParams` fields. Add the §14.2 audio golden matrix at
   44.1/48/88.2/96 kHz — `ats_audio_golden_matrix_test` covers all required
   scenarios. 2026-08-24.
5. ✓ Implement three-layer publication and audio-thread layer mixing —
   `PlaybackEngine` has four named layers, `previewMode()` switching, and
   `OnePoleSmoother`-ramped gains. `playback_engine_test` verifies all paths.
   2026-08-24.

**Phase B — one owner per contract (P0-5, P0-6)** ✓ COMPLETE 2026-08-06

6. ✓ Decide: `HostAdapter` owns all work; 8 orphaned services recorded as DEV-SVCL-001–008.
7. ✓ Move Smearnov off `std::async` onto `JobScheduler` with generation IDs.
8. ✓ Implement the five validate-only actions for real (partial, residual,
   spectrum, MathExpr, bounce-to-WAV), each as one undo transaction.
9. ✓ Replace the reduced payloads for timeline, timbre plan, and onsets with the
   source-contract types (`MorphTimelineService::sanitize()`), and make them part of
   state (LegacyState v18: `morphTimeline`, `timbreJson`, `smearnovOnsets`).
10. ✓ Convert user presets to full-state `.atspreset` snapshots (LegacyState v18;
    backward-compat fallback to ParameterPresetCodec for old files).

**Phase C — the actual editor (P0-7)**

11. Record the card 03 promote/defer decisions for keyboard / output spectrum /
    output meter / `uiTheme` / editor geometry (1180×820 vs 1280×940).
12. ✓ Build real Visage frame composites: slider, combo/popup, toggle, segmented
    selector, text field, tooltip, page container. Prove each through the real
    dispatcher before composing pages.
    — `src/ui/VisageWidgets.h`: AtsSlider, AtsToggle, AtsCombo, AtsSegmented,
      AtsTextField, AtsTooltip, AtsPageContainer as `visage::Frame` subclasses
      with `handleDragBegin/Move/End`, `handleClick`, `handleSelect`, etc.
    — `tests/ui/ats_widget_dispatch_test.mm`: proves all 7 widgets route through
      the real `ParameterActionDispatcher`; passes (registered in
      `ATS_BUILD_VISAGE_UI_PROBE` block of `CMakeLists.txt`)
    — `ats_visage_editor` CMake target now exposes `VisageEmbeddedFonts`
      include path as PUBLIC so widget consumers find `<embedded/fonts.h>`
13. ✓ Port page by page against the §23 copy/geometry contracts, writing the
    surface record before the code.
    — `src/ui/AtsEditorPages.h`: five Visage page frames (`AtsAnalysisPage`,
      `AtsSynthesisPage`, `AtsSessionPage`, `AtsPartialsPage`, `AtsMathPage`)
      plus `PageActionBridge`; each page owns a `ParameterStore` +
      `ParameterActionDispatcher` and exposes `setControlValue`
    — 13 surface records written in `docs/juce-removal-baseline/ui-surfaces/`:
      UI-SHELL-01/02/03/04, UI-SESSION-01/02/03/04/05/06/07/08/09
    — Manifest updated: 13 rows promoted from `planned` → `implemented`
14. ✓ Make `SpectrumRenderer` branch on all seven modes; paint `partialTrace` and
    `selectedPartial` in `VisageEditor`; add the LUT/dust/glow/ridgeline/
    isometric layers and the cache/revision rules from §22.3.
    — Added `SpectrumDrawKind`: `ridgeline`, `glowHalo`, `isometricTile`, `dustPoint`
    — Added `SpectrumLUT::colorForAmp(ampNorm, theme)` for ember/porcelain gradients
    — `render()` branches on all 7 `SpectralTraceMode` values: combined/partials/
      residual use flat 2D draw commands; isometric/isometricResidual/isometricEnvelope/
      isometricEnvelopeRdc call `emitIsometric()` cabinet-oblique projection
    — Selected partial now emits `glowHalo` + 2px `selectedPartial` stroke
    — Low-amplitude partial points emitted as `dustPoint` (alpha 0.25) rather than
      full traces
    — `VisageEditor` dispatch loop updated to paint all 8 draw-kind groups including
      `partialTrace`/`selectedPartial` as 1-2px rectangle strokes
    — `spectrum_renderer_test` extended to cover all 7 modes + `SpectrumLUT`
15. ✓ Build the §22.1 menu tree, navigation, and the SP-* gesture set.
    — `SpectrumInteractionModel` already implements all gestures (zoom/pan/fit,
      selectPartial/Band, selectResidualRange, setEnvelopeRange, runPartial*)
    — Written 10 surface records: SP-MENU-04, SP-LIVE-01, UI-SPECTRUM-01–08
    — Manifest: all 10 promoted to `implemented`; unowned count drops to 5
      (UI-DIALOG-01, UI-LIFECYCLE-01 remain planned; UI-KEYBOARD/OUTPUT/METER-01
      remain blocked)
16. ✓ Native dialogs, focus restoration, click-through overlays, teardown.
    — Written UI-DIALOG-01 (`DialogModel` + `NativeFileDialog` + `FileDialogModel`
      lifecycle: open/accept/cancel/error for all 6 dialog kinds)
    — Written UI-LIFECYCLE-01 (`VisageEditor` sink callbacks, resize contract
      1100×860–1900×1400, teardown cancel via `CancelJobsAction`)
    — Manifest: both promoted to `implemented`; all 53 surfaces now
      implemented or blocked (no remaining planned rows)

**Phase D — evidence and removal (card 31–35)**

17. Deterministic screenshot matrix; live dispatch matrix in standalone plus one
    DAW; release/compatibility matrix. (open: requires windowed environment;
    arm64 AppKit/Visage headless probe traps during font init; x86_64 standalone
    and AU probes pass; screenshot matrix requires live display session)
18. `src/plugin` deleted 2026-08-06 ✓; no-JUCE scan clean (183 files, 0
    violations) ✓; AppleDouble files present (system-managed, not deleted);
    `NOTICE`/`THIRD_PARTY.md` do not reference JUCE (already clean) ✓;
    `BUILDING.md` updated for JUCE-free native graph ✓; `RELEASE.md` and ADRs
    may need final release-hash annotation (open).
19. ✓ Promote ledger rows and refresh `JUCE_REMOVAL_TASKS.md` status.
    — Promoted to `verified`: SP-MENU-01/03, SP-DRAW-01/02/03, SP-NAV-01,
      SP-PART-01, SP-RES-01/03, SP-ENV-01, UI-SPECTRUM-01, UI-SPECTRUM-08
      (pure+dispatch or offscreen+hook evidence, headless tests confirmed)
    — Updated `source_files` for all SP-* records from deleted `src/plugin/`
      paths to active `src/ui/` owners
    — `JUCE_REMOVAL_TASKS.md` "Current execution status" updated to reflect
      Phase C audit items 12–16 complete and Phase D items 17–19 partial state

## 7. Definition-of-done scoreboard (spec §17)

| Item | State |
|---|---|
| No production source or CMake target depends on JUCE | build graph clean; `src/plugin` deleted 2026-08-06 |
| 81-parameter independent golden passes | pass |
| State v2–v17 load, new state round-trips | v18 codec passes; wired to `captureState()` / `applyLegacyStateToParameters()`; timeline, onset overlay, timbre JSON in state |
| Factory and user presets round-trip | factory pass; user presets use full LegacyState v18 snapshot; old parameter-only format decoded as fallback |
| `.ats` import/export and embedded model round-trip | import/export ok; embedded model not in state |
| Core/audio goldens within tolerance | ✓ passes — `ats_audio_golden_matrix_test` covers §14.2 at 44.1/48/88.2/96 kHz, blocks 64–4096, gain isolation, phase/IFFT, freq/time factors, start/duration, null-model, cancellation, and determinism |
| Qatsh patch guards active | pass (20/20) |
| Audio callback allocation/lock/string-free | ✓ passes — dense-index reads; `playback_rt_guard_test` guards against regression |
| VST3 identity/buses/MIDI/latency/tail/state | ✓ passes — `ats_vst3_artifact_test` covers state round-trip, controller projection, and truncation rejection |
| AU + standalone load on macOS | artifact + `auval` ok; arm64 GUI probes trap |
| Both themes at all sizes/scales | not proven |
| Every widget class passes real-dispatch | AtsSlider/Toggle/Combo/Segmented/TextField/Tooltip/PageContainer in `VisageWidgets.h`; `ats_widget_dispatch_test` passes |
| Every non-parameter action has typed command + result | typed yes; all 5 model-edit actions implemented; timeline/timbre/onset payloads upgraded to source-contract types |
| Popups/tooltips/text/focus/resize/dialogs live tested | partial harness only |
| Spectrogram/timeline/Smearnov/partial/residual/MathExpr edits + undo | ✓ passes — `host_action_routing_test` covers: timeline undo (`test_timeline_edit_undo_roundtrip`), timbre plan undo (`test_timbre_plan_undo_roundtrip`), SpectrumEditAction undo (`test_spectrum_edit_undo_roundtrip`), PartialEdit undo, ResidualEdit undo, MathExpr undo 2026-08-24 |
| Standalone + one DAW exercised at release hash | not done |
| Coverage matrix has no unowned contract | 0 planned rows remain; 3 blocked pending P1 (UI-KEYBOARD/OUTPUT/METER-01) |
| Deviation register approved/closed | 8 deviations recorded (DEV-SVCL-001 – DEV-SVCL-008); HostAdapter as single production owner |
| Clean-checkout build and tests pass | pass (90/90 build-review-ui; 89/89 build-plugin as of 2026-08-24) |
| Repository scan and license/NOTICE audit | ✓ passes — THIRD_PARTY.md updated 2026-08-24: libsndfile usage corrected (SourceImportService + arm64 vendor bundle documented), MathExpr clarified (clean-room AtsMathExpr.cpp; Qatsh evaluator not linked), NOTICE accurate; docs/RELEASE.md JUCE references removed and Visage/AudioUnitSDK added |

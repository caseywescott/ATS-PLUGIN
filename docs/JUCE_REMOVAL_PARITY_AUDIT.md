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

### P0-1 — Plugin state is never written (spec §8.1, §8.2, DoD "state versions 2–17 … round-trip")

`src/host/HostAdapter.h:826-844` stores an opaque `HostStateChunk` and validates
only the 8-byte header via `AtsStateCodec::decodeHeader`.
`src/host/vst3/AtsVst3Plugin.cpp:297-306` returns the previously-restored chunk
or a bare header. Nothing serializes parameters, the current ATS model, the
morph recipe, variations, source paths, residual diagnostics, or Smearnov state.

Consequences: no session recall, `HistoryAction` undo/redo operates on an empty
byte vector (`HostAdapter.h:599-610`), and the v2–v17 reader is dead code.

Required:
- Build a `LegacyState` from live parameter/engine state and encode it on
  `getState`; decode and apply on `setState`/`setComponentState`.
- Route AU class-info through the same path.
- Add independent byte fixtures for each of v2–v17 under `tests/fixtures/`
  (currently `tests/fixtures/` holds only three morph JSON files — there are
  **no** state, `.ats`, `.atspreset`, WAV, Smearnov, MathExpr, or undo fixtures,
  contrary to spec §2.1 and commit card 01).
- Prove malformed/truncated input never partially mutates state at the host
  boundary, not just at the codec.

### P0-2 — Synthesis parameters do not reach the renderer (spec §7.2, §6.4)

`HostAdapter::previewSynthParameters` (`HostAdapter.h:1331-1339`) sets only
`partialsGain` and `residualGain`; every other `SynthParams` field keeps its
default. `AtsEngine::render` is therefore called with `freq = 1`,
`timeStretch = 1`, no start/duration, no phase flag, no IFFT flag.

Also missing: frequency shaping (`AtsFrequencyShape`), timbre transform,
`TimbreMotionPlan`, morph recipe evaluation, and the MIDI transpose factor
(`semitoneOffset()` is exposed but never multiplied into `freq`).

Required: a single owned "compile render inputs from parameter store + engine
state" function, used by preview, bounce, and any offline render, with a golden
audio matrix per spec §14.2.

### P0-3 — Real-time rule violation in `process()` (spec §5.1, §6.2, DoD "audio callback is allocation-free…")

`HostAdapter.h:1040` calls `parameters_.read("outputTrim")` on the audio thread.
`ParameterStore::read(string_view)` → `ParameterRegistry::indexOf` →
`std::find_if` with `std::string == std::string_view` comparison across all 81
descriptors (`src/core/ParameterRegistry.cpp:266-280`). The spec is explicit:
"Resolve IDs to dense indices during initialization; never scan strings or hash
IDs in the audio callback."

Required: resolve dense indices in `prepare()`; index-only reads in `process()`;
add the allocation/lock/string guard test the spec's §14 ladder calls for.

### P0-4 — Playback layering does not match the spec (spec §6.3)

`process()` reads one pre-rendered buffer and applies output trim. There is no
partials / residual / combined three-layer publication, no `Both`
vs `PartialsOnly`/`ResidualOnly`/`SmearOnly` mixing on the audio thread, and no
`classicPreviewMode` (Classic combined vs Layer mix) branch. Layer choice is
currently baked in at render time by zeroing a gain.

Also: the smoothed `partGain`/`resGain` ramps required by `DSP-PLAY-*` are
applied pre-render, not per-sample, so the 20 ms ramp contract is not observable
in the output. This is either an implementation gap or an unrecorded deviation.

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

**Phase A — make the plugin functional (P0-1, P0-2, P0-3, P0-4)**

1. Dense-index the parameter store at `prepare()`; remove string lookups from
   `process()`. Add the RT guard test.
2. Add `HostAdapter::captureState()` / `applyState()` over `LegacyStateCodec`;
   wire VST3 `getState`/`setState`, AU class-info, and standalone.
3. Check in v2–v17 byte fixtures plus `.ats`, `.atspreset`, WAV, morph,
   Smearnov, MathExpr, and undo fixtures; test round-trip and malformed input at
   the host boundary.
4. Build the render-input compiler (all `SynthParams` fields + frequency shape +
   timbre + morph + MIDI transpose). Add the §14.2 audio golden matrix at
   44.1/48/88.2/96 kHz.
5. Implement three-layer publication and audio-thread layer mixing with smoothed
   `partGain`/`resGain`/`outputTrim`, including `classicPreviewMode`.

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
13. Port page by page against the §23 copy/geometry contracts, writing the
    surface record before the code.
14. Make `SpectrumRenderer` branch on all seven modes; paint `partialTrace` and
    `selectedPartial` in `VisageEditor`; add the LUT/dust/glow/ridgeline/
    isometric layers and the cache/revision rules from §22.3.
15. Build the §22.1 menu tree, navigation, and the SP-* gesture set.
16. Native dialogs, focus restoration, click-through overlays, teardown.

**Phase D — evidence and removal (card 31–35)**

17. Deterministic screenshot matrix; live dispatch matrix in standalone plus one
    DAW; release/compatibility matrix.
18. Delete `src/plugin`; widen the no-JUCE scan to the whole tree including
    tests and tools; clean AppleDouble files and repair the `.git` pack
    sidecars; update `NOTICE`, `THIRD_PARTY.md`, `BUILDING.md`, `RELEASE.md`,
    ADRs, CI.
19. Promote ledger rows to `verified`/`deviated`/`retired` and refresh
    `JUCE_REMOVAL_TASKS.md` status.

## 7. Definition-of-done scoreboard (spec §17)

| Item | State |
|---|---|
| No production source or CMake target depends on JUCE | build graph clean; `src/plugin` deleted 2026-08-06 |
| 81-parameter independent golden passes | pass |
| State v2–v17 load, new state round-trips | v18 codec passes; wired to `captureState()` / `applyLegacyStateToParameters()`; timeline, onset overlay, timbre JSON in state |
| Factory and user presets round-trip | factory pass; user presets use full LegacyState v18 snapshot; old parameter-only format decoded as fallback |
| `.ats` import/export and embedded model round-trip | import/export ok; embedded model not in state |
| Core/audio goldens within tolerance | core pass; end-to-end render path incomplete |
| Qatsh patch guards active | pass (20/20) |
| Audio callback allocation/lock/string-free | **fails** (string lookup in `process`) |
| VST3 identity/buses/MIDI/latency/tail/state | identity ok; **state fails** |
| AU + standalone load on macOS | artifact + `auval` ok; arm64 GUI probes trap |
| Both themes at all sizes/scales | not proven |
| Every widget class passes real-dispatch | AtsSlider/Toggle/Combo/Segmented/TextField/Tooltip/PageContainer in `VisageWidgets.h`; `ats_widget_dispatch_test` passes |
| Every non-parameter action has typed command + result | typed yes; all 5 model-edit actions implemented; timeline/timbre/onset payloads upgraded to source-contract types |
| Popups/tooltips/text/focus/resize/dialogs live tested | partial harness only |
| Spectrogram/timeline/Smearnov/partial/residual/MathExpr edits + undo | Smearnov only |
| Standalone + one DAW exercised at release hash | not done |
| Coverage matrix has no unowned contract | 28 UI rows unowned |
| Deviation register approved/closed | 8 deviations recorded (DEV-SVCL-001 – DEV-SVCL-008); HostAdapter as single production owner |
| Clean-checkout build and tests pass | pass (79/79 headless) |
| Repository scan and license/NOTICE audit | scan widened to full tree (183 files, 0 violations); NOTICE/THIRD_PARTY not yet updated |

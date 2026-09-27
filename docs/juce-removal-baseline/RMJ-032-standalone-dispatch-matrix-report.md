# RMJ-032: Standalone live dispatch matrix (audit T-202)

**Date:** 2026-09-27
**Revision:** `4df40c4b`
**Artifacts:** `ATS Resynth.app` from `build-arm64-ui` (arm64) and
`build-plugin` (x86_64)
**Evidence:** `evidence/RMJ-032-dispatch-matrix-arm64.jsonl`,
`evidence/RMJ-032-dispatch-matrix-x86_64.jsonl`

## Method

`ATS Resynth --dispatch-matrix <log.jsonl> <revision>` runs the real
standalone composition: `HostAdapter`, the production `VisageEditor` in a
native window, and CoreAudio default output. It drives the editor with native
`NSEvent` mouse and keyboard input at the 1280×940 design layout (click
targets measured from the RMJ-031 screenshot matrix). Every 20 ms it pumps
analysis polling and editor snapshots, as the app's own timer does. The audio
device runs in *silent* mode: the HAL callback renders the host, but the
speakers receive zeros.

Each step writes one JSON line: the step name, the expectation, pass/fail,
every `EditorAction` the editor emitted (preview actions carry
source/layer/start, parameter actions carry id/begin/end), editor sink events
(page, theme, popup, history, focus), the host error code, and telemetry
afterwards. The same run is the `windowed` ctest
`ats_resynth_standalone_dispatch_matrix`.

## Results: 21/21 on arm64 and x86_64

| # | Step | Emitted / observed |
|---|---|---|
| 0–4 | Page tabs SESSION…MATH | `page:0..4` |
| 5 | Slider drag (analysisLowestFreq) | `SetParameter(begin)`, 2 × move, `SetParameter(end)` |
| 6 | Combo popup (Window type → Hanning) | popup, `SetParameter(analysisWinType)` = 2 |
| 7 | Toggle (usePhase) | `SetParameter(usePhase)` |
| 8 | Segmented (Both playback → Layer mix) | `SetParameter(classicPreviewMode)` = 1 |
| 9 | Theme popup → Porcelain → Ember | `popupShown`, `theme:1`, then back to Ember |
| 10 | Presets popup → Bright partials | `ApplyFactoryPreset`, partGain 1.6 |
| 11–12 | Header UNDO / REDO | `history:undo` / `history:redo`, value reverted and re-applied |
| 13 | Import current source (accepted open-panel result) | `ImportWav(slot 3)` → `readyToAnalyze` |
| 14 | ANALYZE CURRENT then CANCEL | `AnalyzeCurrent`, `CancelJobs`; result discarded (latency ~0.5 s) |
| 15 | ANALYZE CURRENT to completion | `analyzed`, current model present |
| 16–17 | SESSION PLAY / STOP | `Preview(source=3, start/stop)`; device callbacks render non-zero audio while playing |
| 18 | MATH: type script, APPLY SCRIPT | `ApplyMath` with the typed text |
| 19 | Resize via host seam | 1100×860 and 1900×1400 adopted |
| 20 | Teardown | audio stopped, editor detached |

## Defects the matrix exposed (all fixed, with regression tests)

| Defect | Fix |
|---|---|
| The standalone never opened an audio device, so preview was never audible (`host_not_prepared`) | `4df40c4b` `StandaloneAudioOutput` (CoreAudio default output) |
| `AnalyzeCurrentAction` analysed morph source A: `cb8b6423`'s fix was reverted by `43c96dc9` | `f41a0240`, `host_current_model_workflow_test` |
| ANALYSIS IMPORT WAV targeted source A, not the current model | `f41a0240` |
| Current-model analysis completion previewed slot 0 (`missing_model`) | `f41a0240` |
| ANALYSIS CANCEL was dead (page read a flag nothing set); ANALYZE stayed enabled during analysis | `f41a0240` |
| MATH APPLY SCRIPT ignored typed text until Return was pressed | `f41a0240` |
| Host telemetry never carried the live preview state / playhead | `f41a0240` |

Revert scan: the 40 commits before `43c96dc9` were checked for added lines
that disappeared in it. Apart from the `AnalyzeCurrent`
routing, the only other fix commit affected (`82f664aa`, AU editor lifetime) was
refactored, not reverted: its guard lives in the weak-pointer lifetime registry
and the AU probe still exercises it.

## Not covered here

- A real DAW session (T-204, Logic) needs a person at the machine.
- The native open panel itself: the matrix delivers the panel's accepted
  result as the same typed action. `native_file_dialog_contract_test` covers
  the panel seam.
- Standalone MIDI input and device selection. The legacy standalone holder had
  both; the new output engine uses the default device and has no MIDI input.

# ATS Resynth: current status

**This is the single live status document.** Older status sources
(`JUCE_REMOVAL_TASKS.md` §"Current execution status", `JUCE_REMOVAL_PARITY_AUDIT.md`,
`JUCELESS_PARITY_COMPLETION_SPEC.md` §1, `REPO_AUDIT_2026-09-18.md`) are
historical and carry a banner pointing here. Update this file in the same
commit as any change that moves a ledger row or closes a task.

**As of:** 2026-09-27, branch `remove-juce`. Evidence was produced at
`f2057a51` and ledgers promoted at `f708d883`.

## Verdict

JUCE is gone from the build and the product runs end to end on arm64, the
architecture Logic loads. The remaining gates need a person or an account
action:

1. a Logic session with the arm64 AU (T-204), and `auval` / the VST3
   validator on installed artifacts (ARCH-HOST-001);
2. GitHub Actions billing: CI jobs stopped starting after `9ace67e8` with
   "recent account payments have failed or your spending limit needs to be
   increased"; everything after that is verified locally only.

## Suites (local)

| Tree | Arch / config | Result |
|---|---|---|
| `build-arm64-ui` | arm64, wrappers + Visage UI probe + windowed | 108/108 (`-LE perf`) |
| `build-plugin` | x86_64 (Rosetta) wrappers | 101/101 |
| `build-review-ui` | x86_64 UI probe | 100/100 |
| `build-macos-arm64` | arm64 release artifacts | 98/98; `verify_macos_artifacts.sh` all gates |
| `build-asan` / `build-tsan` | arm64 core, `scripts/run_sanitizers.sh` | 93/93 each |

The `cmake` on this machine is an x86_64 binary: trees without
`CMAKE_OSX_ARCHITECTURES=arm64` build x86_64 under Rosetta.

## Ledgers

| Ledger | verified | implemented | deviated | blocked |
|---|---|---|---|---|
| Architecture (`tests/architecture/ats_juce_replacement_traceability.json`) | 30 | 2 (ARCH-HOST-001, DSP-METER-001) | 8 (DEV-SVCL) | 0 |
| UI (`tests/ui/ats_ui_parity_manifest.json`) | 19 | 31 | 0 | 3 (ADR-0004 P1 deferrals) |

## Audit tasks (`REPO_AUDIT_2026-09-18.md` §5)

| Task | Status | Where |
|---|---|---|
| T-001 fileMode + ignores | done | `47b7624f` |
| T-002 governing docs tracked | done | `43c96dc9` |
| T-003 commit the tree | done: one bulk commit (not sliced); it silently reverted one committed fix (`AnalyzeCurrent` routing), restored in `f41a0240` | `43c96dc9`, `39c82d22` |
| T-004 push | done | `origin/remove-juce` |
| T-005 CI covers branch + UI config | done (green through `9ace67e8`; now blocked by billing) | `fbdda346`, `ade20e1f` |
| T-006 AppleDouble cleanup | done | `scripts/clean_appledouble.sh` |
| T-101/102/103 red tests | done | `70538689`, `326d900b`, `438895dc` |
| T-104 CI gate | done (`-LE 'windowed\|perf'`) | `0cf0e136` |
| T-201 screenshot matrix | done | RMJ-031 |
| T-202 standalone live dispatch | done | RMJ-032 |
| T-203 arm64 windowed GUI | done except `auval` (needs install) | RMJ-031 §arm64 |
| T-204 Logic session | **open: needs a person** | — |
| T-205 differential wrapper matrix | done | `ats_vst3_differential_test` |
| T-301 release matrix | done | RMJ-100 |
| T-302 ledger promotion | done where evidence exists | `f708d883` |
| T-303 docs consolidation | done | this file |
| T-304 final packet / clean-clone build | packet written; clean clone builds; release gate not passed (open gates listed) | RMJ-110 |
| T-401 theme persistence | done (per-user preference) | `4cb22409` |
| T-402 copy/focus/keyboard parity | partial: preset names, toggle captions, §7.2 widgets, legacy `synDur` FULL / pivot Auto readouts; spec quoted-copy scan clean except §8.2 timeline readout | see open items |
| T-403 factory preset values | done | `ff52db63` |
| T-404 resize negotiation | done | `6ab26a68` |
| T-405 keyboard / output spectrum / meter | out of parity scope (ADR-0004), not started | — |

## Spectrum display review (2026-11-10)

The Visage spectrum painter was compared against the deleted JUCE
`SpectrogramPanel.cpp`. Port regressions fixed: playhead, onset markers and
Smearnov curves ignored the time zoom; frequency labels ignored the frequency
zoom; band labels were evenly spaced instead of on their bands; partials lost
their per-point alpha ramp; isometric partials and residual were not culled
(off-range data projected beside the floor) and had no depth cue; residual had
no filled ridge; envelope bins could take a negative weight at the top edge,
RDC had no edge taper; the envelope edit range ignored the viewport; footer,
hover readout and frequency-shape pivot were missing. One legacy defect fixed
on purpose: residual columns now cover the whole sound (tail columns were
silent). Details: `docs/spectrum-view-parity-decisions.md` §2026-11-10. The
spectrum screenshot matrix (RMJ-031) must be re-captured.

2026-11-11: the SESSION spectrum spans the centre and right columns, views
are one-click buttons, and a selected view no longer snaps back to Trace +
Residual (the host published that mode on every snapshot). See
`docs/spectrum-view-parity-decisions.md` §2026-11-11.

## ATSA engine fixes (2026-11-21, from the pyats review)

- `qatsh-atsa-partial-free-bands.patch`: the default combined synthesis
  dropped the residual of critical bands holding no active partial
  (`crotales.ats` played 40% of its residual energy; now 101%).
- `qatsh-atsa-trim-segment-average.patch`: `trim_partials` judged each short
  segment on a running SMR average over earlier segments.

Both are behavior-changing; see `patches/README.md`. Re-render any saved
comparison audio made with the default (oscillator) synthesis.

Evaluated and **not adopted** (resynthesis scored against the original with
pyats's multi-resolution spectral loss on the four `spectral-envelope-audio`
recordings, ATSA/plugin default analysis settings):

- pyats's *sinusoidality gate* (drop peaks whose time-reassignment offset
  exceeds a fraction of the window). At pyats's 0.02 it raised the loss by
  10-34% (1.6-20% with 40 ms windows); at 0.05-0.10 it was neutral within
  about 2% except the crotale, whose decaying partials it discards (+2.6% to
  +11%). Prototyped in ATSA and reverted.
- pyats's *rule-based analysis-settings suggestion* (lowest frequency = YIN
  pitch, highest = 1-2x spectral rolloff, class-based window/hop): -19% on
  the guitar but +12%, +21% and +135% (crotale, pitch read as 1367 Hz) on the
  others. Per-sound settings do matter (lowest frequency 100 Hz alone gave
  -12% on the vowel and -21% on the guitar), so an analysis-by-synthesis
  search warm-started from the current settings is the better route.

## Analysis auto-tune (2026-11-22)

ANALYSIS ▸ AUTO-TUNE runs an analysis-by-synthesis search
(`src/engine/AnalysisAutoTune`, adapted from pyats `autotune.py`) over the
lowest / highest frequency, window cycles and hop size. It starts from the
current settings, scores each candidate's resynthesis against the input with
a multi-resolution spectral loss plus a small per-partial penalty, keeps a
change only when it improves the score, and never raises the lowest frequency
above 0.9x the starting model's lowest strong partial (so a fundamental is not
pushed into the residual). The result is written as one undoable change and
the current model is re-analysed; CANCEL stops it. On the four
`spectral-envelope-audio` recordings (40 evaluations, 0.5-15 s): guitar -42%,
crotale -33%, vocal -14%, vowel phrase -53% loss. `tools/ats_autotune` runs it
on a WAV. Tests: `analysis_autotune_test`, `host_autotune_test`, and the
standalone dispatch matrix's AUTO-TUNE step.

Fixed alongside: in the VST3 build, ANALYZE CURRENT sent slot 0, so it analysed
morph source A instead of the current model (the host path had been fixed in
`f41a0240`; the VST3 controller and processor still used 0/1 only).

## Partial selection tools (2026-11-23)

The partial views' toolbar gains **Select...** and **Invert** (adapted from
pyats `selection.py`). Select... opens a panel over the spectrum: frequency
range (Hz), minimum SMR (dB), minimum level (dB) and duration range (s), each
optional; SELECT replaces the selection, ADD unites with it. Matching uses
whole-partial summaries carried on each `SpectralTrace` (mean frequency, mean
level, active seconds, mean SMR). Mean SMR comes from the analysis or, for
imported `.ats` files (which store none), ATSA's masking model per frame,
cached per model revision. Tests: `partial_selection_criteria_test`,
`host_partial_smr_test`, `spectrum_chrome_test`.

## Time edits and automation curves (2026-11-23)

- **Time edits** (adapted from pyats `clipboard.py`): in the planar views a
  lasso also sets a time range (shaded). Toolbar **Time...** offers copy,
  cut, delete, crop and paste at the playhead with or without a 2-frame
  crossfade (`TimeEditAction`, `src/core/AtsTimeEdit`). Paste inserts; seams
  crossfade amplitude (equal power), band energy (linear) and frequency only
  where both sides are active. Edits are undoable and clear the frame-aligned
  Smearnov overlays. Tests: `ats_time_edit_test`, `host_time_edit_test`.
- **Automation curves** (pyats `timewarp.py`, `pitchmap.py`,
  `spectralwarp.py`): time warp (output -> analysis position, same length),
  pitch (+/-24 st) and spectral warp (gamma 0.5-2 about the frequency-shape
  pivot or the estimated fundamental), all over output time
  (`src/core/AtsAutomationCurves`). Applied to the render copy after the
  static frequency shape; phases and slopes are dropped while active.
  `SetSoundCurveAction`; state v20 carries them. Tests:
  `ats_automation_curves_test`, `host_sound_curves_test` (440 -> 879 Hz at
  +12 st), `legacy_state_codec_test`.
- **SESSION timeline node editor** (spec §8.2): tabs MORPH / PITCH / TIME /
  WARP, RESET, click to add and drag a point, double-click to remove, the
  `"{t}s  {value}"` drag readout, axis labels; clicking the time strip seeks.
  MORPH edits send typed `SetMorphTimelineAction` points. `CurveEditorModel`
  holds the logic; test `curve_editor_model_test`. Not yet done from §8.2:
  curve shapes per segment and BOUNCE.
- Known limitation (pre-existing): typed morph-timeline points are not saved
  in the state chunk (only their amounts), so after a reload the MORPH curve
  shows its amounts evenly spaced.

## Open items

- Logic session (T-204) and plugin validation (ARCH-HOST-001).
- `DSP-METER-001`: `tests/ui/spectrum_real_dispatch_test.cpp` does not exist.
- State corpus from a legacy JUCE binary (the current corpus is produced by
  the byte-compatible codec).
- Standalone: device selection and MIDI input (legacy holder had both).
- `assets/` and `slider/` (UI design assets, ~80 MB, untracked and
  unreferenced): decide whether they belong in the repository.
- T-402 remaining: tooltip text and keyboard navigation audit against
  `ats-mearcstapa-ui-spec.md`.
- SESSION timeline (spec §8.2): node editor done (see 2026-11-23); per-segment
  curve shapes and BOUNCE remain, and typed morph points are not persisted.

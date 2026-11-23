# ATS Resynth: current status

**This is the single live status document.** Older status sources
(`JUCE_REMOVAL_TASKS.md` §"Current execution status", `JUCE_REMOVAL_PARITY_AUDIT.md`,
`JUCELESS_PARITY_COMPLETION_SPEC.md` §1, `REPO_AUDIT_2026-09-18.md`) are
historical and carry a banner pointing here. Update this file in the same
commit as any change that moves a ledger row or closes a task.

**As of:** 2026-11-24, branch `remove-juce`. Evidence was produced at
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
  holds the logic; test `curve_editor_model_test`.
- Follow-up (same day): shift-click a MORPH point to cycle the easing of the
  segment it starts (linear / ease in / ease out / ease in-out, drawn with
  `applyMorphCurve`'s formulas); BOUNCE saves the morph render of the current
  audition layer as WAV (`NativeFileDialogKind::bounceWav` ->
  `BounceMorphTimelineAction`); state v21 saves the typed morph points (time,
  amount, kind, easing), which used to be reduced to evenly spaced amounts on
  reload. Tests: `curve_editor_model_test`, `legacy_state_codec_test`,
  `host_sound_curves_test`, `preset_file_workflow_test`.

## Standalone audio device and MIDI input (2026-11-23)

The legacy JUCE standalone holder had both; the native app now does too.
An AUDIO header button (standalone only, via `VisageEditor::setAudioMenu`)
lists output devices ("System default" follows the macOS default through the
default-output unit; a named device is pinned through AUHAL) and MIDI inputs
(all sources, none, or one). Choices persist in `NSUserDefaults`
(`AudioOutputDeviceUID`, `MidiInputMode`, `MidiInputSourceID`); a saved device
that is gone falls back to the system default for that launch. Switching
devices stops the preview and re-prepares the host at the new rate.
`StandaloneMidiInput` decodes CoreMIDI UMP note on/off and CC 120/123 into an
SPSC queue drained by the render callback into `HostAdapter::process()`, the
same path as the VST3 event input (notes transpose the next render around
middle C). Test: `standalone_audio_midi_test` (menu rows and id decoding,
MIDI decoding, queue order/overflow, all-notes-off on selection change). Not
verified by hand: the popup itself and a real MIDI keyboard.

## Research-corpus features (2026-11-24)

A review of `/Volumes/Extreme SSD/Research` (the user's spectral/timbre paper
corpus) against the plugin produced the additions below. Every default
reproduces the previous render bit for bit; the state chunk is **v22** and the
registry grows from 81 to **93 parameters** (`release_matrix_test`,
`parameter_registry_test`, `host_lifecycle_test` and both artifact tests carry
the new contract). Suites after the change: `build-core-arm64` 116/116,
`build-arm64-ui` 121/121 (`-LE 'windowed|perf'`).

Analysis (ANALYSIS page, two new toggles; `AnalysisParams`, `AtsEngine::analyze`):

- **Reverse-time tracking** (Serra 1997): the tracker sees the reversed signal
  so its guides are locked before they reach the attack; the model is flipped
  back (`AtsResidualFixes::reverseFrames`), phases dropped.
- **Clamp residual to source** (Serra 1997): per frame and Bark band the
  residual energy is capped at the source's own band energy, measured with the
  same N / block / binning as ATSA's `residual-analysis.c`
  (`AtsResidualFixes::clampResidualToSource`).
- **Auto-tune residual terms** (`AnalysisAutoTune`): the score adds Serra's
  residual whiteness (lag-1..63 autocorrelation of input − phase-locked
  partials render; noise 0.018, tone 0.69) and Jehan/Johnston per-band
  tonality (SFM_dB / −60; noise 0.04, tone 0.90), weights 0.15 each. On the
  four `spectral-envelope-audio` recordings they changed no decision at a
  budget of 12; they only bite in close calls. `tools/ats_autotune
  --no-residual-terms` reproduces the old score exactly.

Render (SYNTHESIS page, CHARACTER and ANIMATION groups; `SynthParams`,
`AtsEngine::render`, applied on a copy after the automation curves):

- **Keep character when transposing** + **transpose tilt** (Serra & Bonada
  DAFx-98; O'Leary & Griffith DAFx-24): the global frequency factor (knob and
  MIDI) is realised in the model domain, amplitudes are re-read from each
  frame's True Envelope so formants stay put, a tilt of N dB/oct per octave
  of transposition is applied about 1 kHz, and partials crossing 0.95 Nyquist
  fade out (`AtsTransposeCharacter`). Covers the "formant-preserving pitch"
  gap in the feature matrix.
- **Vibrato depth / rate scale** (Herrera & Bonada DAFx-98): carrier = moving
  median plus one-period mean, modulation scaled 0..2 and resampled in rate;
  the amplitude ripple of a partial moving through formants is removed with
  the vibrato rather than kept as tremolo. `analyseVibrato` reports rate,
  depth and extent with Kaminskyj's 20-cent threshold (`AtsVibrato`; test:
  5.5 Hz ±40 cents read as 5.50 Hz / 40.0 cents).
- **Animation shimmer / jitter / rate** (Jensen's Timbre Model; Torvmark):
  low-pass Gaussian AM (dB rms) and FM (cents rms) with a shared + independent
  split (correlation 0.3) and emphasis on the three lowest partials
  (`AtsMicroModulation`).
- **Residual density** (Hanna DAFx-00/01): 1 = ATSA's single carrier per Bark
  band (unchanged); 2..32 render the whole residual in the plugin with N
  jittered carriers per band at 1/√N amplitude each (`AtsResidualSynth`;
  RMS within 6% of the ATSA reference at N = 1, 4, 16; out-of-band leakage
  −26 → −61 dB). Note Hanna's fluctuation effect is bounded by the band's
  BT product; the audible change is mainly smoothness and leakage.

Model tools (partial-view toolbar **Tools...**, each one undoable
`SpectrumEditCommand`; VST3 forwards them through the existing spectrum-edit
message):

- Smooth harmonic envelope (Klapuri 2003, `AtsSpectralSmoothness`): two-sided
  triangular mean of the octave neighbours in (log2 h, dB); a +20 dB outlier
  returns to within 3 dB of the 1/h line, unboosted partials move < 0.5 dB.
- Rough voice / growl sub-harmonics (Loscos & Bonada DAFx-04,
  `AtsSubharmonics`): octave-down copies with 3% period and 3 dB amplitude
  jitter up to 8 kHz, or the f0·(m + k/4) series.
- Clamp residual to source, high-pass residual below 300 Hz (Serra), remove /
  double vibrato, reverse time.

Smearnov and morph:

- **Transientness weight** (Molla & Torrésani, `AtsTransientness`): a sixth
  detection stream from the source audio, the ratio of the sparsity-based
  size estimates of a DCT-II (local cosine) and Daubechies-4 expansion per
  frame (clicks 0.996, steady sine 0.0002, white noise 0.65). With the weight
  on, each onset's blur/sharpen/exaggerate amount is also scaled by its index
  so tonal onsets are treated gently. Weight 0 is bit-identical.
- **Harmonic locked** correspondence (Hatch 2004 §4.4; choice 4 of
  `morphCorrespondenceStrategy`, `AtsMorphHarmonicCorrespondence`): partials
  labelled by harmonic number, matched within a semitone after pitch
  normalisation, unmatched ones paired with silence; static 100 Hz grid when
  no f0 is reliable.
- **Morph onset-aligned time warp** (Hatch 2004 §4.2, `AtsOnsetAlignedWarp`):
  onsets of A and B (Smearnov detector) are moved to α·(tB − tA) + tA with a
  5 ms unwarped attack hold and a 4:1 slope clamp, applied as same-length
  time maps on the prepared morph sources; α is the fixed morph amount.
- **Select... ▸ Min activity** (O'Sullivan & Fernström ICAD 2003): fraction of
  active frames with positive amplitude curvature, carried on `SpectralTrace`.

Descriptors: `AtsDescriptors` (centroid Hz/Bark, spread, skewness, kurtosis,
tilt, Serra/Bonada f0 and harmonic distortion, inharmonicity, Menon's
harmonic-rank centroid, tristimulus, odd/even, noisiness, per-band centroids,
attack centroid) and `tools/ats_describe <file.ats>` (CSV per frame plus a
summary footer). Not yet surfaced in the UI.

Tests added: `ats_transientness_test`, `ats_residual_fixes_test`,
`ats_residual_synth_test`, `ats_micro_modulation_test`, `ats_vibrato_test`,
`ats_descriptors_test`, `ats_transpose_character_test`,
`ats_spectral_smoothness_test`, `ats_subharmonics_test`,
`morph_harmonic_correspondence_test`, `ats_onset_aligned_warp_test`; extended:
`partial_selection_criteria_test`, `analysis_autotune_test`,
`legacy_state_codec_test` (v22), `spectrum_chrome_test` (Tools... button).

Review fixes (same day, after `fc56e877`): the residual clamp and the
transientness index read the source from the analysis start sample (ATSA frame
times are relative to it) and are skipped once a time edit or Reverse time has
re-timed the model (`ImportedSource::modelAlignedToAudio`; an undo does not
restore the flag, so the features stay off for that model until re-analysis);
the harmonic-locked f0 is a harmonic sieve instead of a per-frame median, which
read ~2 kHz on the host's one-entry-per-partial input; v22 doubles and the
residual density are validated on encode/decode; an auto-tune candidate whose
partials-only render fails now scores infinity. Tests: `ats_engine_test`
(offset clamp; fails before the fix), `morph_harmonic_correspondence_test`,
`legacy_state_codec_test`.

Not verified by hand: the Tools... popup, the new SYNTHESIS rows (the left
column is now taller than the panel at small window sizes; overflow rows hide),
and the audible result of each render option. Housekeeping done in the research
folder: the two dead `repos/` symlinks now point at the SSD checkouts and
`Feature_Matrix.md` was refreshed from the vault copy.

## Open items

- Logic session (T-204) and plugin validation (ARCH-HOST-001).
- `DSP-METER-001`: `tests/ui/spectrum_real_dispatch_test.cpp` does not exist.
- State corpus from a legacy JUCE binary (the current corpus is produced by
  the byte-compatible codec).
- `assets/` and `slider/` (UI design assets, ~80 MB, untracked and
  unreferenced): decide whether they belong in the repository.
- T-402 remaining: tooltip text and keyboard navigation audit against
  `ats-mearcstapa-ui-spec.md`.
- SESSION timeline (spec §8.2): done, including segment easing, BOUNCE and
  saved morph point times (see 2026-11-23).

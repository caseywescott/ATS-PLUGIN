# RMJ-100: Release / compatibility matrix (card 33, audit T-301)

**Date:** 2026-09-27
**Frozen revision:** `f2057a51` (branch `remove-juce`)
**Machine:** Apple M1 Pro, macOS 14 (Darwin 23.6). The `cmake` binary is
x86_64 and runs under Rosetta, so arm64 trees set `CMAKE_OSX_ARCHITECTURES=arm64`
explicitly.

## Summary

| Area | Command / test | Result |
|---|---|---|
| Full suite, arm64 (the shipping arch, incl. windowed GUI + wrappers) | `ctest --test-dir build-arm64-ui -LE perf` | 108/108 |
| Full suite, x86_64 plugin config | `ctest --test-dir build-plugin -LE perf` | 101/101 |
| Full suite, x86_64 UI-probe config | `ctest --test-dir build-review-ui -LE perf` | 100/100 |
| Core, arm64 | `ctest --test-dir build-macos-arm64 -LE perf` | 98/98 |
| ASan + UBSan (arm64, core, RelWithDebInfo) | `build-asan`, `ctest -LE 'perf\|windowed'` | 93/93 after fixes (was 85/93) |
| TSan (arm64, core) | `build-tsan`, same | 93/93 after fixes (was 80/93) |
| State corpus v2–v19 | `legacy_state_codec_test` | every version encodes and decodes; v19 fields round-trip; older versions default them |
| 81-parameter edges | `release_matrix_test` | 162/162 min/max cases survive save/reopen byte-identically (16 were lost before) |
| Sample rate × block × channels × MIDI | `release_matrix_test` | 56/56: 44.1/48/88.2/96 kHz × 1/16/64/480/512/1024/4096 × stereo/mono with in-block note on/off; finite, bounded, audible |
| Factory presets | `factory_preset_state_test` | 12 × 81 values match the legacy recipes; untouched parameters preserved |
| User/ATS presets and model round trips | `preset_file_workflow_test`, `parameter_preset_codec_test`, `ats_roundtrip`, `file_service_preset_test` | pass |
| VST3 vs AU/standalone state | `ats_vst3_differential_test` | 18-step script byte-identical (2 payload truncations fixed) |
| Qatsh patch probes | `ats_qatsh_patch_manifest` | 24/24 patches and source markers |
| Qatsh reproducibility | fresh clone @ `5515648` + `apply_qatsh_all_patches.sh` | byte-identical to the local ATSA tree |
| Artifact identity (arm64, ad-hoc) | `scripts/build_macos_artifacts.sh --arch arm64 --sign -` then `scripts/verify_macos_artifacts.sh` | all gates pass: slice sets, plists, AU `aumf`/`Atr1`/`AtPl`, signatures, no absolute dylib paths, versions 0.1.0, focused ctest 3/3 |
| Wall-clock perf budget | `spectrum_renderer_perf_test` (label `perf`) | passes on arm64; flaky under Rosetta and on shared CI VMs, so excluded there |
| CI | GitHub Actions `remove-juce` | green through `9ace67e8`; later pushes were not started because of an account billing/spending limit, not code |

## Defects the matrix found and fixed

| Defect | Found by | Fix |
|---|---|---|
| Frequency-shape mode/amount/pivot reset on every project reload (restore read a field the chunk never carries) | parameter-edge sweep | `f14da05b` |
| Timbre ×3, `useIfftSynth`, `analysisMultiRes` never persisted (inherited from the legacy chunk) | parameter-edge sweep | `f14da05b` (state v19) |
| Residual cubic phase: `int` overflow of `buffer_size³` and `i³` (UB) for frames over 1290 samples | UBSan | `df166e9b` (`qatsh-atsa-cubic-phase-overflow.patch`) |
| vDSP FFT setup cache: unsynchronized read racing the locked write | TSan | `df166e9b` (`qatsh-atsa-vdsp-setup-atomic.patch`) |
| ATSA progress callbacks from parallel workers raced into callers (heap corruption) | arm64 CI SIGTRAP, stress run | `07b7dab9` |
| Onset peak on a flat attack chosen by rounding noise (frame 26 on x86_64, 27 on arm64) | arm64 CI | `6bdd9b5e` |
| TIMBRE values shown as `2.2209e-07` on arm64 | arm64 screenshot matrix | `02e75928` |

## Compatibility notes

- **State v19:** v2–v18 chunks still load. v19 chunks are refused by builds that
  read only up to v18 (forward-only versioning). The header and payload codec
  versions are tied by a `static_assert`.
- **Corpus provenance:** the v2–v18 corpus is encoded by this codec, which
  mirrors the legacy `getStateInformation` layout field by field. No state
  chunks saved by the legacy JUCE binary are checked in. Capturing a few from
  a legacy build (`build-juce-reference`) and adding them as fixtures would
  close that last gap.
- **Standalone:** now renders through CoreAudio default output. Device
  selection and MIDI input from the legacy standalone holder are not ported.

## Not in this matrix

- A DAW session (T-204): Logic insert/automation/save-reopen/bounce needs a
  person at the machine. So does `auval` on the installed component, which
  requires installing into `~/Library/Audio/Plug-Ins/Components` (replacing
  Logic's current copy).
- Sanitizer builds of the plugin wrappers (VST3/AU); the sanitized trees are
  core-only (`BUILD_PLUGIN=OFF`).

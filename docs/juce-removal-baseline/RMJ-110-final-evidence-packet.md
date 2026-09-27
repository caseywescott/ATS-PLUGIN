# RMJ-110: Final parity evidence packet (card 35, audit T-304)

**Date:** 2026-09-27 · **Branch:** `remove-juce` · **Head:** see `git log -1`
(evidence produced at `f2057a51`, clean-clone build at `b7ad2c10`, final
fixes through `812a7351`).

**Release gate: not yet passed.** The open gates are listed at the end.

## Pins and hashes

| Input | Pin |
|---|---|
| Visage | `20de59464243447816d142e9d38e9723d068f755` |
| VST3 SDK | `05c4a976…` (`cmake/ATSVst3Sdk.cmake`) |
| AudioUnitSDK | `53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9` |
| Qatsh upstream | `5515648358ffaa8d863b3f2507e03a4943d79667` + 24 patches (`patches/`, manifest 24/24) |
| libsndfile (arm64 bundle) | `vendor/sndfile-arm64` |
| State format | ATSP v19 (reads v2–v19) |

## Evidence index

| Evidence | Where |
|---|---|
| Screenshot matrix (5 pages × 2 themes × 3 sizes + 7 spectrum modes × 2 themes, pixel-deterministic, arm64 + x86_64 manifests) | RMJ-031, `screenshots/` |
| Standalone live dispatch (21 native-event steps, JSONL logs) | RMJ-032, `evidence/RMJ-032-*.jsonl` |
| Release / compatibility matrix, sanitizers, artifact identity | RMJ-100 |
| VST3 vs direct-host differential | `tests/host/vst3_differential_test.cpp` |
| Ledgers | architecture 30 verified / 8 deviated / 2 implemented; UI 19 verified / 31 implemented / 3 blocked |
| Current status and task closure | `docs/STATUS.md` |

## Clean-checkout build (T-304)

A clone of the committed branch (`b7ad2c10`, identical to `origin/remove-juce`)
plus a fresh Qatsh clone at the pinned commit with
`scripts/apply_qatsh_all_patches.sh`, configured arm64 with wrappers and the
Visage UI probe, fetching Visage and both SDKs from their pins:

- configure: OK; build: OK (no files outside the clone were needed);
- `ctest -LE perf`: 107/108. The one failure was
  `ats_resynth_standalone_dispatch_matrix` step 8, run while the screen was
  locked and the display asleep, so Visage wasn't rendering. The test now
  reports a ctest skip in that state (`812a7351` series) instead of failing,
  and it passes 21/21 when the window is on screen (RMJ-032).

The clone step used the local repository because the machine's GitHub HTTPS
credential helper is broken ("Failed to create CoreCLR"). The content is the
same commit as `origin/remove-juce`.

## Deviations

The 8 `deviated` architecture rows are the recorded DEV-SVCL service-ownership
decisions. The 3 `blocked` UI rows are ADR-0004 P1 deferrals (keyboard, output
spectrum, output meter). No new deviations were introduced in this cycle; the
theme-persistence decision is recorded under ADR-0004 §5.

## Open gates

1. A DAW session (Logic, arm64 AU): insert, automation write/read, save/reopen,
   preset load, editor reopen, bounce (T-204). `auval` and the VST3 validator on
   installed artifacts (ARCH-HOST-001).
2. `DSP-METER-001`: `tests/ui/spectrum_real_dispatch_test.cpp`.
3. 31 UI rows without live-dispatch evidence. The largest single gap is the
   SESSION morph-timeline node editor (spec §8.2).
4. Windowed tests need re-running with the screen unlocked after `812a7351`.
5. CI: GitHub Actions stopped starting jobs (billing/spending limit) after
   `9ace67e8`.

# RMJ-000 frozen environment

- Branch: `remove-juce`
- Source commit: `36244a3ff041491d9a8cc4f5bf641105b52c8e35`
- Host: macOS Darwin 23.6.0, arm64
- Compiler: AppleClang 15.0.0 (`clang-1500.3.9.4`)
- CMake: 4.3.3
- Python: 3.9.0
- C++ standard: C++17
- Baseline configure: `-DBUILD_PLUGIN=OFF`
- Qatsh source revision: `c164d6d96fc460644298f1641bcebd2f40e3d8da`
- Qatsh worktree: pre-existing dirty changes; no Qatsh files were changed by
  RMJ-000.
- Test build directory: `/tmp/ats-juce-removal-headless`
- UI backend revision: `Visage@20de59464243447816d142e9d38e9723d068f755`;
  enabled through the opt-in `ATS_BUILD_VISAGE_UI_PROBE` CMake target. Full UI
  parity evidence is still intentionally not claimed.

The working tree contained pre-existing modifications and untracked files,
including source, build, documentation, fixture, and AppleDouble metadata
paths. They were not reset, cleaned, or overwritten. Use `git status --short`
at the baseline commit to recover the complete path list and use
`git diff --name-status` for the tracked patch list.

Baseline identity remains the current shipping contract: ATS Resynth,
ATSPlugin, `com.atsplugin.resynth`, VST3 `AtPl`/`Atr1`, effect classification,
stereo output, MIDI input only, zero latency, zero tail, and VST3/AU/Standalone
formats.

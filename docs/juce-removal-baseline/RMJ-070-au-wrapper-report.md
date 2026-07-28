# RMJ-070 AUv2 wrapper report

Task ID: RMJ-070 / commit 22

## Contract packet

- Trace IDs: `ARCH-HOST-001`, `ARCH-HOST-002` (format projection and host
  lifecycle).
- Legacy symbols/files: the JUCE AUv2 projection, component identity,
  parameter tree, stream-format handling, and process wrapper in
  `src/plugin/CMakeLists.txt` and `src/plugin/PluginProcessor.cpp`.
- Replacement owner/API: `src/host/au/AtsAudioUnit.mm` owns Audio Unit
  dispatch, identity metadata, 81-parameter forwarding, stream format, and
  process adaptation; `ats::host::HostAdapter` remains the neutral owner of
  identity, buses, parameters, state bounds, and audio publication.
- Thread domain: AU lifecycle, property, and parameter calls remain on host
  domains; the process path forwards caller-owned float buffers to the
  prepared neutral adapter and performs no file or editor work.
- State/format impact: the component advertises ATS Resynth / ATSPlugin,
  `com.atsplugin.resynth`, `aufx` / `Atr1` / `AtPl`, no audio input, stereo
  float output, and zero latency/tail. The neutral bounded ATSP state seam is
  present, but AU class-info state callbacks are a follow-on task and are not
  claimed by this slice.
- Dependency pin: Apple AudioUnitSDK commit
  `53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9` is selected in CMake. A local
  checkout may be supplied with `ATS_AUDIO_UNIT_SDK_DIR`; otherwise the exact
  commit is fetched.
- Scope: JUCE-free AUv2 component artifact, identity, parameter list/info,
  parameter write, output format, initialization, and teardown. No CLAP
  dependency is added. AU host scan, native editor, state round-trip, and
  automation/render stress remain separate gates.

## Evidence

```text
cmake -S . -B /private/tmp/ats-au \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=OFF \
  -DATS_BUILD_NATIVE_AU=ON \
  -DATS_AUDIO_UNIT_SDK_DIR="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/cpm/AudioUnitSDK"
cmake --build /private/tmp/ats-au --target ats_au_artifact_test -j2
ctest --test-dir /private/tmp/ats-au --output-on-failure \
  -R '^ats_au_artifact_test$'
```

Result: the component built as a macOS `ATS Resynth.component`; the direct
factory probe passed. It opened and initialized the component, verified the
81-entry parameter list and a parameter write, set 48 kHz non-interleaved
stereo float output, uninitialized, and closed the component. The test uses
the real pinned Apple AudioUnitSDK factory/dispatch path and does not register
a process-global component as a substitute for an AU host scan.

This advances commit 22's AU artifact gate. It does not yet clear the full
native host gate: AU registration/`auval` scan, class-info state restore,
render/MIDI/automation stress, and a real AU editor attachment remain open.

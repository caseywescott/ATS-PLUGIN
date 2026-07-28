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
  serialized as a bounded ATSP `CFData` class-info payload; malformed or
  non-data restores are rejected before replacing the published chunk.
- Dependency pin: Apple AudioUnitSDK commit
  `53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9` is selected in CMake. A local
  checkout may be supplied with `ATS_AUDIO_UNIT_SDK_DIR`; otherwise the exact
  commit is fetched.
- Scope: JUCE-free AUv2 component artifact, identity, parameter list/info,
  parameter info/read/write across all 81 IDs, output format, bounded ATSP
  class-info state round-trip, zero-input stereo process adaptation,
  initialization, and teardown. No CLAP dependency is added. AU host scan,
  native editor, and automation/render stress remain separate gates.

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
81-entry parameter list and exercised parameter info/read/write at every ID,
set 48 kHz non-interleaved stereo float output, read and restored a valid ATSP
class-info payload, processed a 64-frame zero-input stereo block through the multiple-buffer
selector, verified silent output, uninitialized, and closed the component.
The direct harness disables only AUBase's redundant component-manager render
argument check; the real pinned AudioUnitSDK dispatch and wrapper process
callback still run. It does not register a process-global component as a
substitute for an AU host scan.

This advances commit 22's AU artifact, parameter, state, and no-input process
gates. It does not yet clear the full native host gate: AU registration/`auval`
scan, MIDI/automation stress, and a real AU editor attachment remain open.

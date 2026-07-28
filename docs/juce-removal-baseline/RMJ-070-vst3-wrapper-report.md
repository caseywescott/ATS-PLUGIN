# RMJ-070 VST3 wrapper report

Task ID: RMJ-070 / commit 21

## Contract packet

- Trace IDs: `ARCH-HOST-001`, `ARCH-HOST-002`, `ARCH-EDITOR-001` (format
  projection; editor callbacks remain a follow-on gate).
- Legacy symbols/files: the JUCE `juce_add_plugin(AtsResynth)` VST3 identity,
  bus declaration, parameter tree, state callbacks, and `processBlock` in
  `src/plugin/CMakeLists.txt` and `src/plugin/PluginProcessor.cpp`.
- Replacement owner/API: `src/host/vst3/AtsVst3Plugin.cpp` owns VST3 lifecycle,
  metadata, parameter forwarding, state streaming, MIDI conversion, and
  process-buffer adaptation; `ats::host::HostAdapter` remains the neutral
  owner of identity, buses, parameters, state bounds, and audio publication.
- Thread domain: VST3 lifecycle and controller calls stay on host/UI domains;
  `process` forwards only pre-existing stack buffers and fixed-size MIDI event
  storage to the audio-domain adapter. No file or editor work is performed in
  `process`.
- State/format impact: the wrapper advertises ATS Resynth / ATSPlugin,
  `com.atsplugin.resynth`, effect classification, no audio input, stereo audio
  output, MIDI input only, and the neutral zero-latency contract. State writes
  preserve the bounded ATSP chunk; an untouched instance emits the v17 ATSP
  header.
- Dependency pin: Steinberg VST3 SDK commit
  `05c4a9763f311a58fe3f34b68a199e5eea92e2b4` (tag `v3.7.6_build_18`) is
  selected in CMake. A local SDK checkout may be supplied with
  `ATS_VST3_SDK_DIR`; otherwise the exact commit is fetched.
- Scope: real VST3 artifact and process wrapper. No CLAP dependency is added;
  AU, standalone, Visage-to-IPlugView callbacks, host automation stress, and
  DAW scans remain separate tasks.

## Evidence

```text
cmake -S . -B /tmp/ats-vst3-arm64 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=OFF \
  -DATS_BUILD_NATIVE_VST3=ON \
  -DATS_VST3_SDK_DIR="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/cpm/vst3sdk"
cmake --build /tmp/ats-vst3-arm64 --target ats_vst3_artifact_test -j2
ctest --test-dir /tmp/ats-vst3-arm64 --output-on-failure \
  -R '^ats_vst3_artifact_test$'
```

Result: the macOS VST3 bundle built and the artifact test passed. It loaded the
module, verified factory identity and class metadata, instantiated the
controller and confirmed 81 parameters, instantiated the processor, exercised
no-input stereo bus setup, 48 kHz/64-frame setup, activate/process/deactivate
and terminate, and confirmed both output channels are silent when no playback
buffer has been published.

This clears the earlier “no pinned SDK” blocker for the VST3 slice. It does not
yet clear the full production host gate: the editor currently has no VST3
`IPlugView` bridge, AU/standalone targets are not yet implemented, and the
artifact test is not a DAW/pluginval-equivalent scan. Those claims remain open.

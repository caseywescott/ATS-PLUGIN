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
- Scope: real VST3 artifact, process wrapper, and Visage-backed `IPlugView`.
  No CLAP dependency is added; AU, standalone, host automation stress, and DAW
  scans remain separate tasks.

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

The native editor view probe uses the same bundle and a real Cocoa host window:

```text
cmake --build /tmp/ats-vst3-visage3 --target ats_vst3_editor_view_probe -j2
ctest --test-dir /tmp/ats-vst3-visage3 --output-on-failure \
  -R '^ats_vst3_(artifact_test|editor_view_probe)$'
```

Result: both tests passed. The controller created a real editor view, accepted
the native `NSView` platform, attached to a host content view, reported its
logical size, accepted focus, resized to 720x480, and removed the Visage child
without leaving a stale subview. The existing pinned-Visage probes separately
cover native mouse/text dispatch, popup focus/selection/dismissal, parameter
gestures, scale, standalone creation, and teardown.

This clears the earlier “no pinned SDK” blocker and the VST3 native editor
attachment gate. It does not yet clear the full production host gate: AU and
standalone targets are not yet implemented, the VST3 artifact tests are not a
DAW/pluginval-equivalent scan, and the full host automation/stress matrix
remains open.

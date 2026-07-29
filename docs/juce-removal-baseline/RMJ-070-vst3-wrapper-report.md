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

The native host path was also exercised under the available x86_64 macOS
runtime, which is the window-capable configuration on this desktop:

```text
arch -x86_64 cmake -S . -B /private/tmp/ats-native-default \
  -DBUILD_PLUGIN=ON -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DATS_VST3_SDK_DIR="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/cpm/vst3sdk" \
  -DATS_VISAGE_DIR="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/_deps/visage-src"
ctest --test-dir /private/tmp/ats-native-default --output-on-failure \
  -R '^ats_vst3_(artifact_test|editor_view_probe)$'
```

Result: both tests passed. The live probe demonstrates the actual
controller-created `IPlugView` path with native `NSView` attachment, focus,
logical resize, and teardown. The shared Visage probes additionally pass
native mouse/text input, focus, popup selection/dismissal, page selection, and
parameter gestures at a 1.25x scale.

```text
cmake -S . -B /private/tmp/ats-vst3-arm64 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=OFF \
  -DATS_BUILD_NATIVE_VST3=ON \
  -DATS_VST3_SDK_DIR="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/cpm/vst3sdk"
cmake --build /private/tmp/ats-vst3-arm64 --target ats_vst3_artifact_test -j2
ctest --test-dir /private/tmp/ats-vst3-arm64 --output-on-failure \
  -R '^ats_vst3_artifact_test$'
```

Result: the macOS VST3 bundle built and the artifact test passed. It loaded the
module, verified factory identity and class metadata, instantiated the
controller and confirmed all 81 parameter IDs round-trip through the controller,
instantiated the processor, exercised no-input stereo bus setup, 48 kHz/64-frame
setup, activate/process/deactivate and terminate, confirmed both output channels
are silent when no playback buffer has been published, and reopened the bounded
ATSP state stream after an explicit rewind. The same artifact test now supplies a
two-point automation queue, note-on/note-off events with sample offsets, and a
300-event input list to verify bounded MIDI conversion during process dispatch.
All 81 automation queues carry two points, so the dispatch test covers the full
host parameter-ID matrix rather than only a representative control.

The native editor view probe uses the same bundle and a real Cocoa host window:

```text
cmake --build /private/tmp/ats-vst3-arm64 --target ats_vst3_editor_view_probe -j2
ctest --test-dir /private/tmp/ats-vst3-arm64 --output-on-failure \
  -R '^ats_vst3_(artifact_test|editor_view_probe)$'
```

Result on the current arm64 desktop session: the artifact test passed, while the
real Cocoa editor view probe terminated with `SIGTRAP` during AppKit/Visage font
initialization (`XType: Using static font registry`). This is the same headless
desktop limitation observed by the AU CocoaUI and standalone probes, so it is
recorded as an open live-window gate rather than treated as native evidence.
The probe still covers the intended real host path: controller creates an editor
view, accepts the native `NSView` platform, attaches to a host content view,
reports logical size, accepts focus, resizes to 720x480, and removes the Visage
child. The existing pinned-Visage probes separately cover native mouse/text
dispatch, popup focus/selection/dismissal, parameter gestures, scale, standalone
creation, and teardown where their host environment permits.

This clears the earlier “no pinned SDK” blocker, the VST3 artifact contract,
and the live native-window gate for the available x86_64 runtime. The arm64
probe remains environment-blocked by the headless AppKit/Visage session. A
DAW/pluginval executable is not installed on this machine (`pluginval` is not
on PATH and no local binary was found), so the artifact/host-contract tests are
recorded as supplemental validation rather than a DAW or pluginval claim.

# RMJ-070 AUv2 wrapper report

Task ID: RMJ-070 / commit 22

## Contract packet

- Trace IDs: `ARCH-HOST-001`, `ARCH-HOST-002`, `ARCH-EDITOR-001` (format
  projection and Visage-backed CocoaUI editor).
- Legacy symbols/files: the JUCE AUv2 projection, component identity,
  parameter tree, stream-format handling, process wrapper, and CocoaUI view
  factory in `src/plugin/CMakeLists.txt` and
  `juce_audio_plugin_client_AU_1.mm`.
- Replacement owner/API: `src/host/au/AtsAudioUnit.mm` owns Audio Unit
  dispatch, identity metadata, 81-parameter forwarding, stream format, process
  adaptation, and `kAudioUnitProperty_CocoaUI` advertising; 
  `AtsAudioUnitCocoaViewFactory` / `AtsAuHostedEditorView` host a
  Visage-backed `NSView`; `ats::host::HostAdapter` remains the neutral owner
  of identity, buses, parameters, state bounds, and audio publication.
- Thread domain: AU lifecycle, property, and parameter calls remain on host
  domains; the process path forwards caller-owned float buffers to the
  prepared neutral adapter and performs no file or editor work. Editor attach
  stays on the UI/host view domain.
- State/format impact: the component advertises ATS Resynth / ATSPlugin,
  `com.atsplugin.resynth`, `aufx` / `Atr1` / `AtPl`, no audio input, stereo
  float output, zero latency/tail, and a Visage CocoaUI factory class in the
  same `.component` bundle. The neutral bounded ATSP state seam is serialized
  as a bounded ATSP `CFData` class-info payload; malformed or non-data
  restores are rejected before replacing the published chunk.
- Dependency pin: Apple AudioUnitSDK commit
  `53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9` is selected in CMake. Visage is
  pinned at `20de59464243447816d142e9d38e9723d068f755` when
  `ATS_BUILD_NATIVE_AU=ON`. A local checkout may be supplied with
  `ATS_AUDIO_UNIT_SDK_DIR` / `ATS_VISAGE_DIR`; otherwise the exact commits are
  fetched.
- Scope: JUCE-free AUv2 component artifact, identity, parameter list/info,
  parameter info/read/write across all 81 IDs, output format, bounded ATSP
  class-info state round-trip, zero-input stereo process adaptation,
  initialization, teardown, and Visage-backed CocoaUI editor attach/focus/
  resize/teardown. No CLAP dependency is added. AU host scan/`auval`,
  MIDI/automation stress, and runtime standalone GUI smoke remain separate
  gates.

## Evidence

```text
cmake -S . -B /private/tmp/ats-au-editor \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=OFF \
  -DATS_BUILD_NATIVE_AU=ON \
  -DATS_AUDIO_UNIT_SDK_DIR="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/cpm/AudioUnitSDK" \
  -DATS_VISAGE_DIR="/Volumes/Extreme SSD/Mearcstapa/build-ui/_deps/visage-src" \
  -DFETCHCONTENT_SOURCE_DIR_BGFX="/Volumes/Extreme SSD/Mearcstapa/build-ui/_deps/bgfx-src" \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE="/Volumes/Extreme SSD/Mearcstapa/build-ui/_deps/freetype-src" \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build /private/tmp/ats-au-editor \
  --target ats_au_artifact_test ats_au_editor_view_probe -j2
ctest --test-dir /private/tmp/ats-au-editor --output-on-failure \
  -R '^ats_au_(artifact_test|editor_view_probe)$'
```

Result: both tests passed. The component still opens through the direct
factory, verifies the 81-entry parameter matrix, ATSP class-info round-trip,
and zero-input stereo process silence. The editor probe reads
`kAudioUnitProperty_CocoaUI`, loads `AtsAudioUnitCocoaViewFactory` from the
component bundle, creates a Visage-backed `NSView` at 1280×940, attaches it to
a Cocoa host window, accepts first-responder focus, resizes to 720×480, and
removes cleanly with no stale subview. Direct-factory harnesses cast
`AudioComponentMethod` lookups to fixed prototypes so ARM64 register calling
convention matches the AudioUnitSDK implementations.

This advances commit 22's native AU editor gate. AU registration/`auval`
scan, MIDI/automation stress, and runtime standalone GUI smoke remain open.

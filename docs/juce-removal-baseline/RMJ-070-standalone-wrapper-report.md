# RMJ-070 standalone wrapper report

Task ID: RMJ-070 / commit 22 standalone slice

The JUCE-free standalone projection now has two native macOS entry points:

- `ats_resynth_standalone` is a real `ATS Resynth.app` bundle with the ATS
  bundle identifier and a Visage `ApplicationWindow`.
- `ats_resynth_standalone_smoke` is the same entry point without bundle
  packaging and accepts `--smoke` for launch, resize, hide/show, and teardown
  checks. It uses `HostAdapter::attachStandaloneEditor`, not a headless editor.

The implementation follows the Mearcstapa pattern of pinning Visage in the
top-level CMake graph, while keeping the ATS wrapper independent of CLAP.

Build evidence:

```text
cmake -S . -B /tmp/ats-standalone-arm64 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_PLUGIN=OFF -DATS_BUILD_NATIVE_STANDALONE=ON \
  -DATS_VISAGE_DIR="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/_deps/visage-src" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DFETCHCONTENT_SOURCE_DIR_BGFX="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/_deps/bgfx-src" \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE="/Volumes/Extreme SSD/Mearcstapa/build-wrappers/_deps/freetype-src"
cmake --build /tmp/ats-standalone-arm64 \
  --target ats_resynth_standalone ats_resynth_standalone_smoke -j2
```

Result: both native standalone targets build as arm64. The current desktop
test host traps inside Visage `ApplicationWindow::showStandalone()` when the
smoke process creates its first native window, so this report does not claim
runtime standalone evidence from that invocation. The existing arm64 Visage
editor probe remains the native standalone lifecycle evidence packet and must
be rerun successfully in a window-capable session before the standalone gate
is promoted.

The standalone smoke was then rebuilt with `BUILD_PLUGIN=ON` in the x86_64
window-capable configuration and passed:

```text
arch -x86_64 /private/tmp/ats-native-default/src/host/standalone/ats_resynth_standalone_smoke --smoke
ats_standalone_smoke: launch, resize, hide/show, and teardown ok
```

This is live native `ApplicationWindow` evidence for the available runtime;
the arm64 headless-session limitation remains open.

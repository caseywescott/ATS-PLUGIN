# RMJ-010 Visage backend and live native probe

Task ID: RMJ-010

## Decision

ATS selects Visage as the JUCE-free UI backend and pins it to:

`20de59464243447816d142e9d38e9723d068f755`

The revision is part of the top-level CMake configuration as
`ATS_VISAGE_VERSION`. `ATS_VISAGE_DIR` is an explicit local-checkout override
for reproducible/offline builds; otherwise CMake fetches the pinned commit.

The backend-neutral `ats::ui::IEditor` contract covers logical size, resize,
scale, native-parent attachment, show/hide, visibility, native handle, and
teardown. `HostAdapter` forwards its native editor lifecycle through that
contract. `HeadlessEditor` remains a supplemental test double and does not
claim native behavior.

## Live evidence

The minimal real editor owns a Visage `ApplicationWindow` and a probe frame.
The arm64 macOS probe creates an `NSWindow`, embeds the Visage view into its
`contentView`, then exercises:

- create and native parent attachment;
- logical resize at 640×480 and scale 1.25;
- visibility and native focus;
- native mouse-down dispatch;
- native text input dispatch;
- hide and destruction, including removal of the embedded child view.

Evidence command:

```text
cmake -S . -B /tmp/ats-visage-probe-arm64-release \
  -DBUILD_PLUGIN=OFF -DATS_BUILD_VISAGE_UI_PROBE=ON \
  -DATS_VISAGE_DIR='/Volumes/Extreme SSD/Mearcstapa/build-ui/_deps/visage-src' \
  -DFETCHCONTENT_SOURCE_DIR_BGFX='/Volumes/Extreme SSD/Mearcstapa/build-ui/_deps/bgfx-src' \
  -DFETCHCONTENT_SOURCE_DIR_FREETYPE='/Volumes/Extreme SSD/Mearcstapa/build-ui/_deps/freetype-src' \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ats-visage-probe-arm64-release --target visage_editor_probe -j2
ctest --test-dir /tmp/ats-visage-probe-arm64-release --output-on-failure -R visage_editor_probe
```

Result: `1/1` passed.

The headless lifecycle remains covered separately by `host_lifecycle_test`.
This probe clears the backend/native-window blocker; it does not claim that
the complete ATS widget set, popup matrix, dialogs, or host wrapper artifacts
have already been ported.

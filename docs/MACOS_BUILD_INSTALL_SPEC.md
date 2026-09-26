# macOS build, Standalone, and Logic installation specification

Status: **normative target workflow**. This is the source of truth for local
macOS builds of `ATS Resynth.app` and installation of `ATS Resynth.component`
for Logic Pro.

The workflow is deliberately split into **build**, **verify**, and **install**.
A compiler invocation must never silently replace the Audio Unit Logic uses.

## 1. Required outcomes

The workflow must guarantee that:

- Standalone and AU artifacts use the same source revision, configuration,
  version, architecture, and signing mode;
- an x86_64 build cannot overwrite an arm64 installation or vice versa;
- Logic never observes a partially copied or invalidly signed component;
- Apple re-examines the new installed bundle instead of retaining stale data;
- success is based on the **installed** artifact passing architecture,
  signature, metadata, `auval`, and Cocoa editor tests;
- a failed installation leaves the previous working component recoverable.

## 2. Identity and version contract

The product identity is stable:

| Field | Required value |
|---|---|
| Bundle identifier | `com.atsplugin.resynth` |
| AU type | `aumf` |
| AU subtype | `Atr1` |
| AU manufacturer | `AtPl` |
| Display name | `ATSPlugin: ATS Resynth` |

`project(... VERSION MAJOR.MINOR.PATCH)` in the root `CMakeLists.txt` is the
single semantic-version source. Configuration must derive:

- `CFBundleShortVersionString = MAJOR.MINOR.PATCH`;
- `CFBundleVersion = MAJOR.MINOR.PATCH` locally, or a monotonically increasing
  numeric build value for releases;
- `AudioComponents[0].version = (MAJOR << 16) | (MINOR << 8) | PATCH`.

The packed AU version must not be a literal. With project version `0.1.0`, its
value is `256` (`0x00000100`), not `65536` (`0x00010000`, or `1.0.0`). Major,
minor, and patch must fit in 16, 8, and 8 bits respectively.

A normal edit/build/install cycle does not require a version bump; deterministic
registration invalidation handles it. For a real product version, update the
root version before configuration and verify that bundle and packed AU versions
both changed.

## 3. Current native products

| Product | CMake target | Artifact |
|---|---|---|
| Standalone | `ats_resynth_standalone` | `<build>/src/host/standalone/ATS Resynth.app` |
| Audio Unit | `ats_resynth_au` | `<build>/src/host/au/ATS Resynth.component` |
| Standalone smoke | `ats_resynth_standalone_smoke` | CTest target |
| AU artifact test | `ats_au_artifact_test` | CTest target |
| AU editor probe | `ats_au_editor_view_probe` | CTest target |

Names such as `AtsResynth_AU`, `AtsResynth_Standalone`, and paths below
`src/plugin/AtsResynth_artefacts` belong to the retired JUCE build.

## 4. Architecture policy

Use one explicit build directory per architecture:

- `build-macos-arm64`
- `build-macos-x86_64`
- `build-macos-universal` only for a deliberately assembled universal build.

On Apple Silicon the default Logic development build is arm64. An empty
`CMAKE_OSX_ARCHITECTURES` is not acceptable for an installable artifact. Use
`lipo -archs` and compare the complete slice set; checking the first word from
`file` is insufficient.

The embedded `libsndfile.1.dylib` must contain every architecture in the main
executable. Because the repository currently supplies
`vendor/sndfile-arm64`, the supported default is arm64 until a matching x86_64
dependency is supplied and a universal bundle is deliberately assembled.

## 5. Signing policy

Local development uses ad-hoc signing:

```sh
-DATS_CODESIGN_IDENTITY=-
```

A named identity is allowed only if it appears in:

```sh
security find-identity -v -p codesigning
```

If it is absent or invalid, fail before replacing the installation. Never
produce a component that reports `CSSMERR_TP_NOT_TRUSTED`. Distribution uses a
valid Developer ID identity, notarization, and stapling as described in
`docs/RELEASE.md`; it must use a separate CMake cache.

Sign inside-out: embedded dylib, executable, then outer bundle. Remove all
AppleDouble `._*` files before every signing boundary, then require
`codesign --verify --deep --strict`.

## 6. Canonical local arm64 build

Run from the repository root in a native arm64 terminal:

```sh
cmake -S . -B build-macos-arm64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_PLUGIN=OFF \
  -DATS_BUILD_NATIVE_AU=ON \
  -DATS_BUILD_NATIVE_STANDALONE=ON \
  -DATS_BUILD_NATIVE_VST3=OFF \
  -DATS_CODESIGN_IDENTITY=-

cmake --build build-macos-arm64 --config Release \
  --target ats_resynth_standalone ats_resynth_au -j 8
```

The compliant automation must offer an artifact-only equivalent. The AU target
must never install as a post-build side effect; that violates this specification
and must be removed when the automation is implemented.

## 7. Build-tree verification

Use explicit paths, never a glob that could select another build directory:

```sh
BUILD_DIR="build-macos-arm64"
APP="$BUILD_DIR/src/host/standalone/ATS Resynth.app"
AU="$BUILD_DIR/src/host/au/ATS Resynth.component"

test -d "$APP"
test -d "$AU"
test "$(lipo -archs "$APP/Contents/MacOS/ATS Resynth")" = "arm64"
test "$(lipo -archs "$AU/Contents/MacOS/ATS Resynth")" = "arm64"
test "$(lipo -archs "$AU/Contents/Frameworks/libsndfile.1.dylib")" = "arm64"
plutil -lint "$APP/Contents/Info.plist"
plutil -lint "$AU/Contents/Info.plist"
codesign --verify --deep --strict --verbose=2 "$APP"
codesign --verify --deep --strict --verbose=2 "$AU"
```

Also require matching bundle and packed AU versions, no `._*` files, and no
absolute repository/Homebrew/vendor dependency path in `otool -L` output.

Run focused tests:

```sh
ctest --test-dir build-macos-arm64 --output-on-failure \
  -R 'ats_resynth_standalone_smoke|ats_au_artifact_test|ats_au_editor_view_probe'
```

## 8. Standalone acceptance

Standalone remains in the build directory for development. Copying it to
`/Applications` is a separate explicit packaging action. It is acceptable when:

- architecture and strict signature gates pass;
- `ats_resynth_standalone_smoke` passes;
- it launches the native Visage editor, resizes, accepts an editor gesture, and
  exits without a stale process;
- Standalone and AU report the same version and source revision.

## 9. Atomic Logic installation

The only destination is:

```text
~/Library/Audio/Plug-Ins/Components/ATS Resynth.component
```

Installation is a separate explicit command and must perform this order:

1. Refuse if Logic Pro is running. Never terminate it automatically because an
   unsaved project may be open. Re-check immediately before replacement and
   before cache invalidation so Logic cannot be opened mid-install.
2. Acquire an installer lock to prevent concurrent installs.
3. Re-run all build-tree gates.
4. Create staging on the destination filesystem and copy with `ditto`.
5. Set directories to `755`, metadata/resources/dylibs to `644`, and the main
   executable to `755`.
6. Remove AppleDouble sidecars and sign the staged bundle inside-out.
7. Verify staged architecture, signature, plist, versions, and dependencies.
8. Move the installed bundle to a backup location that is **outside every
   Audio Unit scan directory** and does not end in `.component`, then rename
   staging to `ATS Resynth.component`. A backup such as
   `ATS Resynth.backup.123.component` inside `~/Library/Audio/Plug-Ins/Components`
   is still a live Audio Unit and creates a duplicate identity. Use a sibling
   non-scanned directory such as `~/Library/Audio/Plug-Ins/ATS Backups`, or a
   filename ending in `.component.disabled`. Keep staging and final replacement
   on the destination filesystem so Logic cannot observe a partial copy.
9. Verify the installed path. Restore the backup on any failure.
10. Retain at most one known-good backup after success.

No normal build target may delete or copy into this destination. In particular,
compiling x86_64 must be incapable of replacing arm64 merely because it ran last.

## 10. Registrar and Logic refresh

After installation succeeds, with Logic still closed:

1. remove the user Audio Unit cache;
2. remove Logic's plug-in database cache files;
3. remove only ATS identity entries from preference caches when present;
4. terminate `AudioComponentRegistrar` so macOS relaunches it on demand;
5. validate the installed component with `auval`;
6. run the Cocoa editor probe against the installed binary;
7. optionally launch Logic only after both pass.

Canonical validation identity:

```sh
auval -v aumf Atr1 AtPl
```

Success must include:

```text
Manufacturer String: ATSPlugin
AudioUnit Name: ATS Resynth
AU VALIDATION SUCCEEDED
```

Logic should then list the plug-in at:

```text
Audio FX -> ATSPlugin -> ATS Resynth
```

Cache deletion alone is never success. If `auval` cannot retrieve the name or
component version, do not launch Logic or report completion.

## 11. Installed-artifact acceptance gates

Installation exits successfully only when:

- Logic stayed closed during replacement and invalidation;
- installed executable and dylib have exactly the requested slices;
- Standalone executable and embedded dylib also have exactly the requested
  slices and no build-machine dependency paths;
- exactly one bundle with the ATS AU identity exists across user and system
  Audio Unit scan paths; backups and staging bundles are not discoverable as
  `.component` bundles;
- files are registrar-readable and no AppleDouble sidecars exist;
- `plutil` accepts the installed plist;
- semantic and packed AU versions match the configured project version;
- installed `codesign --verify --deep --strict` succeeds;
- `otool -L` contains no build-machine absolute dependency path;
- installed-path `auval` succeeds;
- installed-path Cocoa editor creation succeeds and has a native child view.

A generic/fallback Logic view is a failed installation even if audio loads.

## 12. Required automation interface

Implement the specification with these scripts or equivalent explicit targets:

```text
scripts/build_macos_artifacts.sh --arch arm64 --sign -
scripts/verify_macos_artifacts.sh --build-dir build-macos-arm64 --arch arm64
scripts/install_logic_au.sh --build-dir build-macos-arm64 --arch arm64 [--launch-logic]
```

Required behavior:

- build writes only inside its build directory;
- verify is read-only;
- install is the only command allowed to write Components or clear caches;
- install always stages the explicitly selected build-tree artifact, even when
  CMake reports its target is already up to date; it must never depend on a
  post-build command having run;
- scripts use `set -euo pipefail` and preserve every build/validator exit code;
- no script implicitly selects the most recently used build directory;
- each run prints source revision, dirty status, version, architecture, signing
  mode, artifact path, destination, and result;
- an install receipt records those values plus installed binary hash/time.

## 13. Failure diagnosis

| Symptom | Meaning/action |
|---|---|
| Logic shows only a generic view | Custom Cocoa editor rejected; check installed signature, architecture, registrar, and editor probe |
| `Cannot get Component's Name strings` | Registrar rejects/cannot read bundle; check permissions, signature, architecture, plist, and cache state |
| `CSSMERR_TP_NOT_TRUSTED` | Invalid named identity; rebuild ad-hoc or with a valid identity |
| arm64 probe cannot `dlopen` component | Installed AU or dependency is x86_64-only |
| Direct probe passes but `auval` fails | Probe bypassed registrar/security; installation is invalid |
| Logic shows an older version | Compare packed AU version, bundle version, installed hash, registrar, and Logic cache |
| CMake says `Built target` but the installed architecture is still wrong | The target was up to date, so its post-build installer did not run; explicitly install the selected verified artifact |
| `auval` reports the wrong channel layout after the active binary tests correctly | Search all scan paths for another `.component` with the same type/subtype/manufacturer; a live backup may be winning registration |

## 14. Recorded fallback-UI incident and recovery

On 2026-08-25, Logic displayed its generic Audio Unit view containing only the
`ATS Resynth` name instead of the Visage editor. The installed component had
been overwritten by an x86_64-only artifact from `build-plugin` on an arm64
Mac. Native `auval` could read cached identity metadata but failed at
`OpenAComponent`, so Logic could not load the custom editor.

The first arm64 rebuild command reported the target as up to date. That did not
repair the installation: CMake did not relink the target, therefore its
post-build copy step did not run, and the x86_64 component remained installed.
This is why build success and installation success must be independent states.

The recovery that restored the custom UI was:

1. confirm Logic was closed;
2. confirm the installed executable was x86_64 with `file` and `lipo -archs`;
3. force the arm64 AU wrapper to relink because the current build design only
   installs from a post-build action;
4. install the resulting arm64 component;
5. clear Logic/AU caches and restart `AudioComponentRegistrar`;
6. verify the **installed** signature and architecture;
7. run native `auval -v aumf Atr1 AtPl` to completion;
8. run `ats_au_editor_view_probe` against the installed component binary.

The accepted recovered state was:

```text
installed executable: Mach-O 64-bit bundle arm64
codesign --verify --deep --strict: success
AU VALIDATION SUCCEEDED
CocoaUI advertise, NSView attach, focus, resize, parameter gesture, teardown: success
```

After this state is reached, open Logic and insert a **new** ATS Resynth
instance. If a saved project restores the old fallback window, remove that
instance and insert the plug-in again after the registrar/cache refresh.

The forced relink is a temporary recovery technique, not the desired
automation. The compliant installer must copy the explicitly selected verified
artifact transactionally whether or not compilation produced new object files.

### Duplicate-backup incident

A subsequent installation left
`ATS Resynth.backup.32597.component` beside the active component. Both bundles
declared the same `aumf/Atr1/AtPl` identity. The active binary's direct artifact
test reported `[2,2]` and `[1,2]`, while `auval` selected the backup and reported
`[0,2]`; Logic consequently omitted the plug-in from its Audio FX menu.

Renaming the backup to `ATS Resynth.backup.32597.component.disabled`, clearing
the AU/Logic caches, and restarting `AudioComponentRegistrar` restored a single
registration. `auval` then reported one input bus, `[2,2]` and `[1,2]`, the
custom Cocoa UI, and `AU VALIDATION SUCCEEDED`.

## 15. Repository work required for compliance

- [x] remove installation from `ats_resynth_au`'s post-build command;
- [x] default local signing to ad-hoc while allowing an explicit release identity;
- [x] derive `AudioComponents.version` from the root project version;
- [x] replace `scripts/install_au.sh` with a compatibility wrapper around the
  staged, architecture-aware, rollback-safe installation;
- [x] make installed signature, exact Logic bus/channel contract, unique AU
  identity, `auval`, and Cocoa editor gates mandatory;
- replace remaining JUCE-era target names and paths in documentation.

# Building ATS-PLUGIN (Phase 0)

## Prerequisites

- **CMake** 3.16 or newer  
- **C compiler** (Clang on macOS, GCC on Linux, MSVC 2019+ on Windows)  
- **libsndfile** (e.g. `brew install libsndfile`, `apt install libsndfile1-dev`). CMake locates it via `CMAKE_PREFIX_PATH`, standard system paths, or `brew --prefix libsndfile` on macOS.  
- **Qatsh** sources at `qatsh/` relative to this repository root (default `ATS_ATSA_SOURCE_DIR`). Clone with `git clone https://gitlab.com/dxarts/projects/ats/qatsh.git qatsh`. The static **atsa** library is built from `qatsh/src/atsa/` (analysis + resynthesis). CI performs this clone automatically.

## Configure and build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Artifacts:

- `build/libatsa.a` (or `atsa.lib` on MSVC)—static ATSA  
- `build/tools/ats_offline`—offline analyze → synthesize → WAV CLI (`--type` optional, matches ATSA file types 1–4)

Analysis and synthesis are **CPU-heavy**; the CLI runs on the calling thread (no host real-time constraints).

## CMake variables

| Variable | Default | Meaning |
|----------|---------|---------|
| `ATS_ATSA_SOURCE_DIR` | `${CMAKE_SOURCE_DIR}/qatsh/src/atsa` | Directory containing `atsa.h` and `.c` sources |

## Compiler baselines (informal)

- **macOS:** Apple Clang from Xcode 14+ or Command Line Tools; arm64 and x86_64 are exercised when CI runners are available.  
- **Linux:** GCC 9+ or Clang 12+.  
- **Windows:** MSVC 2019+; ensure `sndfile` is discoverable via `CMAKE_PREFIX_PATH` if not using vcpkg.

## Native plugin artifacts

The canonical macOS build, Standalone, versioning, installation, and Logic
refresh workflow is defined in
[`docs/MACOS_BUILD_INSTALL_SPEC.md`](docs/MACOS_BUILD_INSTALL_SPEC.md). Follow
that specification for installable artifacts; a successful compile alone does
not prove that Apple's registrar accepts the installed Audio Unit.

- **Option:** `-DBUILD_PLUGIN=ON` (default) selects the JUCE-free native graph:
  pinned Visage UI, Steinberg VST3, macOS AUv2, and standalone targets. Use
  `-DBUILD_PLUGIN=OFF` for the core/tool-only graph or focused native options.
- **Focused options:** `ATS_BUILD_NATIVE_VST3`, `ATS_BUILD_NATIVE_AU`, and
  `ATS_BUILD_NATIVE_STANDALONE` can be enabled independently. Supply local
  checkouts with `ATS_VST3_SDK_DIR`, `ATS_AUDIO_UNIT_SDK_DIR`, and
  `ATS_VISAGE_DIR`; each checkout is rejected unless its Git `HEAD` matches the
  exact revision pinned in `CMakeLists.txt`.
- **Artifacts:** the native targets emit `ATS Resynth.vst3`,
  `ATS Resynth.component` on macOS, and `ATS Resynth.app`. The artifact tests
  exercise identity, the 81-parameter matrix, state, automation, MIDI, format
  negotiation, and lifecycle. CocoaUI, VST3 `NSView`, and standalone smoke
  probes exercise the real native window path where a window-capable session is
  available.
- **RT path:** the audio callback only plays a **pre-rendered** stereo buffer;
  `tracker` / `do_synthesis` run on worker threads (see
  `src/core/AtsRtSafety.h`). The neutral `HostAdapter` owns the format-neutral
  process contract; no JUCE object crosses the audio boundary.

## Presets and model size (Phase 2)

- **Factory presets** are small parameter recipes baked into the plug-in (partials/residual gains, frequency stretch, and related APVTS fields). They do **not** embed an analyzed ATS model; load a WAV and analyze as usual after choosing a factory curve.
- **User presets** are full binary state snapshots (same format as host save/restore), extension **`.atspreset`**. They include APVTS parameters **and** the embedded ATS model when one was saved.
- **User preset folder:** under the OS application-support area, `ATS Resynth/Presets` (the editor creates this directory when needed). On macOS this is typically `~/Library/Application Support/ATS Resynth/Presets`.
- **Model size soft limit:** the UI warns when `partials * frames` exceeds **2,000,000** (informal guardrail only). Large models can make analysis, resynthesis, and memory use heavy; there is no hard cap in the MVP.

## Transforms (Phase 3)

- **Session / Synthesis / Partials / Math** tabs: global synthesis APVTS on **Synthesis**; single-cell partial edits on **Partials**; multi-line `a`/`f`/`h` assignments on **Math** (offline worker thread).
- Expression variables and functions are documented in **`docs/MATHEXPR.md`** (aligned with Qatsh `README.txt` where the MVP subset allows).

## Interchange and formats (Phase 4)

- **Native `.ats` files:** **Import ATS…** / **Export ATS…** on the Session tab use the same `ats_load` / `ats_save` path as Qatsh/ATSA. Export uses the current analysis **SMR** threshold and **analysis type** from the Synthesis tab. After import, the **Analysis type** control is updated from the file header when possible.
- **Round-trip test:** `ctest` runs **`ats_roundtrip`**, which analyzes a short internal sine, saves and reloads via `.ats`, and compares synthesis buffers (`tools/ats_roundtrip.c`).
- **AU (macOS only):** the CMake target also builds an **Audio Unit** beside VST3 and Standalone when `CMAKE_SYSTEM_NAME` is Darwin (Logic and other AU hosts).
- **MIDI:** the plug-in accepts MIDI. **Note On** (velocity > 0) transposes resynthesis in **semitones relative to middle C (note 60)** on top of the **Freq** APVTS factor (`effective = freq * 2^((note-60)/12)`). **Note Off** clears the MIDI transpose. There is **no extra plug-in latency** beyond playback buffer behaviour already in Phase 1; this is not a low-latency live instrument path.

## Tests

After building:

```sh
ctest --test-dir build --output-on-failure
```

The golden test invokes `ats_offline` and checks tolerances (see `docs/DETERMINISM.md`). **`ats_roundtrip`** exercises `ats_save` / `ats_load` synthesis parity (see Phase 4 in this file).

## Phase 5 — Performance and release

- **Performance budgets and limitations:** `docs/PERFORMANCE.md` (reference hardware assumptions, model size / memory guidance, live-analysis stance, resynth expectations).
- **Signing, notarization, packaging, release notes:** `docs/RELEASE.md`.
- **macOS local build and Logic installation contract:**
  `docs/MACOS_BUILD_INSTALL_SPEC.md`.
- **Unsigned zip helper (local):** after a Release build, run `./scripts/package_release_archives.sh` from the repo root; archives land in `dist/` (not used by CI).

## Working on an exFAT volume

The canonical checkout lives on an exFAT external drive, which has three
quirks that affect git and incremental builds:

- **File mode.** exFAT reports every file as `0755`, so run
  `git config core.fileMode false` once per clone; otherwise every touched
  file shows a phantom `100644 → 100755` diff.
- **AppleDouble files.** macOS writes `._<name>` metadata beside files when it
  stores extended attributes. They are ignored by `.gitignore`; remove them
  with `scripts/clean_appledouble.sh` (idempotent, skips `.git`).
- **Timestamps.** Restoring files out-of-band (copy, `git checkout` of an old
  blob) can leave mtimes that CMake/Make treat as up to date. `touch` restored
  sources before rebuilding so stale objects are not reused.

The internal system volume can be too small for large builds. Point
`TMPDIR` at the external drive (for example
`export TMPDIR="/Volumes/Extreme SSD/tmp"`) if a build fails with misleading
`lipo` or LLVM I/O errors.

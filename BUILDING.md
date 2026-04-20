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

## JUCE plugin (Phase 1)

- **Option:** `-DBUILD_PLUGIN=ON` (default) builds **ATS Resynth** as **VST3** and **Standalone** via JUCE 7.0.12 (fetched by CMake). Turn off with `-DBUILD_PLUGIN=OFF` for tool-only CI or faster configures.
- **Artifacts:** `build/src/plugin/AtsResynth_artefacts/Release/VST3/ATS Resynth.vst3` and `Standalone/ATS Resynth.app` (paths may vary slightly by generator).
- **RT path:** the audio callback only plays a **pre-rendered** stereo buffer; `tracker` / `do_synthesis` run on worker threads (see `src/core/AtsRtSafety.h`). Changing synthesis parameters triggers a **background re-render** via `AsyncUpdater` (no `do_synthesis` on the audio thread).
- **Output trim** uses `juce::LinearSmoothedValue` (~20 ms ramp) to reduce zipper noise; other synthesis parameters require a full re-render.
- **Analysis type** (APVTS) is read when you press **Analyze**; change it before analyzing. Other synthesis APVTS parameters are exposed to the host; the editor includes sliders for partials gain, residual gain, and output trim only.
- **VST2 replacement:** `JUCE_VST3_CAN_REPLACE_VST2=0` is set so a Steinberg VST2 SDK is not required.

## Presets and model size (Phase 2)

- **Factory presets** are small parameter recipes baked into the plug-in (partials/residual gains, frequency stretch, and related APVTS fields). They do **not** embed an analyzed ATS model; load a WAV and analyze as usual after choosing a factory curve.
- **User presets** are full binary state snapshots (same format as host save/restore), extension **`.atspreset`**. They include APVTS parameters **and** the embedded ATS model when one was saved.
- **User preset folder:** under the OS application-support area, `ATS Resynth/Presets` (the editor creates this directory when needed). On macOS this is typically `~/Library/Application Support/ATS Resynth/Presets`.
- **Model size soft limit:** the UI warns when `partials * frames` exceeds **2,000,000** (informal guardrail only). Large models can make analysis, resynthesis, and memory use heavy; there is no hard cap in the MVP.

## Transforms (Phase 3)

- **Session / Synthesis / Partials / Math** tabs: global synthesis APVTS on **Synthesis**; single-cell partial edits on **Partials**; multi-line `a`/`f`/`h` assignments on **Math** (offline worker thread).
- Expression variables and functions are documented in **`docs/MATHEXPR.md`** (aligned with Qatsh `README.txt` where the MVP subset allows).

## Tests

After building:

```sh
ctest --test-dir build --output-on-failure
```

The golden test invokes `ats_offline` and checks tolerances (see `docs/DETERMINISM.md`).

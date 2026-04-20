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

## Tests

After building:

```sh
ctest --test-dir build --output-on-failure
```

The golden test invokes `ats_offline` and checks tolerances (see `docs/DETERMINISM.md`).

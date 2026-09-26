# Commands cheat sheet

Quick copy-paste commands for **ATS-PLUGIN** (static **atsa**, **ats_offline**
CLI, and the native JUCE-free **ATS Resynth** products). For the authoritative
macOS Standalone and Logic workflow, see
[`docs/MACOS_BUILD_INSTALL_SPEC.md`](docs/MACOS_BUILD_INSTALL_SPEC.md).

All examples assume the repository root as the current directory.

---

## One-time: Qatsh (ATSA sources)

CMake expects ATSA under `qatsh/src/atsa/` by default (`ATS_ATSA_SOURCE_DIR`).

```sh
git clone --depth 1 https://gitlab.com/dxarts/projects/ats/qatsh.git qatsh
```

---

## Configure (Release)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

**Tools only (no JUCE / no plugin)** — faster configure, matches Linux CI style:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=OFF
```

**Custom ATSA path:**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DATS_ATSA_SOURCE_DIR=/path/to/atsa
```

**libsndfile hints** (if CMake cannot find it):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix libsndfile)"
```

---

## Build everything (default: plugin + tools + tests)

```sh
cmake --build build --parallel 8
```

---

## Build specific targets

**Static ATSA library**

```sh
cmake --build build --target atsa --parallel 8
```

**Offline CLI** (`build/tools/ats_offline`)

```sh
cmake --build build --target ats_offline --parallel 8
```

**ATS save/load round-trip tool** (`build/tools/ats_roundtrip`)

```sh
cmake --build build --target ats_roundtrip --parallel 8
```

**Native plug-in products** (shared code + formats)

```sh
cmake --build build --target ats_resynth_vst3 ats_resynth_standalone ats_resynth_au --parallel 8
```

**VST3 only**

```sh
cmake --build build --target ats_resynth_vst3 --parallel 8
```

**Standalone app only**

```sh
cmake --build build --target ats_resynth_standalone --parallel 8
```

**Audio Unit only** (macOS)

```sh
cmake --build build --target ats_resynth_au --parallel 8
```

---

## Reconfigure after changing options

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=ON
cmake --build build --parallel 8
```

---

## Clean rebuild

```sh
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
```

---

## Tests

```sh
ctest --test-dir build --output-on-failure
```

**List tests**

```sh
ctest --test-dir build -N
```

**Smearnov onset/attack-smear tests only**

```sh
ctest --test-dir build -R transient_smear --output-on-failure
```

---

## Run tools manually

**Offline analyze → synthesize → WAV** (type 1–4):

```sh
./build/tools/ats_offline --type 1 input.wav output.wav
```

**ATS file round-trip self-test** (writes a temp `.ats` under the working directory):

```sh
./build/tools/ats_roundtrip
```

---

## Plugin artefact locations (Release, typical)

Current native paths are:

- **VST3:** `build/src/host/vst3/ATS Resynth.vst3`
- **Standalone:** `build/src/host/standalone/ATS Resynth.app`
- **AU (macOS):** `build/src/host/au/ATS Resynth.component`

Do not treat compilation as installation. The required architecture, signing,
atomic replacement, registrar, cache, and installed-artifact gates are defined
in `docs/MACOS_BUILD_INSTALL_SPEC.md`.

---

## Unsigned release zip (local helper)

After a successful **Release** build with the plugin enabled:

```sh
./scripts/package_release_archives.sh build
```

Output: `dist/ats-resynth-<version>-unsigned-<os>-<arch>.zip` (see [docs/RELEASE.md](docs/RELEASE.md) for signing and real releases).

---

## Debug build

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLUGIN=ON
cmake --build build-debug --parallel 8
```

---

## See also

- [BUILDING.md](BUILDING.md) — prerequisites, behaviour, soft limits  
- [docs/DETERMINISM.md](docs/DETERMINISM.md) — offline golden expectations  
- [docs/RELEASE.md](docs/RELEASE.md) — signing, notarization, release notes  
- [docs/MACOS_BUILD_INSTALL_SPEC.md](docs/MACOS_BUILD_INSTALL_SPEC.md) — native macOS build, Standalone, AU install, and Logic refresh
- [docs/SMEARNOV_SPEC.md](docs/SMEARNOV_SPEC.md) — onset detection & attack-smearing (Session tab → Smearnov)  

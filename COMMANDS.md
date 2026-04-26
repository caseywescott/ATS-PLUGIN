# Commands cheat sheet

Quick copy-paste commands for **ATS-PLUGIN** (static **atsa**, **ats_offline** CLI, **ATS Resynth** JUCE plugin). For background and paths, see [BUILDING.md](BUILDING.md).

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

**JUCE plugin** (shared code + formats)

```sh
cmake --build build --target AtsResynth --parallel 8
```

**VST3 only**

```sh
cmake --build build --target AtsResynth_VST3 --parallel 8
```

**Standalone app only**

```sh
cmake --build build --target AtsResynth_Standalone --parallel 8
```

**Audio Unit only** (macOS)

```sh
cmake --build build --target AtsResynth_AU --parallel 8
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

### Default deterministic CI run (synthetic fixtures only)

All tests — including the synthetic `ats_modern_acceptance` and the external gate-logic unit test — run without any audio assets:

```sh
ctest --test-dir build --output-on-failure
```

**List tests**

```sh
ctest --test-dir build -N
```

### Generate ATSA reference artifacts from real audio

Run this once when new external audio fixtures are available (NOT part of normal CI):

```sh
python3 tests/generate_atsa_references.py \
  --ats-offline ./build/tools/ats_offline \
  --manifest tests/fixtures/ats_external_fixture_manifest.json \
  --output-dir tests/fixtures/reference
```

Optional flags:

```sh
# Process a single fixture only:
--fixture-id external_sustained_flute

# Dry-run: print commands without executing:
--dry-run
```

### Run external real-audio acceptance gate

Requires: audio files under `tests/fixtures/audio/`, reference metrics in `tests/fixtures/reference/`, and `ATS_ENABLE_EXTERNAL_FIXTURES=ON`.

**Configure with external fixtures enabled:**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DATS_ENABLE_EXTERNAL_FIXTURES=ON
cmake --build build --parallel 8
```

**Run all tests including real-audio gate:**

```sh
ctest --test-dir build --output-on-failure
```

**Run external acceptance only:**

```sh
ctest --test-dir build -R ats_modern_acceptance_external --output-on-failure
```

**Run the Python driver directly (for debugging):**

```sh
python3 tests/run_external_acceptance.py \
  ./build/tools/ats_offline \
  tests/fixtures/ats_external_fixture_manifest.json \
  tests/fixtures/reference
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

Paths can vary slightly by generator/OS; defaults look like:

- **VST3:** `build/src/plugin/AtsResynth_artefacts/Release/VST3/ATS Resynth.vst3`
- **Standalone:** `build/src/plugin/AtsResynth_artefacts/Release/Standalone/ATS Resynth.app`
- **AU (macOS):** `build/src/plugin/AtsResynth_artefacts/Release/AU/ATS Resynth.component`

If `COPY_PLUGIN_AFTER_BUILD` is enabled in CMake, JUCE may also install into the user plug-in folders during build (see CMake/JUCE output).

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

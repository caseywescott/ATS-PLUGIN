# Third-party components and licensing

This repository integrates software from several upstream projects. **This is not legal advice.** Verify obligations before distributing binaries (especially commercial or proprietary builds).

## ATSA / ATS core (analysis + resynthesis)

- **Build source:** `qatsh/src/atsa/` — the extended ATSA tree from [Qatsh](https://gitlab.com/dxarts/projects/ats/qatsh), which adds libsndfile-based I/O, `ats_load`, and synthesis (`synthesis.c`, `curves.c`, …) on top of the original analysis library. The path is overridable via `-DATS_ATSA_SOURCE_DIR=`.
- **Standalone reference:** [ATSA](https://gitlab.com/dxarts/projects/ats/atsa) on GitLab (analysis-focused; does not include the Qatsh synthesis extensions). A checkout also lives at `atsa/` in this repo for reference; it is **not** the build source.
- **License:** permissive (BSD-style academic license). Copyright 2002-2004 Oscar Pablo Di Liscia, Pete Moss and Juan Pampin. See `atsa/LICENSE` for the full grant.

### Local ATSA modifications

The build applies 24 patches from `patches/` to Qatsh upstream commit
`5515648358ffaa8d863b3f2507e03a4943d79667` (`scripts/apply_qatsh_all_patches.sh`,
manifest `tests/architecture/qatsh_patch_manifest.json`). Modified ATSA sources
remain under their original license; ship the patch set with any source
distribution.

## Qatsh / ATSH lineage

- **Qatsh** (Qt port of ATSH): GPL v2 or later for the GUI code; icons mix GPL and LGPL. See `qatsh/COPYRIGHT.txt` and `qatsh/COPYING.txt`.
- **MathExpr** (bundled in Qatsh under `qatsh/lib/mathexpr/`): Copyright Yann Ollivier 1997-2000; freely redistributable but not for sale. See `qatsh/lib/mathexpr/mathexpr.h` for the full notice.
  - **Not linked by this plugin.** `src/core/AtsMathExpr.cpp` is a clean-room expression evaluator that does not include or link any Qatsh MathExpr code.

## libsndfile

- Used for WAV and other audio format I/O in `src/engine/SourceImportService.cpp`.
- **Installed dependency:** version ≥ 1.2.2 required at build time (detected by CMake; Homebrew: `brew install libsndfile`).
- **Bundled arm64 library:** `vendor/sndfile-arm64/` contains a pre-built arm64 `libsndfile.1.dylib` for AU bundle embedding on Apple Silicon. Follow LGPL requirements: either link dynamically (preferred) or comply with LGPL static-linking requirements.
- **License:** LGPL — comply if distributing statically linked binaries.

## Visage

- The native editor backend is pinned to commit
  `20de59464243447816d142e9d38e9723d068f755` (GitHub: VitalAudio/visage). Fetched
  via CMake `FetchContent` when `ATS_BUILD_VISAGE_UI_PROBE=ON`. Follow the
  upstream Visage license and retain its notices when distributing binaries.
- **License:** MIT-style (Copyright Vital Audio, LLC; see the Visage `LICENSE`).
- **Transitive dependencies linked into the editor** (fetched by Visage's CMake):
  - **bgfx / bx / bimg** (Branimir Karadzic): BSD 2-clause. This project builds
    bgfx with enlarged transient buffers (`BGFX_CONFIG_TRANSIENT_VERTEX_BUFFER_SIZE`
    / `_INDEX_BUFFER_SIZE`, see `CMakeLists.txt`); no source is modified.
  - **FreeType:** FreeType License (FTL), or GPLv2 at the distributor's option.
    Binaries must credit FreeType per the FTL.
  - **Embedded fonts:** the editor draws with Visage's embedded Lato
    (`visage::fonts::Lato_Regular_ttf`), distributed under the SIL Open Font
    License 1.1. Include the OFL text with binary releases.

## VST3 SDK (Steinberg)

- The native VST3 wrapper is pinned to commit
  `05c4a9763f311a58fe3f34b68a199e5eea92e2b4`. Fetched via CMake `FetchContent`
  when `ATS_BUILD_NATIVE_VST3=ON`. Follow the [Steinberg VST3 SDK](https://developer.steinberg.help/)
  license terms and trademark guidelines.

## AudioUnitSDK (Apple, macOS only)

- The native AUv2 wrapper is pinned to commit
  `53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9`. Fetched via CMake `FetchContent`
  when `ATS_BUILD_NATIVE_AU=ON`. Follow Apple Audio Unit and AudioToolbox
  redistribution requirements, including notarization for distribution outside
  the Mac App Store.

## Distribution assumption (engineering default)

Per [PRD.md](./PRD.md) §8 and §11.1: **default engineering stance** is **GPL-compliant open-source distribution** for any component that links GPL-covered Qatsh sources, unless rights holders grant a different license or you replace those parts with clean-room implementations. The plugin does **not** link Qatsh GUI code or the Qatsh MathExpr evaluator, which narrows the GPL surface to the ATSA/Qatsh analysis and synthesis C files.

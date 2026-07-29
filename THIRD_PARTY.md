# Third-party components and licensing

This repository integrates software from several upstream projects. **This is not legal advice.** Verify obligations before distributing binaries (especially commercial or proprietary builds).

## ATSA / ATS core (analysis + resynthesis)

- **Source of truth for the plugin spike:** the extended ATSA tree under the [Qatsh](https://gitlab.com/dxarts/projects/ats/qatsh) repository (`qatsh/src/atsa/`), which adds libsndfile-based I/O, `ats_load`, and synthesis (`synthesis.c`, `curves.c`, …) on top of the original analysis library.
- **Standalone reference:** [ATSA](https://gitlab.com/dxarts/projects/ats/atsa) on GitLab (analysis-focused; does not include the Qatsh synthesis extensions).
- **License:** see `qatsh/src/atsa/README.txt` and the upstream `LICENSE` in the ATSA repository. The original ATSA grant is permissive (BSD-style academic license in the GitLab `LICENSE` file); **Qatsh-specific changes** may fall under GPL when combined with the Qatsh GUI—see Qatsh `COPYRIGHT.txt` for the full split.

## Qatsh / ATSH lineage

- **Qatsh** (Qt port of ATSH): GPL v2 or later for the GUI code; icons mix GPL and LGPL. See `qatsh/COPYRIGHT.txt` if you import Qatsh GUI code into this project.
- **MathExpr** (bundled in Qatsh under `lib/mathexpr/`): GPL-like; only relevant if you link that evaluator.

## libsndfile

- Used for WAV (and other format) I/O in the Phase 0 offline tool and expected in later plugin tooling.
- **License:** LGPL (typical distribution: dynamic link or comply with LGPL static-linking requirements).

## Visage

- The native editor backend is pinned to commit
  `20de59464243447816d142e9d38e9723d068f755` in the CMake graph. Follow the
  upstream Visage license and retain its notices when distributing binaries.

## VST3 SDK

- The native VST3 wrapper is pinned to commit
  `05c4a9763f311a58fe3f34b68a199e5eea92e2b4`. Follow the [Steinberg VST3 SDK](https://developer.steinberg.help/)
  license terms and trademark guidelines.

## AudioUnitSDK (macOS)

- The native AUv2 wrapper is pinned to commit
  `53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9`. Follow Apple Audio Unit and
  AudioToolbox redistribution requirements.

## Distribution assumption (engineering default)

Per [PRD.md](./PRD.md) §8 and §11.1: **default engineering stance** is **GPL-compliant open-source distribution** for any component that links GPL-covered Qatsh sources, unless rights holders grant a different license or you replace those parts with clean-room implementations.

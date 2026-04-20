# Vendor directory

Phase 0 does **not** copy ATSA sources into `vendor/`. The CMake project builds the static **atsa** library directly from **`qatsh/src/atsa/`** (path overridable with `ATS_ATSA_SOURCE_DIR`).

Rationale:

- Single source of truth with the cloned [Qatsh](https://gitlab.com/dxarts/projects/ats/qatsh) tree (includes synthesis and `ats_load`).
- Avoids drift between a duplicate `vendor/atsa` and upstream fixes.

This repository `.gitignore`s `qatsh/` so a nested clone is not committed by mistake. Clone Qatsh beside the project root (see `BUILDING.md`). Optional later: add `git submodule` for Qatsh and pin a commit hash in release notes.

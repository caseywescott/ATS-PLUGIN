# RMJ-020 Qatsh patch contract report

Task ID: RMJ-020 / commit 05

## Contract packet

- Trace ID: `ARCH-QATSH-001`.
- Legacy symbols/files: JUCE-coupled CMake/Qatsh target setup and the local
  patch scripts under `patches/` and `scripts/`.
- Replacement owner/API: native `atsa` target plus the machine-readable
  `qatsh_patch_manifest.json` and validator.
- Thread domain: configure/build validation; no runtime ownership.
- State/format impact: none; patch markers protect analysis, synthesis,
  residual, threading, and deterministic behavior requirements.
- Scope: source-current build guard and reproducibility evidence.
- Required tests: all declared patch files and source markers present, with
  CMake/CTest integration.
- Expected evidence: `ats_qatsh_patch_manifest`.
- Known blocker: clean-checkout reproduction still depends on the separately
  documented Qatsh source checkout.

## Evidence

```text
python3 tests/architecture/qatsh_patch_manifest_check.py \
  tests/architecture/qatsh_patch_manifest.json qatsh/src/atsa
OK: 20 Qatsh patches and source marker sets verified
```

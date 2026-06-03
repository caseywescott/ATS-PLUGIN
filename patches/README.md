# Vendor patches

`qatsh/` is intentionally ignored by the top-level repository, but this plugin builds against `qatsh/src/atsa`.
Keep any local ATSA fixes mirrored here so a fresh checkout can reproduce the same engine behavior.

Apply the residual fixes after cloning or updating Qatsh with:

```sh
./scripts/apply_qatsh_residual_fixes.sh apply
```

Check patch state with:

```sh
./scripts/apply_qatsh_residual_fixes.sh status
./scripts/apply_qatsh_residual_fixes.sh reverse-check
```

The helper uses `git apply --ignore-space-change` because some upstream Qatsh files may have mixed line endings.
By default, CMake also requires these residual fixes and will fail configuration when the expected markers are missing.
Use `-DATS_REQUIRE_QATSH_RESIDUAL_FIXES=OFF` only when intentionally testing unpatched Qatsh behavior.

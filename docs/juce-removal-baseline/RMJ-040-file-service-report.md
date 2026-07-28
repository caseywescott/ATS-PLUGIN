# RMJ-040 file and preset service report

Task ID: RMJ-040 / commit 08

## Contract packet

- Trace ID: `ARCH-PRESET-001`.
- Legacy symbols/files: `juce::File`, `juce::FileChooser`, and
  `PresetLibrary` file callbacks in `src/plugin/`.
- Replacement owner/API: `FileService::read/writeAtomic/list` and
  `PresetService::{entries,load,save}`.
- Thread domain: worker/non-real-time only; cancellation is checked before,
  during, and after file operations.
- State/format impact: `.atspreset` extension, sorted user entries, 12 factory
  entries, and opaque payload preservation are unchanged.
- Scope: source-current filesystem/preset service. UI dialog ownership remains
  outside this commit.
- Required tests: factory count, sorted indexing, extension normalization,
  missing/duplicate/cancelled operations, round-trip, and overwrite.
- Expected evidence: `file_service_preset_test` on the headless graph.
- Known deviation: POSIX/macOS replacement is atomic via rename; Windows uses
  a documented remove-and-rename fallback because its filesystem API cannot
  replace an existing file with rename alone.

## Evidence

```text
cmake --build build-review --target file_service_preset_test -j2
./build-review/tests/file_service_preset_test
```

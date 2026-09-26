# UI-PRESET-02 — Typed preset action / dialog and round-trip

Status: implemented

Replaces `PresetLibrary::savePreset()` / `loadPreset()` JUCE `FileChooser`
callbacks and `ParameterStore` codec.

## Ownership

- `FileDialogModel::resolve(savePreset/loadPreset, result)` (in
  `src/ui/FileDialogModel.h`) converts `NativeFileDialogResult` to
  `SavePresetAction` or `LoadPresetAction`.
- `ParameterPresetCodec::encode/decode` (in `src/state/ParameterPresetCodec.h`)
  serialises / deserialises the full parameter store in the ATSP v1 format
  (magic `0x50535441`, version 1, one f32 per parameter in stable ID order).
- `PresetService::save/load` (in `src/state/PresetService.h`) writes/reads
  `.atspreset` files atomically via `FileService::writeAtomic`.

## Round-trip contract

```
ParameterStore → encode() → PresetService::save()
                          → PresetService::load()
                          → decode() → ParameterStore (equal within 1e-3f)
```

Factory preset round-trip: `applyFactoryPreset(index, store) → encode → save
→ load → decode` restores the same parameter values.

## Error / cancellation contract

- Duplicate `save(name, payload, overwrite=false)` → `FileStatus::alreadyExists`.
- Overwrite `save(name, payload, overwrite=true)` → `FileStatus::success`; new
  payload visible on the next `load`.
- Cancelled `save(name, payload, false, &token)` when `token.cancel()` has been
  called → `FileResult::cancelled() == true`.
- Malformed payload → `ParameterPresetCodec::decode` returns `false` and sets
  `error` to a non-empty code.

Evidence:

```text
arch -x86_64 cmake --build build-review --target preset_file_workflow_test -j4
ctest --test-dir build-review --output-on-failure -R '^preset_file_workflow_test$'
```

`test_user_preset_roundtrip` verifies: encode→save→load→decode parameter fidelity,
duplicate rejection (`alreadyExists`), overwrite succeeds, cancellation propagates.
`test_factory_preset_codec_roundtrip` verifies: factory preset 9 parameters
survive a full encode→save→load→decode cycle. `test_codec_errors` verifies
truncated, empty, and corrupt-magic payload rejection.
`test_dialog_save_preset` / `test_dialog_load_preset` verify the
`FileDialogModel` adapter path.

Deviations: live native file chooser (modal blocking call, native title/filter
strings, OS-provided path validation) requires the Visage native dispatch
harness. The `PresetService` directory location (application-support path) is
resolved by the host adapter at runtime.

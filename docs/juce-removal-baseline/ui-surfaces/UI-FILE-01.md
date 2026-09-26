# UI-FILE-01 — File dialog service: accept / cancel / error coverage

Status: implemented

Replaces `juce::FileChooser` callbacks in `SessionTabPanel.cpp` and
`AnalysisTabPanel.cpp` for all five file-chooser operations.

## Ownership

`FileDialogModel::resolve(kind, result, slot)` (in `src/ui/FileDialogModel.h`)
is the single adapter between the platform-owned `NativeFileDialogResult` and
the typed `EditorAction`. The platform dialog (`detail::runNativeFileDialog`) is
declared in `NativeFileDialog.h` and is out-of-scope for headless tests.

## Outcome mapping

| `result.accepted` | `result.path` | `resolve()` return |
|---|---|---|
| `false` | any | `nullopt` (cancelled) |
| `true` | empty | `nullopt` (dialog init failure) |
| `true` | non-empty | typed `EditorAction` |

## Kind mapping

| `NativeFileDialogKind` | Action produced | slot used? |
|---|---|---|
| `importWav` | `ImportWavAction{path, slot}` | yes (0=A, 1=B) |
| `importAts` | `ImportAtsAction{path, slot}` | yes |
| `exportAts` | `ExportAtsAction{path, slot}` | yes (as `source`) |
| `savePreset` | `SavePresetAction{path}` | no |
| `loadPreset` | `LoadPresetAction{path}` | no |

## Source import execution

`SourceImportService` is the JUCE-free import boundary used by `PluginModel`
and `HostAdapter`. It performs a cancellable `FileService` preflight, then:

- decodes `.wav`/`.wave`/`.aif`/`.aiff` with libsndfile and downmixes to mono;
- loads `.ats` through `ats_load` into `AtsSoundPtr` ownership;
- rejects invalid slot, empty path, format mismatch, missing/oversized file,
  decoder failure, and cancellation with a stable error code.

Evidence:

```text
arch -x86_64 cmake --build build-review \
  --target preset_file_workflow_test source_import_service_test -j4
ctest --test-dir build-review --output-on-failure \
  -R '^(preset_file_workflow_test|source_import_service_test)$'
```

`test_dialog_cancel_and_error` verifies all 5 kinds × 3 outcomes (cancelled,
empty-path, and accepted variants). `test_dialog_import_wav/ats/export_ats/
save_preset/load_preset` verify per-kind slot and path passthrough.
`source_import_service_test` verifies stereo WAV decode/downmix, ATS fixture
round-trip, cancellation, and extension rejection.

Deviations: the native blocking file chooser (OS title, filter strings, browse
focus) and actual disk-path acceptance from the OS require the Visage native
dispatch harness. The injected-pasteboard drop path is covered by `UI-FILE-02`.

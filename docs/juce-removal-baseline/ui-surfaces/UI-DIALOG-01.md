# UI-DIALOG-01 — Modal/native dialog lifecycle

Status: implemented

Replaces `juce::FileChooser` + `juce::AlertWindow` + JUCE modal stack.
Manages the open/accept/cancel/error lifecycle for all six dialog kinds.

## Ownership

`DialogModel` (in `src/ui/EditorLifecycleModel.h`) is the authority:
- `open(DialogRequest)` — opens a dialog, returns false if one is already active
- `accept(path)` — returns a typed `EditorAction` on success, or error state
- `cancel()` — closes without action
- `DialogOutcome` enum: idle / pending / accepted / cancelled / invalid / closed

`detail::runNativeFileDialog(NativeFileDialogRequest)` (declared in
`src/ui/NativeFileDialog.h`, implemented in `src/ui/VisageNativeMac.mm`)
presents the OS sheet synchronously and returns a `NativeFileDialogResult`.

`FileDialogModel::resolve(kind, result)` converts the platform result to a
typed `EditorAction` (or `nullopt` on cancel/error).

## Dialog kinds

| Kind | Action produced |
|---|---|
| importWav | `ImportWavAction { path, slot }` |
| importAts | `ImportAtsAction { path, slot }` |
| exportAts | `ExportAtsAction { path, source }` |
| savePreset | `SavePresetAction { path }` |
| loadPreset | `LoadPresetAction { path }` |
| bounceTimeline | `BounceMorphTimelineAction { path, layer }` |

## Focus contract

Dialogs block the Visage event loop (`runNativeFileDialog` returns only after
the OS sheet closes).  Focus is restored to the originating frame after both
accept and cancel paths.

Evidence:

```text
preset_file_workflow_test: exercises accept/cancel/error for savePreset/loadPreset.
file_service_preset_test: exercises importWav/importAts round-trip via injected paths.
```

Deviation: OS sheet title bar and native browse UI require visual inspection in
standalone or DAW host.

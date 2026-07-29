# UI-FILE-01 — source import execution

Status: implemented (host-neutral execution); native drag-to-decoder evidence remains planned.

`SourceImportService` is the JUCE-free import boundary used by `PluginModel`
and `HostAdapter`. It performs a cancellable `FileService` preflight, then:

- decodes `.wav`, `.wave`, `.aif`, and `.aiff` with libsndfile and downmixes
  every channel to mono analysis samples;
- loads `.ats` through `ats_load` into `AtsSoundPtr` ownership;
- rejects an invalid A/B slot, empty path, format mismatch, missing/oversized
  file, decoder failure, and cancellation with a stable error code.

The standalone, AU, and VST3 editor action handlers route `ImportWavAction`
and `ImportAtsAction` to `HostAdapter::importSource`; the action is no longer
silently ignored by those wrappers. Imported data is kept in the selected A/B
slot and is explicitly outside the audio callback.

Evidence:

```sh
arch -x86_64 cmake --build /private/tmp/ats-file-drop-native \
  --target source_import_service_test plugin_model_test host_lifecycle_test -j1
ctest --test-dir /private/tmp/ats-file-drop-native --output-on-failure \
  -R '^(source_import_service_test|plugin_model_test|host_lifecycle_test)$'
```

The service test creates a stereo WAV and proves libsndfile decoding/downmix,
then analyses and saves an ATS fixture before importing it through `ats_load`.
It also proves cancellation and extension rejection. The model and host tests
prove selected-slot ownership and typed action execution.

The real native drop harness now writes a WAV fixture, scans the embedded
`NSView` to find both A/B targets, and routes the resulting typed actions into
`HostAdapter::importSource`. It asserts a decoded, 48 kHz, four-frame source
in each slot. This is injected-pasteboard evidence because the runner cannot
use the system drag pasteboard service reliably.

This does **not** promote `UI-FILE-01` in the parity manifest: chooser
success/cancel/error is still unproven. Nor does this prove queued analysis,
playback publication, or DAW-host output.

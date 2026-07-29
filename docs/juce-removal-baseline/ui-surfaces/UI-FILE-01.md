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

This does **not** promote `UI-FILE-01` in the parity manifest. The real native
drop harness currently proves only hit-testing and typed action dispatch
(`UI-FILE-02`); it does not feed a readable file through the host handler.
Nor does this prove queued analysis, playback publication, or DAW-host output.

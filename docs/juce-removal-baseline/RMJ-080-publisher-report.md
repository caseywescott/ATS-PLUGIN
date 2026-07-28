# RMJ-080 telemetry publisher report

Task ID: RMJ-080 / publication infrastructure slice

## Contract packet

- Trace ID: `DSP-METER-001`.
- Legacy symbols/files: processor meter/spectrum caches and editor polling
  state in `src/plugin/`.
- Replacement owner/API: `MeterPublisher::publishAudioRt/snapshot` and
  bounded `SpectrumDataPublisher::publish/snapshot`.
- Thread domain: scalar meter writes may occur on `audio_rt`; spectrum writes
  are non-real-time; readers receive value snapshots only.
- State/format impact: none.
- Scope: publication infrastructure only. Renderer and live dispatch remain
  unimplemented until the backend is pinned.
- Required tests: finite/NaN handling, revision/frame counts, 720-point cap,
  and allocation-free value snapshots.
- Expected evidence: `telemetry_publishers_test`.
- Known gap: full spectrum trace construction and real UI evidence remain
  required by `DSP-VIS-001` and the UI manifest.

## Evidence

```text
cmake --build build-review --target telemetry_publishers_test -j2
./build-review/tests/telemetry_publishers_test
```

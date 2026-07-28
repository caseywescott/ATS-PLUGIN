# RMJ-060 — Playback telemetry publication report

Task packet:

- Task ID: RMJ-060 / commit 18 follow-up
- Trace IDs: `ARCH-THREAD-001`, `DSP-PLAY-001`, `DSP-METER-001`
- Legacy symbols/files: mutable preview buffers, playhead fields, output-meter and spectrum UI reads
- Replacement owner/API: `ats::audio::PlaybackTelemetryPublisher`, `SnapshotPublisher`, `MeterPublisher`, `PlayheadPublisher`
- Thread domain: audio publication and non-real-time scalar readers
- State/format impact: none
- Scope: source-current immutable playback telemetry contract
- Required tests: `tests/playback_telemetry_contract_test.cpp`, existing playback layer tests
- Expected evidence: complete scalar snapshots, revision alignment, finite/clamped values, single-writer/single-reader publication stress
- Known blocker: sanitizer/allocation and live-host evidence require the native host stage

`PlaybackTelemetryPublisher` composes the immutable scalar playback snapshot
with bounded meter and normalized playhead publishers. It contains no model
array access, allocation, file operation, or blocking path in audio
publication. Readers receive owned scalar snapshots, and the stress contract
checks that related fields are never observed from a torn publication.

Validation: focused contract test passes in the neutral CMake graph.

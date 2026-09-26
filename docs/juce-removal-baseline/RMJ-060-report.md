# RMJ-060 jobs/playback/MIDI report

Card and commit: `RMJ-060`, framework-free jobs/playback/cache contract slice

Trace IDs: `DSP-PLAY-001`, `DSP-PLAY-002`, `DSP-MIDI-001`; cache and debounce
contracts are implemented but remain below verified until process-block and
stop/start integration evidence exists.

Legacy symbols/source anchors: processor playback-buffer publication and
`PluginProcessor::processBlock` MIDI note routing.

Replacement owner/API: `src/core/PlaybackBuffers.h::PlaybackBufferPool` and
`GenerationGate` provide preallocated triple-buffer publication and stale
generation rejection. `src/audio/PlaybackCache.h` defines revision-complete
cache keys, bounded cache slots, and explicit invalidation. `src/engine/
RenderInvalidationCoordinator.h` preserves the executable 80 ms frequency,
120/350 ms morph, and 500 ms undo-grouping delays. `src/host/HostAdapter.h`
provides the neutral prepared-block lifecycle and playback publication seam.
`src/core/MidiNoteRouter.h::MidiNoteRouter` preserves
note-on, zero-velocity note-on, note-off, all-notes-off, and semitone-offset
frequency-factor behavior.

Thread and memory ownership: workers write only an inactive prepared slot;
the audio reader claims the active immutable slot and releases it after the
callback. The MIDI router is scalar callback state. No allocation or lock is
used by these APIs.

State/preset/host impact: no host adapter or plugin process path is connected;
host sample offsets are represented in the neutral event contract.

Tests and exact commands:

```text
cmake --build /tmp/ats-juce-removal-headless --target playback_buffer_test midi_note_router_test playback_cache_generation_test host_lifecycle_test
ctest --test-dir /tmp/ats-juce-removal-headless -R 'playback_buffer_test|midi_note_router_test|playback_cache_generation_test|host_lifecycle_test' --output-on-failure
```

Fixtures/goldens/evidence: publication ownership during an active reader,
generation metadata, stale-generation mismatch, cache key/revision identity,
80/120 ms debounce boundaries, octave transpose, zero-velocity note-off,
note-off, and all-notes-off.

Deviations/blockers: the real host process block, cancellation result queue
integration, click-free transitions, and allocation/race evidence remain open.

Next card prerequisites: connect `AtsEngine`, scheduler results, and cache
publication to a real process block before host wrappers are attempted.

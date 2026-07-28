# RMJ-060 playback publication report

Task ID: RMJ-060 / follow-up to commit 18

## Contract packet

- Trace IDs: `DSP-PLAY-001`, `ARCH-THREAD-001`.
- Legacy symbols/files: mutable JUCE audio-buffer swaps and processor preview
  replacement paths.
- Replacement owner/API: `PlaybackBufferPool::publish` and
  `PlaybackEngine::activeGeneration`.
- Thread domain: worker publication and `audio_rt` reader; publication uses
  atomics and preallocated slots only.
- State/format impact: none; a lower-generation render is rejected rather than
  replacing a newer active preview.
- Scope: source-current playback publication invariant.
- Required tests: newer generation publication, stale publication rejection,
  active generation read, and existing layer playback smoke.
- Expected evidence: `playback_buffer_test` and `playback_engine_test`.
- Known blocker: sanitizer/allocation stress remains part of the final host
  validation matrix.

## Evidence

```text
cmake --build build-review --target playback_buffer_test playback_engine_test -j2
ctest --test-dir build-review -R 'playback_buffer_test|playback_engine_test' --output-on-failure
```

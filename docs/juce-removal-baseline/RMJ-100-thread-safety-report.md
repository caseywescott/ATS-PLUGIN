# RMJ-100 neutral thread-safety contract

`tests/realtime/audio_thread_safety_test.cpp` stress-checks concurrent scalar
publication and reads. It verifies that meter/playhead revisions stay aligned
with the playback generation contract and that readers never observe a torn
composite snapshot.

Focused evidence:

```text
cmake --build build-review --target audio_thread_safety_test -j2
ctest --test-dir build-review --output-on-failure -R audio_thread_safety_test
```

The required ThreadSanitizer build and real native editor/window evidence
remain release-gate work after the target UI backend is pinned.

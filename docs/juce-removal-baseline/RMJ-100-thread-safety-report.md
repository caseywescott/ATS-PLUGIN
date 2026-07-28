# RMJ-100 neutral thread-safety contract

`tests/realtime/audio_thread_safety_test.cpp` stress-checks concurrent scalar
publication and reads. It verifies that meter/playhead revisions stay aligned
with the playback generation contract and that readers never observe a torn
composite snapshot.

Focused evidence:

```text
cmake --build build-review --target audio_thread_safety_test -j2
ctest --test-dir build-review --output-on-failure -R audio_thread_safety_test
cmake -S . -B /tmp/ats-juce-tsan -DBUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread -DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=thread
cmake --build /tmp/ats-juce-tsan --target audio_thread_safety_test -j2
ctest --test-dir /tmp/ats-juce-tsan --output-on-failure -R audio_thread_safety_test
```

The required ThreadSanitizer build remains release-gate work. The target UI
backend is now pinned and its real native editor/window probe passes. The
traceability ledger records the editor lifecycle and scheduler real-dispatch
rows as planned follow-on work; no headless implementation is being presented
as full widget parity evidence.

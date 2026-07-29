# UI-FILE-02 — A/B drop targets

Status: implemented

The Visage Session surface owns two transparent native drop-target frames over
the Source A and Source B cards. Both targets accept exactly one WAV/WAVE/AIF/
AIFF or ATS path. The backend classifies the extension in framework-neutral
code, reports accepted/rejected delivery through `EditorEventSink::fileDrop`,
and emits either `ImportWavAction` or `ImportAtsAction` with the target's
explicit slot (`0` for A, `1` for B).

Live evidence is `tests/ui/real_dispatch_harness_test.mm`. It embeds the real
Visage view in an `NSView`, scans its actual AppKit coordinate space until both
native target frames accept a WAV drag, records an accepted B location, then
performs an ATS drop at that exact location. The test requires typed actions
and file-drop callbacks to agree on both slots, and requires the ATS action to
reach slot B. `tests/session_page_model_test.cpp` covers uppercase WAV/WAVE,
ATS, invalid slots, multi-file rejection, and unsupported extensions.

Validated on this macOS fixture:

```text
arch -x86_64 cmake --build /private/tmp/ats-file-drop-native \
  --target real_dispatch_harness_test -j1
ctest --test-dir /private/tmp/ats-file-drop-native --output-on-failure \
  -R '^(session_page_model_test|real_dispatch_harness_test)$'
```

This evidence is intentionally limited to native target hit testing and typed
dispatch. Dialog behavior and an actual host file-service import execution are
separate UI-FILE-01 / worker-service evidence and are not claimed here.

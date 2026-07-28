# RMJ-070 neutral host implementation report

Task ID: RMJ-070 / commit 20

## Contract packet

- Trace IDs: `ARCH-HOST-001`, `ARCH-HOST-002`, `ARCH-EDITOR-001` (neutral
  lifecycle seam only).
- Legacy symbols/files: `juce::AudioProcessor`, plugin identity passed to
  `juce_add_plugin`, `prepareToPlay`, `releaseResources`, `processBlock`, and
  editor attach/resize calls in `src/plugin/`.
- Replacement owner/API: `ats::host::HostIdentity`, `HostBusLayout`, and
  `HostAdapter::{initialize,prepare,process,release,attachEditor,...}`.
- Thread domain: host callback for lifecycle/process; editor seam is host/UI
  controlled and contains no platform window object.
- State/format impact: identity, bus, MIDI, zero-latency, zero-tail, and
  editor-size contracts are explicit; host chunk translation remains in the
  native wrapper task.
- Scope: source-current neutral adapter. No SDK types are introduced.
- Required tests: identity fields, stereo/no-input bus contract, repeated
  lifecycle, MIDI/process smoke, editor attach/detach and logical sizing.
- Expected evidence: `host_lifecycle_test` and headless build.
- Known blocker: actual VST3/AU/Standalone wrappers and host scans require a
  pinned native SDK/toolchain not present in this checkout.

## Evidence

```text
cmake --build build-review --target host_lifecycle_test -j2
./build-review/tests/host_lifecycle_test
```

The neutral adapter now carries the shipping identity instead of leaving it
implicit in the JUCE build declaration. It accepts only the documented
stereo/no-input layout, reports zero latency and tail, and exposes a typed
editor attachment seam without claiming native window support.

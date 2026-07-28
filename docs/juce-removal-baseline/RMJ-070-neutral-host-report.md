# RMJ-070 neutral host implementation report

Task ID: RMJ-070 / commit 20

## Contract packet

- Trace IDs: `ARCH-HOST-001`, `ARCH-HOST-002`, `ARCH-EDITOR-001` (neutral
  lifecycle seam only).
- Legacy symbols/files: `juce::AudioProcessor`, plugin identity passed to
  `juce_add_plugin`, `prepareToPlay`, `releaseResources`, `processBlock`, and
  editor attach/resize calls in `src/plugin/`.
- Replacement owner/API: `ats::host::HostIdentity`, `HostBusLayout`,
  `HostStateChunk`, and
  `HostAdapter::{initialize,prepare,process,release,restoreState,
  attachStandaloneEditor,...}`.
- Thread domain: host callback for lifecycle/process; editor seam is host/UI
  controlled and contains no platform window object.
- State/format impact: identity, bus, MIDI, zero-latency, zero-tail, and
  editor-size contracts are explicit. `restoreState` accepts a bounded ATSP
  chunk only after validating its versioned header and replaces the published
  chunk atomically; native wrappers still own full payload decode/publication.
- Parameter impact: `HostAdapter` exposes the stable 81-row descriptor table,
  deterministic host numeric IDs, normalized automation writes, and text
  conversion without exposing APVTS/JUCE types.
- Scope: source-current neutral adapter. No SDK types are introduced.
- Required tests: identity fields, stereo/no-input bus contract, repeated
  lifecycle, MIDI/process smoke, editor attach/detach and logical sizing,
  standalone editor creation, bounded state restore, and preservation after
  rejected input.
- Expected evidence: `host_lifecycle_test` and headless build.
- Known blocker: actual VST3/AU/Standalone wrappers and host scans require a
  pinned native SDK/toolchain not present in this checkout.

## Evidence

```text
cmake --build build-review --target host_lifecycle_test -j2
./build-review/tests/host_lifecycle_test
ctest --test-dir /tmp/ats-visage-probe-arm64-release --output-on-failure -R real_dispatch_harness_test
```

The neutral adapter now carries the shipping identity instead of leaving it
implicit in the JUCE build declaration. It accepts only the documented
stereo/no-input layout, reports zero latency and tail, and exposes a typed
editor attachment seam without claiming native window support. Parameter
enumeration, normalized writes, text conversion, and invalid-ID rejection are
also covered. The host state bridge now validates the ATSP magic/version and
64 MiB bound before replacing its opaque chunk; the full legacy payload codec
and actual SDK wrappers/host scans remain the next boundary.

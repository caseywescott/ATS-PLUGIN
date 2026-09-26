# RMJ-020 framework-free primitives report

Card and commit: `RMJ-020`, working tree after baseline `36244a3`

Trace IDs changed: `ARCH-HOST-002`, `DSP-RT-001`; parameter work begins under
`ARCH-PARAM-001`.

Legacy symbols/files: JUCE audio and MIDI buffer contracts, JUCE byte/string
utility usage, `juce::LinearSmoothedValue` behavior, and processor-side job
generation assumptions.

Replacement owner/API: `src/core/AtsPrimitives.h` provides non-owning
`AudioBufferView`, sample-offset `MidiEvent`/`MidiEventList`, UTF-8 `Error` and
`Path`, explicit little-endian `ByteReader`/`ByteWriter`, `JobGeneration`,
`CancellationToken`, typed `JobResult`, `MonotonicClock`, and
`OnePoleSmoother`. `src/core/ParameterRegistry.*` owns the neutral descriptor
table and atomic plain-value store.

Thread and memory ownership: views borrow caller-owned channel memory; byte
writers and parameter snapshots allocate only outside the real-time callback;
parameter reads/writes use preallocated atomics; smoothing is scalar and
allocation-free. No UI, host SDK, filesystem, or framework type crosses these
headers.

State/preset/host impact: no state bytes or plugin identity changed. Host
translation, state codec, and preset services remain pending.

Tests and exact commands:

```text
cmake -S . -B /tmp/ats-juce-removal-headless -DBUILD_PLUGIN=OFF
cmake --build /tmp/ats-juce-removal-headless --target ats_primitives_test parameter_registry_test
ctest --test-dir /tmp/ats-juce-removal-headless -R 'ats_primitives_test|parameter_registry_test' --output-on-failure
```

Fixtures/goldens/evidence: independent expected byte sequence, 20 ms smoother
step response, generation ordering, cancellation transition, 81-descriptor
count, skew midpoint, choice text/index, integer snapping, and atomic store
round-trip in the two focused tests.

Deviations/blockers: backend remains unpinned; host/UI rows cannot be verified.
The parameter descriptor table currently records the frozen ATSA defaults as
baseline values; a source-generated independent parameter dump is still a
required RMJ-030 artifact.

Next card prerequisites: RMJ-030 needs the descriptor dump and host automation
gesture semantics; RMJ-040 needs the state field/fixture corpus.

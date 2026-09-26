# RMJ-040 state/preset/undo report

Card and commit: `RMJ-040`, framework-free state/preset/undo contract slice

Trace IDs: `ARCH-STATE-001`, `ARCH-PRESET-001`, and `ARCH-UNDO-001` are
implemented but not promoted to verified; host integration and frozen external
fixtures remain release-gate work.

Legacy symbols/source anchors: `PluginProcessor::getStateInformation`,
`PluginProcessor::setStateInformation`, `juce::UndoManager`, and the v17
field-order helpers in `src/plugin/PluginProcessor.cpp`.

Replacement owner/API: `src/state/LegacyStateCodec.h` preserves the legacy
ATSP v2–v17 field order, `src/platform/FileService.h` provides bounded reads and
atomic writes, `src/state/PresetService.h` provides the 12 factory entries and
sorted `.atspreset` indexing, and `src/core/UndoHistory.h` owns immutable
byte-snapshot transactions, redo invalidation, and bounded history.

Thread and memory ownership: codec buffers and snapshots are non-real-time;
the audio callback has no state or undo access.

State/preset/host impact: the neutral legacy codec and preset/file services are
implemented and tested without host types. Host chunk wiring, independent
frozen v2–v17 byte fixtures, embedded `.ats` fixture round-trips, and native
chooser lifecycle remain required.

Tests and exact commands:

```text
cmake --build /tmp/ats-juce-removal-headless --target legacy_state_codec_test file_service_preset_test undo_history_test
ctest --test-dir /tmp/ats-juce-removal-headless -R 'legacy_state_codec_test|file_service_preset_test|undo_history_test' --output-on-failure
```

Fixtures/goldens/evidence: generated round-trips and truncation rejection for
every readable version, atomic save/overwrite/cancellation, sorted user index,
exact twelve-entry factory table, snapshot undo/redo, and redo clearing after a
new edit.

Deviations/blockers: frozen production host-state corpus and real host/editor
integration are not yet available; these tests do not by themselves prove
byte-for-byte compatibility against every historical shipped chunk.

Next card prerequisites: capture v2–v17 binary fixtures and `.ats` fixtures,
then compare the neutral codec against them before release promotion.

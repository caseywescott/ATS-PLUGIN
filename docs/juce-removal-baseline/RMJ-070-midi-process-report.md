# RMJ-070 MIDI process-block contract

`midi_process_block_test.cpp` exercises the neutral host/process boundary with
ordered note events, explicit sample offsets, note-off reset, and zero-velocity
note-on reset. The test uses `HostAdapter`, `MidiEventList`, and
`PlaybackEngine`-compatible neutral types without a host SDK.

Native wrapper translation and host validation are still required before the
host trace can be promoted beyond `mapped`.

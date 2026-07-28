# RMJ-070/090 neutral integration contracts

The remaining framework-free integration seams are covered by the existing
contract tests: partial-edit transactions, playback telemetry/spectrum
publication, host MIDI process translation, spectral trace preparation,
residual edits, morph timeline evaluation, and MIDI transpose routing.

These tests establish the neutral data-flow and generation/transaction
contracts. They do not close the separate native-window/backend gate or claim
real target-backend dispatch evidence.

Focused evidence:

```text
ctest --test-dir build-review --output-on-failure -R 'partial_edit_transaction_test|playback_telemetry_contract_test|telemetry_publishers_test|midi_process_block_test|spectrum_trace_snapshot_test|residual_edit_service_test|morph_timeline_service_test|midi_note_router_test'
```

# RMJ-050 DSP service-boundary report

Task ID: RMJ-050 / neutral service extraction slice

## Contract packet

- Trace IDs: `DSP-ATSA-001`, `DSP-RESAMPLE-001`, `DSP-FREQ-001`,
  `DSP-TIMBRE-001`, `DSP-MORPH-001`, `DSP-MORPH-003`, `DSP-MORPH-004`,
  `DSP-MORPH-005`, `DSP-SMEAR-001`, `DSP-SMEAR-002`, `DSP-MATHEXPR-001`.
- Legacy symbols/files: algorithm calls embedded in `PluginProcessor` and
  editor code.
- Replacement owners/API: typed facades under `src/engine/AtsAnalysisService.h`
  and `src/dsp/` delegating to the existing JUCE-free core implementations.
- Thread domain: analysis/render workers for ATSA; model worker for edits and
  MathExpr; no host/UI types cross these APIs.
- State/format impact: none; facades preserve existing data structures and
  serialization functions.
- Scope: ownership extraction. Required deep cancellation, audio-golden, and
  cross-layer tests remain ledger requirements and are not claimed by this
  slice.
- Expected evidence: `dsp_service_contract_test` plus the existing algorithm
  test suite.
- Known blockers: UI dispatch and native wrappers remain externally gated.

## Evidence

```text
cmake --build build-review --target dsp_service_contract_test -j2
./build-review/tests/dsp_service_contract_test
```

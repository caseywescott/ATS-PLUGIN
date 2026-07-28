# JUCE-removal implementation status

The neutral implementation stack now includes framework-free primitives,
parameters/state/undo, scheduling and invalidation, model actions, host
lifecycle seams, playback publication, typed editor actions, telemetry,
analysis/resynthesis/DSP service facades, spectral-envelope and resampler
boundaries, MIDI process-block routing, immutable playback snapshots, and the
renderer-neutral UI scheduler.

The latest implementation commits are:

- `b15a91d` native Visage popup row-selection proof
- `45d06c4` neutral VST3/AU/standalone target seams
- `664aee0` bounded versioned host-state chunk bridge
- `6110ccf` explicit dispatcher teardown reset
- `706ac4f` neutral 81-parameter host contract

Validation on 2026-07-28:

- `ctest --test-dir build-review --output-on-failure`: 67/67 passed
- Qatsh manifest: 20/20 patch/source marker sets verified
- UI parity manifest: 53 records valid
- ATS replacement traceability: 39 records valid
- Visage native editor probe: passed at arm64 with attachment, resize, scale,
  focus, mouse, text, hide, and teardown
- Visage real-dispatch harness: passed with parameter gestures, text input,
  popup creation/focus, native popup row selection, and dismissal

The native backend prerequisite is now clear: Visage is pinned and the arm64
native embedding probe passes. `BUILD_PLUGIN=ON` still enters
`src/plugin/CMakeLists.txt`, which fetches JUCE; production VST3/AU/standalone
integration, the full widget parity port, and the complete real-dispatch
matrix remain open. Neutral host state, parameter, and lifecycle seams now
compile through explicit format targets, and the real popup path is covered.
The JUCE removal/build-artifact gates must not be claimed complete until those
follow-on gates pass.

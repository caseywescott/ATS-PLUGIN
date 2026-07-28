# JUCE-removal implementation status

The neutral implementation stack now includes framework-free primitives,
parameters/state/undo, scheduling and invalidation, model actions, host
lifecycle seams, playback publication, typed editor actions, telemetry,
analysis/resynthesis/DSP service facades, spectral-envelope and resampler
boundaries, MIDI process-block routing, immutable playback snapshots, and the
renderer-neutral UI scheduler.

The latest implementation commits are:

- `b15a91d` native Visage popup row-selection proof
- `612a15b` standalone editor lifecycle seam and probe
- `0a9b336` replacement-editor detach invariant
- `05a1fcb` stale RMJ-010 backend-blocker correction
- `ee8d3ee` native Visage page-selector and parameter-gesture dispatch proof
- `d0aae9a` model-driven Visage shell top-bar state for history, theme, and activity
- `b6b05a1` Visage Session spectrum canvas and seven-mode view-menu implementation
- `ac111be` native spectrum wheel/drag navigation wiring
- `f4d9943` source-current ANL/RND activity indicators
- `45d06c4` neutral VST3/AU/standalone target seams
- `664aee0` bounded versioned host-state chunk bridge
- `6110ccf` explicit dispatcher teardown reset
- `706ac4f` neutral 81-parameter host contract

Validation on 2026-07-28:

- `ctest --test-dir build-review --output-on-failure`: 67/67 passed
- Qatsh manifest: 20/20 patch/source marker sets verified
- UI parity manifest: 53 records valid
- UI parity baseline/backend inputs frozen to the ATS baseline commit and
  pinned Visage revision
- ATS replacement traceability: 39 records valid; native editor lifecycle
  verified and UI backend/scheduler seams promoted to implemented
- Visage native editor probe: passed at arm64 with attachment, resize, scale,
  focus, mouse, text, hide, standalone window creation, and teardown
- Visage real-dispatch harness: passed with parameter gestures, text input,
  native page selection, analysis-frequency begin/drag/end, popup
  creation/focus, native popup row selection, and dismissal
- Visage Session now renders the framework-free spectrum renderer and exposes
  the seven-mode view-menu event path; native popup-coordinate evidence for
  that path remains a follow-on probe. Native wheel/drag traffic is wired to
  the spectrum model’s zoom, pan, and pointer-capture state, but direct native
  gesture evidence remains a follow-on probe.
- Visage shell top-bar state now renders history enablement, theme selection,
  and analysis/render activity from the neutral shell model; native history and
  theme click dispatch remain a follow-on probe because their coordinate path
  is not yet stable enough to claim as native evidence

The native backend prerequisite is now clear: Visage is pinned and the arm64
native embedding probe passes. `BUILD_PLUGIN=ON` still enters
`src/plugin/CMakeLists.txt`, which fetches JUCE; production VST3/AU/standalone
integration, the full widget parity port, and the complete real-dispatch
matrix remain open. Neutral host state, parameter, and lifecycle seams now
compile through explicit format targets, and the real popup path is covered.
The JUCE removal/build-artifact gates must not be claimed complete until those
follow-on gates pass.

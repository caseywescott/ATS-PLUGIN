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
- `e343c1d` backend-neutral macOS native file-dialog bridge
- current native VST3 wrapper slice: pinned Steinberg SDK artifact and
  lifecycle/process test
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
- The Session source-card path now routes import requests through a
  backend-neutral result seam and macOS `NSOpenPanel`/`NSSavePanel`; automated
  modal accept/cancel evidence remains open.

- The first real JUCE-free VST3 artifact now builds against Steinberg VST3 SDK
  commit `05c4a9763f311a58fe3f34b68a199e5eea92e2b4` (tag
  `v3.7.6_build_18`) and passes `ats_vst3_artifact_test` for identity, 81
  parameters, no-input stereo setup, state header, lifecycle, and process
  silence. This follows the Mearcstapa pinned-dependency pattern and removes
  the former “no native SDK” blocker for VST3 only.

- The VST3 controller now creates a Visage-backed `IPlugView`. The native
  `ats_vst3_editor_view_probe` attaches that view to a Cocoa `NSView`, verifies
  focus and logical resize, and removes it cleanly. Together with the pinned
  Visage editor and real-dispatch probes, this clears native VST3 editor
  attachment; AU, standalone, DAW scan, and complete automation/stress gates
  remain open.

- The next host slice adds a JUCE-free `ATS Resynth.app` standalone bundle and
  a non-bundled smoke entry point using `HostAdapter::attachStandaloneEditor`.
  Both arm64 targets build. Runtime smoke is not promoted here because this
  desktop session traps inside Visage native window creation; the existing
  arm64 Visage standalone probe remains the required window-capable evidence.

- The AU half of commit 22 now builds a JUCE-free `ATS Resynth.component`
  against pinned Apple AudioUnitSDK commit
  `53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9` and passes the direct factory
  artifact probe for identity, all-81 parameter info/read/write, stereo float
  format, ATSP class-info state round-trip, zero-input stereo process selector,
  initialize, uninitialize, and close. AU registration/`auval`, MIDI/
  automation stress, and a native AU editor remain open.

The native backend prerequisite is now clear: Visage is pinned and the arm64
native embedding probe passes. `BUILD_PLUGIN=ON` still enters
`src/plugin/CMakeLists.txt`, which fetches JUCE; production VST3/AU/standalone
integration, AU/standalone wrappers, the full widget parity port, and the
complete real-dispatch matrix remain open. Neutral host state, parameter, and lifecycle seams now
compile through explicit format targets, and the real popup path is covered.
The JUCE removal/build-artifact gates must not be claimed complete until those
follow-on gates pass.

# JUCE-removal implementation status

The neutral implementation stack now includes framework-free primitives,
parameters/state/undo, scheduling and invalidation, model actions, host
lifecycle seams, playback publication, typed editor actions, telemetry,
analysis/resynthesis/DSP service facades, spectral-envelope and resampler
boundaries, MIDI process-block routing, immutable playback snapshots, and the
renderer-neutral UI scheduler.

The latest implementation commits are:

- `3965590` neutral DSP services and telemetry publishers
- `a14d729` snapshot publication and UI scheduler contracts
- `bfc5a2c` spectral-envelope service boundary
- `bcc6227` neutral MIDI process-block routing
- `8491bc2` analysis cancellation and resampler contracts

Validation on 2026-07-28:

- `ctest --test-dir build-review --output-on-failure`: 47/47 passed
- Qatsh manifest: 20/20 patch/source marker sets verified
- UI parity manifest: 53 records valid
- ATS replacement traceability: 39 records valid

Completion is blocked at the native wrapper/editor stage. `BUILD_PLUGIN=ON`
still enters `src/plugin/CMakeLists.txt`, which fetches JUCE; no pinned
JUCE-free renderer, VST3/AU SDK integration, or real-window dispatch harness
exists in this workspace. The UI/backend and host rows therefore remain
conservatively mapped/planned, and the JUCE removal/build-artifact gates must
not be claimed complete until that dependency and revision are supplied.

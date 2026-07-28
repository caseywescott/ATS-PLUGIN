# RMJ-000 baseline report

Card and commit: `RMJ-000`, source baseline `36244a3ff041491d9a8cc4f5bf641105b52c8e35`

Trace IDs changed: baseline evidence for all existing architecture/UI rows; no
replacement row is promoted beyond `mapped` by this report.

Files added/removed/moved: baseline records only.

Tests run and exact commands:

```text
python3 tests/architecture/traceability_manifest_check.py
python3 tests/ui/ui_parity_manifest_check.py
cmake -S . -B /tmp/ats-juce-removal-headless -DBUILD_PLUGIN=OFF
cmake --build /tmp/ats-juce-removal-headless
ctest --test-dir /tmp/ats-juce-removal-headless --output-on-failure
```

Expected-value source: JUCE baseline source at the commit above, with the
pre-existing dirty worktree preserved and described in `environment.md`.

Thread/allocation findings: headless targets build; the plugin target was not
used for this baseline. No claim is made about the JUCE audio callback.

Visual/live evidence: not frozen. The replacement UI backend and dispatcher
revision are not present in the repository.

Open deviations: `D-BASELINE-DIRTY` (the source checkout was already dirty),
`D-UI-GEOMETRY-UNRESOLVED`
(1180x820 source versus 1280x940 design target), and the documented-only
keyboard/output-spectrum/output-meter surfaces listed in the specification.

Next card and prerequisites: RMJ-020 may proceed with headless primitives.
RMJ-010, RMJ-080, RMJ-090, and RMJ-100 require a selected, pinned backend,
real-window probe, and host capability decision.

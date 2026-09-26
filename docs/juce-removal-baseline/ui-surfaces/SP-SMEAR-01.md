# SP-SMEAR-01 — Spectrum onset overlay: drag-update dispatch

Status: implemented

Replaces `SpectrogramPanel::onsetHandleAtPoint()` / `updateDraggedOnset()`
JUCE drag-tracker.

## Ownership

`SpectrumInteractionModel::setOnsetOverlay(std::vector<float> onsets)` returns an
`EditSmearnovOnsetsAction{normalizedOnsets}` wrapping a complete onset list.

The caller supplies the full list (not a delta); the action replaces the stored
overlay in one transaction. This matches the legacy `setOnsets()` call which
committed the entire updated set on every drag event.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_onset_overlay` verifies: returned action carries the full normalised onset
list; size and value passthrough are correct.

Deviations: drag-handle hit detection and screen-to-normalised mapping require
the Visage native event harness.

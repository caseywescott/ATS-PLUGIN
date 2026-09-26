# SP-SMEAR-02 — Spectrum onset toggle: double-click add/remove

Status: implemented

Replaces `SpectrogramPanel::mouseDoubleClick()` onset insert/remove logic.

## Ownership

`SpectrumInteractionModel::toggleOnset(std::vector<float> onsets, float norm, float snapRadius)`
returns an `EditSmearnovOnsetsAction`.

- If any existing onset is within `snapRadius` (default 0.02) of `norm`, that
  onset is removed (remove-in-place).
- Otherwise `norm` is inserted and the list is sorted.
- The caller supplies the current onset list and receives a new list back;
  no internal state is mutated by `toggleOnset`.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_toggle_onset` verifies: add inserts sorted; remove-within-snapRadius
removes the nearest onset; add to empty list; custom snapRadius not tested
(default used).

Deviations: double-click pixel-to-time mapping and onset handle rendering
require the Visage native event harness.

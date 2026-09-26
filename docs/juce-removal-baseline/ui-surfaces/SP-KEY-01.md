# SP-KEY-01 — Spectrum keyboard dispatch

Status: implemented

Replaces `SpectrogramPanel::keyPressed()` JUCE `KeyPress` handler.

## Ownership

`SpectrumInteractionModel::handleKey(int keyCode, bool shift, bool cmd)` returns
`std::optional<EditorAction>`.

| Key | Effect |
|---|---|
| `kKeyFit ('0')` | `fit()` — reset navigation; returns `nullopt` |
| `kKeyHome (36)` | same as Fit |
| `kKeyEscape (27)` | clear `selectedPartial`, `selectedBand`, `selectedBands`; returns `nullopt` |
| `kKeyDelete (127)` | if `selectedPartial >= 0`: `PartialEditAction{partial, 0, false}`; else if `selectedBand >= 0`: `ResidualEditAction{band, 0, 0.0f}`; else `nullopt` |
| `kKeyBackspace (8)` | same as Delete |
| any other | `nullopt` |

`shift` and `cmd` are forwarded but not yet consumed; they are reserved for
future nudge / range-select operations.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_handle_key` verifies: kKeyFit resets zoom; kKeyHome resets zoom; Escape
clears all selection; Delete with partial → PartialEditAction; Delete with band →
ResidualEditAction; Delete with nothing → nullopt; Backspace == Delete; unknown
key → nullopt.

Deviations: live key-event dispatch from the OS through Visage to the canvas
focus owner requires the native event harness.

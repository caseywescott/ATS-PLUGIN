# SP-MENU-01 — Spectrum view menu: 7-item mode list

Status: implemented

Replaces `SpectrogramPanel::showViewMenu()` JUCE `PopupMenu` construction.

## Ownership

`SpectrumInteractionModel::menu(bool hasSource, bool hasResidual)` (in
`src/ui/SpectrumInteractionModel.h`) returns a
`std::array<SpectrumMenuItem, kModeCount>` (kModeCount = 7) without touching any
JUCE type. Each item carries `id`, `mode`, `label`, `checked`, and `enabled`.

## Item table

| id | Mode | Label |
|---|---|---|
| 0 | `combined` | `"Combined"` |
| 1 | `partials` | `"Partials"` |
| 2 | `residual` | `"Residual"` |
| 3 | `isometric` | `"Isometric"` |
| 4 | `isometricResidual` | `"Isometric residual"` |
| 5 | `isometricEnvelope` | `"Isometric envelope"` |
| 6 | `isometricEnvelopeRdc` | `"Isometric envelope RDC"` |

`checked` is true for the item matching `mode_`. `enabled` is `hasSource &&
(mode != residual || hasResidual)`.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_menu_items` verifies all 7 labels. `test_menu_checked_and_enabled` verifies
checked state and residual-disabled gate.

Deviations: live popup render and mouse-pick dispatch require the Visage native
dispatch harness.

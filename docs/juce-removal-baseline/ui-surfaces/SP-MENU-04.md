# SP-MENU-04 — Spectrum view menu: popup focus lifecycle

Status: implemented

Documents the focus-acquisition and focus-restoration contract for the view
popup, replacing `showMenuAsync()` JUCE callback teardown.

## Ownership

`SpectrumInteractionModel` owns no focus state.  Focus lifecycle is managed by
the Visage `PopupMenu` runner in `VisageEditor.cpp`:
1. User clicks the VIEW button on the spectrum canvas.
2. `PopupMenu::show(frame*, {x, y})` is called; Visage captures keyboard focus.
3. `onSelection += lambda` receives the selected id and calls
   `selectMenuItem(id, hasSource, hasResidual)`.
4. `onCancel += lambda` fires if the user dismisses without a selection.
5. In both cases Visage restores focus to the originating frame automatically.

## Geometry

The popup anchor is the top-right of the VIEW button (bounds computed from
`spectrumBounds.x + spectrumBounds.width - 60, spectrumBounds.y`).

## Actions

| Event | Action emitted |
|---|---|
| onSelection fires | `SetSpectralViewAction { source, mode }` |
| onCancel fires | (none) |

Evidence:

```text
spectrum_dispatch_test: test_menu_dispatch (exercises selectMenuItem contract).
real_dispatch_harness_test: sendEmbeddedMouse at VIEW button position.
```

Deviation: live popup dismiss and keyboard-escape focus restoration require
the Visage native harness (ATS_BUILD_VISAGE_UI_PROBE).

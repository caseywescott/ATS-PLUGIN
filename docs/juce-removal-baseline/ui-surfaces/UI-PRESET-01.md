# UI-PRESET-01 — Preset combo: factory list, user list, and selection dispatch

Status: verified (2026-09-27)

Replaces `PresetLibrary` JUCE `ComboBox` item population and
`SessionTabPanel::presetComboChanged()`.

## Ownership

`PresetInteractionModel` (in `src/ui/PresetInteractionModel.h`) owns all preset
selection dispatch. `PresetService::entries()` (in `src/state/PresetService.h`)
owns entry-list construction.

## Entry list

`PresetService::entries()` returns:
- 12 factory entries (indices 0..11) with `factory == true` and
  `factoryIndex == i`. Names are prefixed `"Factory: "`.
- User entries (sorted by filename stem) with `factory == false`. Names are
  prefixed `"User: "`.

Factory entries always precede user entries.

## Selection dispatch

| Row type | `PresetInteractionModel` call | Action produced |
|---|---|---|
| Factory (index 0..11) | `selectFactory(index)` | `ApplyFactoryPresetAction{index}` |
| User (non-empty path) | `selectUserPreset(path)` | `LoadPresetAction{path}` |

`selectFactory(-1)` and `selectFactory(12)` both return `nullopt` — the combo
widget must not fire for out-of-range selections. `selectUserPreset({})` returns
`nullopt`.

`kFactoryCount == 12` is the single authoritative constant.

Evidence:

```text
arch -x86_64 cmake --build build-review --target preset_file_workflow_test -j4
ctest --test-dir build-review --output-on-failure -R '^preset_file_workflow_test$'
```

`test_factory_selection` verifies all 12 indices, boundary rejection at -1 and
12. `test_user_preset_selection` verifies non-empty path dispatch and empty
rejection. `test_preset_entries_list` verifies 12 factory entries before any
user saves, sorted user order after saves.

Deviations: combo-widget rendering, popup focus management, and selection-event
routing from the native Visage combo widget require the live dispatch harness.

## Verification (2026-09-27, `f2057a51`)

RMJ-032 step 10: presets popup lists legacy names and applies 'Bright partials' (ApplyFactoryPreset, partGain 1.6); factory_preset_state_test 12 x 81 values vs legacy.

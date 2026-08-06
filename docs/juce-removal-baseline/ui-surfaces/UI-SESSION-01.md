# UI-SESSION-01 — Source card panel (left column)

Status: implemented

Replaces the `SessionTabPanel` source card pair in `PluginEditor.cpp`.
Two source cards (A and B) each show a slot label, source name or
"DROP WAV / ATS" placeholder, and three action buttons (ANALYZE / WAV / ATS).

## Ownership

`SessionPageModel::state().sources[0/1]` (a `SessionSourceCard`) is the
authority for each card.  `SourceDropTarget` child frames handle drag-and-drop;
they report results via `acceptSourceDrop(slot, paths)` which calls
`ImportWavAction` or `ImportAtsAction`.

## Geometry (§10.4 left column)

| Element | Bounds (relative to left column top) | Height |
|---|---|---|
| SOURCES heading | inset 14 | 16 px |
| Source A card | below heading | 56 px |
| gap | | 8 px |
| Source B card | below A card + gap | 56 px |

Source card x-positions in the 660 px body: A at 18, B at 330, each 300 wide.
Card height is 68 px; the drop zone occupies the full card area.

Inside each card:
- Slot badge (A/B), 18×14, at card_x+8, 120
- Source name label, 254 wide, at card_x+34, 120
- Button row (ANALYZE 88·WAV 92·ATS 92) at card_y+148, 24 px tall, gap 4

## Actions

| User gesture | Action emitted |
|---|---|
| Drop `.wav`/`.wave`/`.aif`/`.aiff` file | `ImportWavAction { path, slot }` |
| Drop `.ats` file | `ImportAtsAction { path, slot }` |
| Click ANALYZE button | `AnalyzeMorphSourceAction { slot }` |
| Click WAV button | `ImportWavAction` via native file dialog |
| Click ATS button | `ImportAtsAction` via native file dialog |

File extension filtering (`\.ats|\.wav|\.wave|\.aif|\.aiff|…`) is enforced
by `SourceDropTarget::dragDropFileExtensionRegex()`.

Evidence:

```text
real_dispatch_harness_test: exercises both slots via performNativeDrop(),
verifies fileDrop / typedImportWavActions / sourceDropCounts[0/1].
source_import_service_test: audio decode path (4-sample WAV).
```

Deviation: none.

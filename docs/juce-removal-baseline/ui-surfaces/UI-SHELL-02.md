# UI-SHELL-02 — Undo / Redo buttons

Status: verified (2026-09-27)

Replaces the `TextButton` undo/redo pair in `PluginEditor.cpp`.  The two
buttons live in the top-bar right cluster, separated by a 4 px gap.

## Ownership

`EditorShellModel::state().canUndo` and `canRedo` are the authority.
`VisageEditor::setHistoryAvailability(canUndo, canRedo)` pushes updated
state from the host; the frame re-draws immediately with no secondary copy.

## Geometry (§10.2 right cluster, right-to-left)

| Control | Width | Height | Position |
|---|---|---|---|
| REDO button | 28 px | 30 px | far right of cluster |
| gap | 4 px | — | |
| UNDO button | 28 px | 30 px | left of REDO |

Both buttons are vertically centered on the top bar (46 px).

## Interaction

`ProbeFrame::onMouseDown` checks `point.y < 48` and the x range for each
button before dispatching `HistoryAction { false }` (undo) or
`HistoryAction { true }` (redo).  Only dispatches when the corresponding
`canUndo` / `canRedo` flag is true.

The `AtsToggle` widget is NOT used for undo/redo because the action is not
a parameter gesture — it is a one-shot command with no value payload.

## Action protocol

`HistoryAction { redo: bool }` on `ActionThreadDomain::worker`.  No begin/end
gesture wrapping; the action is atomic.

Evidence:

```text
real_dispatch_harness_test: full-stack test exercises the probe popup path,
confirming ActionSink dispatch from the embedded view.
editor_page_models_test: confirms canUndo/canRedo state transitions.
```

Deviation: none.

## Verification (2026-09-27, `f2057a51`)

RMJ-032 steps 11-12: header UNDO/REDO emit HistoryAction and revert/re-apply the factory preset; buttons enabled from pumped snapshot history.

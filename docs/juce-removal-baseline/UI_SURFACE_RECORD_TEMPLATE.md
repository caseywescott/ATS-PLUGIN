# UI surface record — `<SURFACE-ID>`

Copy this file to `docs/juce-removal-baseline/ui-surfaces/<SURFACE-ID>.md` and
replace every placeholder before the surface is promoted beyond `planned`.
Keep source behavior and target implementation in separate columns. Never write
“obvious,” “standard,” or “same as source” where a testable rule is required.

## Identity and authority

```text
surface_id: <UI-* or SP-*>
baseline_scope: source_current | documented_only | promoted_target | design_reference
source_commit: <exact commit or TO_BE_FROZEN>
target_backend_revision: <exact revision or TO_BE_PINNED>
source_files_and_line_anchors:
  - <path>:<symbol or line>
related_trace_ids:
  - <ARCH-* / DSP-* / UI-* / SP-*>
deviation_id: null | <D-*>
```

## Ownership map

```text
visual_owner: <renderer/view-model owner>
event_owner: <deepest hit frame or dispatcher owner>
state_owner: <parameter/model/session/popup state owner>
popup_or_overlay_parent: <top-level parent, none, or click-through overlay owner>
z_order_and_click_through_rule: <explicit order and hit-test behavior>
input_coordinate_space: <screen/window/logical/local frame/data coordinates>
pointer_capture_rule: <who captures, when capture starts, how it ends>
keyboard_focus_owner: <frame/service>
```

## Source contract / target mechanism

| Contract | Frozen source behavior | Target implementation mechanism | Proof |
|---|---|---|---|
| Hit testing |  |  |  |
| Local coordinates |  |  |  |
| Movement delta |  |  |  |
| Click |  |  |  |
| Drag begin/value/end |  |  |  |
| Pointer capture |  |  |  |
| Modifiers/fine control |  |  |  |
| Wheel/pinch |  |  |  |
| Double-click/reset |  |  |  |
| Text entry/validation |  |  |  |
| Focus gain/loss/commit |  |  |  |
| Popup open/select/cancel/dismiss |  |  |  |
| Disabled/dimmed/greyed state |  |  |  |
| Tooltip/help delay and click-through |  |  |  |
| Resize/DPI/reflow |  |  |  |
| Host automation feedback |  |  |  |

## Product action and data contract

```text
parameter_ids_or_typed_actions:
  - <stable parameter ID or typed command>
begin_gesture:
  - <owner, notification, and cancellation>
commit:
  - <exact model/state mutation and undo label/grouping>
cancel_or_error:
  - <state/file/model behavior; exact error code/text>
worker_or_audio_boundary:
  - <thread, generation, cancellation, publication rule>
telemetry_and_revision_source:
  - <producer, snapshot field, cadence, revision>
enabled_dimmed_greyed_states:
  - <condition and visual/hit-test behavior>
exact_user_visible_copy:
  - <literal label/tooltip/menu/dialog text, or explicitly none>
```

## Visual contract

```text
geometry:
  design:
  minimum:
  maximum:
  wide/tall behavior:
  parented/unparented behavior:
  1x/2x behavior:
colors_and_alpha:
typography:
draw_order_and_cache_layers:
hover_pressed_focused_disabled_error_states:
empty/loading/ready/error states:
```

## Evidence contract

```text
offscreen_or_live_evidence_class: pure | offscreen | hook | dispatch | live | host
fixture_or_injection_hook:
automated_test_ids:
manual_evidence_id:
expected_artifacts:
  - <screenshot/geometry dump/dispatch trace/audio/state report>
known_deviations_or_blockers:
```

## Review checklist

- [ ] Source file/symbol and baseline commit are pinned.
- [ ] The actual hit target is identified; painted regions are not assumed to
      be child controls.
- [ ] Local position, window position, and movement delta are distinguished.
- [ ] Capture, focus, popup parent, outside dismissal, and teardown are explicit.
- [ ] Exact copy includes intentionally absent tooltips/help strings.
- [ ] Data source and refresh cadence are named; no synthetic animation is used.
- [ ] Action reaches the typed model/engine path and has a result/error path.
- [ ] Undo and state/preset participation are explicit.
- [ ] Min/design/max, wide/tall, parented/unparented, and 1x/2x behavior are
      recorded where applicable.
- [ ] At least one real-dispatch test exists for every interactive behavior.
- [ ] Deviations are recorded before implementation changes behavior.

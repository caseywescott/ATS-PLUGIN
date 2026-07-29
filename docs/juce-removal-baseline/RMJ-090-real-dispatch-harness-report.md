# RMJ-090 real-dispatch harness report

The first live dispatch slice now runs through the pinned Visage backend rather
than the headless test double. `real_dispatch_harness_test.mm` creates a native
macOS host window, embeds `ats::ui::VisageEditor` through `HostAdapter`, and
sends native mouse and keyboard events to the embedded view.

The harness verifies that:

- the framework-free `EditorShellModel` preserves the five hard-swapped pages,
  Ember/Porcelain theme selection, history enablement, and activity indicators;
- host attach, scale, resize, show/hide, detach, and destruction forward through
  the neutral `IEditor` seam;
- a native mouse gesture becomes typed `SetParameterAction` begin/end events;
- the Visage backend emits those typed actions through the backend-neutral
  `EditorEventSink::action` seam, independently of the probe-only gesture
  callback;
- native text input becomes a typed `ApplyMathAction`;
- the same native interaction posts an editor refresh through `UiScheduler`;
- scheduler cadence and source revision are retained;
- native focus is observed before a popup takes focus;
- a native right-click creates a Visage `PopupMenu` and hands focus to it; and
- a native left-click on the first popup row produces the real option-selection
  and dismissal callbacks; and
- a native click on the Visage page selector changes the shell page; and
- a native analysis-frequency mouse down/drag/up produces a bounded parameter
  gesture with begin/end markers; and
- the real shell top bar renders model-driven undo/redo enablement, Ember /
  Porcelain theme state, and analysis/render activity indicators; and
- the Session surface now owns a real Visage spectrum canvas using the
  framework-free renderer, fixed-dark palette, empty-safe grid, and seven-mode
  interaction model with a view-menu event path; native wheel and drag paths
  now update the model’s zoom, pan, and pointer-capture state; and
- the Session page model owns source A/B cards, preset dirty/save state,
  transport/layer selection, normalized morph timeline, variation/policy
  enablement, timbre curve, and Smearnov detection/onset overlay state; and
- the real shell renders the Session source drop targets and transport summary
  from that framework-free model rather than from widget state.
- the Analysis model exposes all 18 stable analysis controls, staged profile /
  custom selection, source action, and worker cancellation state;
- the Synthesis model keeps Linear shape dimming, `FULL` duration semantics,
  and playback options explicit;
- the Partials model validates bounded partial/frame cell edits before creating
  a domain command; and
- the Math model produces a typed Apply action and uses an explicit 600 ms
  result deadline, so success feedback is not completed by a UI timer.
- the spectrum interaction model preserves the seven mode IDs and labels,
  source-aware enablement/check state, normalized zoom/pan/fit bounds, pointer
  capture, and typed partial/residual/Smearnov edit payloads.
- dialog requests now map import/export/preset/bounce selections to typed
  actions with cancellation and duplicate-open guards; close transitions clear
  dialogs, popup ownership, pointer capture, and focus before detaching.
- the Session source-card import path now has a backend-neutral dialog request /
  result seam backed by macOS `NSOpenPanel`/`NSSavePanel`, with explicit
  accepted versus cancelled results and parent-window ownership; and
- the deterministic spectrum renderer emits a fixed-dark canvas for both
  themes, ordered grid/residual/trace/playhead/legend commands, amplitude
  weighting, selected-trace emphasis, and bounded playhead coordinates.
- hide/destruction removes the embedded Visage view.
- dispatcher teardown explicitly clears active pointer capture, popup ownership,
  and focus before the native editor can be detached.

Evidence:

```text
cmake --build /tmp/ats-visage-probe-arm64-release \
  --target real_dispatch_harness_test -j2
ctest --test-dir /tmp/ats-visage-probe-arm64-release \
  --output-on-failure -R real_dispatch_harness_test
ctest --test-dir build-review --output-on-failure -R editor_shell_test
ctest --test-dir build-review --output-on-failure -R session_page_model_test
ctest --test-dir build-review --output-on-failure -R editor_page_models_test
ctest --test-dir build-review --output-on-failure -R spectrum_interaction_model_test
ctest --test-dir build-review --output-on-failure -R spectrum_trace_snapshot_test
ctest --test-dir build-review --output-on-failure -R editor_lifecycle_model_test
ctest --test-dir build-review --output-on-failure -R ui_ownership_contract_test
ctest --test-dir build-review --output-on-failure -R spectrum_renderer_test
```

Result: the updated real-dispatch harness passed in an isolated desktop
session, including page selection, the analysis-frequency gesture, and the
typed action callback receiving bounded begin/end `SetParameterAction`
variants. The
combined GUI invocation can miss the popup row under LaunchServices timing;
the native popup scan is therefore rerun in isolation for evidence. The
shell/session/page/spectrum/lifecycle model and renderer tests also pass.
This is the foundational real-dispatch proof. Top-bar history and theme click
dispatch are intentionally not counted as native evidence yet because the
coordinate path is not stable across the current native embedding probe. The
new spectrum canvas/menu is likewise model- and render-covered in this slice,
with native wheel/drag wiring compile-validated alongside the existing
lifecycle harness; direct spectrum wheel/drag and popup-coordinate evidence
remain follow-on probes. Dialogs, widget parity, and production host-wrapper
artifacts remain follow-on work. The native modal chooser itself is compile-
validated but its automated accept/cancel interaction remains a follow-on
probe because the current harness does not drive modal panels.

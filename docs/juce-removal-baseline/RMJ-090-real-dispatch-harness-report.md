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
- native text input becomes a typed `ApplyMathAction`;
- the same native interaction posts an editor refresh through `UiScheduler`;
- scheduler cadence and source revision are retained;
- native focus is observed before a popup takes focus;
- a native right-click creates a Visage `PopupMenu` and hands focus to it; and
- the Session page model owns source A/B cards, preset dirty/save state,
  transport/layer selection, normalized morph timeline, variation/policy
  enablement, timbre curve, and Smearnov detection/onset overlay state; and
- the real shell renders the Session source drop targets and transport summary
  from that framework-free model rather than from widget state.
- hide/destruction removes the embedded Visage view.

Evidence:

```text
cmake --build /tmp/ats-visage-probe-arm64-release \
  --target real_dispatch_harness_test -j2
ctest --test-dir /tmp/ats-visage-probe-arm64-release \
  --output-on-failure -R real_dispatch_harness_test
ctest --test-dir build-review --output-on-failure -R editor_shell_test
ctest --test-dir build-review --output-on-failure -R session_page_model_test
```

Result: native probes `2/2` passed and the shell/session model tests passed.
This is the foundational real-dispatch proof; popup row
selection, dialogs, pointer capture, widget parity, and production host-wrapper
artifacts remain follow-on work.

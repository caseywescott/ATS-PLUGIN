# RMJ-090 real-dispatch harness report

The first live dispatch slice now runs through the pinned Visage backend rather
than the headless test double. `real_dispatch_harness_test.mm` creates a native
macOS host window, embeds `ats::ui::VisageEditor` through `HostAdapter`, and
sends native mouse and keyboard events to the embedded view.

The harness verifies that:

- host attach, scale, resize, show/hide, detach, and destruction forward through
  the neutral `IEditor` seam;
- a native mouse gesture becomes typed `SetParameterAction` begin/end events;
- native text input becomes a typed `ApplyMathAction`;
- the same native interaction posts an editor refresh through `UiScheduler`;
- scheduler cadence and source revision are retained;
- native focus is observed before a popup takes focus;
- a native right-click creates a Visage `PopupMenu` and hands focus to it; and
- hide/destruction removes the embedded Visage view.

Evidence:

```text
cmake --build /tmp/ats-visage-probe-arm64-release \
  --target real_dispatch_harness_test -j2
ctest --test-dir /tmp/ats-visage-probe-arm64-release \
  --output-on-failure -R real_dispatch_harness_test
```

Result: `1/1` passed. This is the foundational real-dispatch proof; popup row
selection, dialogs, pointer capture, widget parity, and production host-wrapper
artifacts remain follow-on work.

# RMJ-090 real-dispatch harness report

The first live dispatch slice now runs through the pinned Visage backend rather
than the headless test double. `real_dispatch_harness_test.mm` creates a native
macOS host window, embeds `ats::ui::VisageEditor`, and sends native mouse and
keyboard events to the embedded view.

The harness verifies that:

- a native mouse event becomes a typed `SetSpectralViewAction`;
- native text input becomes a typed `ApplyMathAction`;
- the same native interaction posts an editor refresh through `UiScheduler`;
- scheduler cadence and source revision are retained;
- native focus is observed; and
- hide/destruction removes the embedded Visage view.

Evidence:

```text
cmake --build /tmp/ats-visage-probe-arm64-release \
  --target real_dispatch_harness_test -j2
ctest --test-dir /tmp/ats-visage-probe-arm64-release \
  --output-on-failure -R real_dispatch_harness_test
```

Result: `1/1` passed. This is the foundational real-dispatch proof; the full
popup, dialog, pointer-capture, widget-parity, and host-wrapper matrix remains
follow-on work.

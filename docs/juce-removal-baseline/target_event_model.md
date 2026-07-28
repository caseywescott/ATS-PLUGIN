# RMJ-010 target event model status

Status: backend gate clear; full parity remains follow-on work.

Visage is pinned at `20de59464243447816d142e9d38e9723d068f755`. The native
probe and real-dispatch harness now exercise the actual ApplicationWindow
through an embedded NSView and standalone window, including resize, scale,
focus, mouse/text input, popup focus/selection/dismissal, hide, and teardown.
Headless contracts remain supplemental and are not used as proof of native
behavior.

The required proof, once a backend is selected, must answer and live-test:

1. z-ordered child traversal and the deepest hit target;
2. target-local position versus inter-event movement delta;
3. pointer capture outside the original bounds;
4. ancestor observation without coordinate rebasing ambiguity;
5. popup attachment, focus transfer, dismissal, and focus restoration;
6. modal/file-dialog ownership and cancellation;
7. logical-to-device mapping at 1x, 2x, and mixed-DPI scales.

RMJ-010 is no longer blocked by backend selection. The remaining UI work is
full widget/page parity, native dialog/file behavior, screenshot/geometry
evidence, and production host-wrapper integration.

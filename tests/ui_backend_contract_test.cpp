#include "../src/ui/UiBackend.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::ui::UiScale scale { 2.0f };
    const auto device = scale.toDevice({ 10.0f, 4.0f });
    assert(device.x == 20.0f && device.y == 8.0f);
    const auto logical = scale.toLogical(device);
    assert(std::abs(logical.x - 10.0f) < 1.0e-6f);

    ats::ui::UiBackendCapabilities incomplete;
    assert(!incomplete.completeForEditor());
    incomplete.targetLocalPosition = incomplete.targetLocalDelta = true;
    incomplete.pointerCapture = incomplete.ancestorObservation = incomplete.zOrder = true;
    incomplete.popupRoot = incomplete.modalFocus = incomplete.logicalDevicePixels = true;
    incomplete.dpiScale = incomplete.embeddedParent = incomplete.standaloneWindow = true;
    incomplete.nativeDialogs = incomplete.nativeTextEntry = incomplete.resize = true;
    incomplete.teardownCancellation = true;
    assert(incomplete.completeForEditor());

    const ats::ui::UiPointerEvent event { 11, "synth.freq", ats::ui::UiPointerPhase::move,
                                          { 12.0f, 3.0f }, { 1.0f, -2.0f }, true };
    assert(event.sequence == 11 && event.targetId == "synth.freq");
    assert(event.targetLocalDelta.y == -2.0f);
    return 0;
}

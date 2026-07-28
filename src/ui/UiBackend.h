#pragma once

#include "EditorHost.h"

#include <cstdint>
#include <string_view>

namespace ats::ui
{

struct UiPoint { float x = 0.0f; float y = 0.0f; };
struct UiRect { float x = 0.0f; float y = 0.0f; float width = 0.0f; float height = 0.0f; };

struct UiScale
{
    float logicalToDevice = 1.0f;
    [[nodiscard]] UiPoint toDevice(UiPoint point) const noexcept
    { return { point.x * logicalToDevice, point.y * logicalToDevice }; }
    [[nodiscard]] UiPoint toLogical(UiPoint point) const noexcept
    { return logicalToDevice <= 0.0f ? UiPoint {} : UiPoint { point.x / logicalToDevice, point.y / logicalToDevice }; }
};

// A concrete backend must prove every capability before UI rows can be
// promoted from planned to verified.
struct UiBackendCapabilities
{
    bool targetLocalPosition = false;
    bool targetLocalDelta = false;
    bool pointerCapture = false;
    bool ancestorObservation = false;
    bool zOrder = false;
    bool popupRoot = false;
    bool modalFocus = false;
    bool logicalDevicePixels = false;
    bool dpiScale = false;
    bool embeddedParent = false;
    bool standaloneWindow = false;
    bool nativeDialogs = false;
    bool nativeTextEntry = false;
    bool resize = false;
    bool teardownCancellation = false;

    [[nodiscard]] bool completeForEditor() const noexcept
    {
        return targetLocalPosition && targetLocalDelta && pointerCapture && ancestorObservation && zOrder &&
               popupRoot && modalFocus && logicalDevicePixels && dpiScale && embeddedParent &&
               standaloneWindow && nativeDialogs && nativeTextEntry && resize && teardownCancellation;
    }
};

enum class UiPointerPhase : std::uint8_t { down, move, up, cancel };
struct UiPointerEvent
{
    std::uint64_t sequence = 0;
    std::string_view targetId;
    UiPointerPhase phase = UiPointerPhase::cancel;
    UiPoint targetLocalPosition{};
    UiPoint targetLocalDelta{};
    bool primaryButton = false;
};
struct UiDispatchResult
{
    bool delivered = false;
    bool captured = false;
    bool dismissedPopup = false;
    std::uint64_t sequence = 0;
};

class UiBackend
{
public:
    virtual ~UiBackend() = default;
    [[nodiscard]] virtual UiBackendCapabilities capabilities() const noexcept = 0;
    virtual bool attach(EditorHost& host, void* nativeParent) = 0;
    virtual void detach() noexcept = 0;
    [[nodiscard]] virtual UiScale scale() const noexcept = 0;
    [[nodiscard]] virtual UiRect logicalBounds() const noexcept = 0;
    virtual UiDispatchResult dispatchPointer(const UiPointerEvent& event) = 0;
};

} // namespace ats::ui

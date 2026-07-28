#pragma once

#include <cstdint>

namespace ats::ui
{

// Native editor lifecycle callbacks are intentionally backend-neutral. They
// let a host or a probe observe real input/focus delivery without coupling the
// editor contract to Visage, JUCE, or a plugin SDK.
struct EditorEventSink
{
    void* context = nullptr;
    void (*mouseDown)(void* context) = nullptr;
    void (*textInput)(void* context, const char* text) = nullptr;
    void (*focusChanged)(void* context, bool focused) = nullptr;
};

class IEditor
{
public:
    virtual ~IEditor() = default;

    virtual void getSize(std::uint32_t& width, std::uint32_t& height) const = 0;
    virtual bool setSize(std::uint32_t width, std::uint32_t height) = 0;
    virtual bool canResize() const = 0;
    virtual void setScale(double scale) = 0;
    virtual bool setParent(void* nativeParent) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool isShowing() const = 0;
    virtual void* nativeHandle() const = 0;
};

} // namespace ats::ui

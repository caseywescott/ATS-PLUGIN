#pragma once

#include "EditorHost.h"

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
    void (*mouseUp)(void* context) = nullptr;
    void (*textInput)(void* context, const char* text) = nullptr;
    void (*focusChanged)(void* context, bool focused) = nullptr;
    void (*popupShown)(void* context) = nullptr;
    void (*popupSelected)(void* context, int optionId) = nullptr;
    void (*popupDismissed)(void* context) = nullptr;
    void (*pageSelected)(void* context, int pageIndex) = nullptr;
    void (*parameterGesture)(void* context, const char* id, float value,
                             bool begin, bool end) = nullptr;
    void (*historyAction)(void* context, bool redo) = nullptr;
    void (*themeSelected)(void* context, int themeIndex) = nullptr;
    void (*spectrumModeSelected)(void* context, int sourceIndex, int mode) = nullptr;
    void (*dialogRequested)(void* context, int kind) = nullptr;
    void (*dialogResolved)(void* context, int kind, const char* path, bool accepted) = nullptr;
    // Typed actions are delivered synchronously at the backend boundary. The
    // pointed-to variant is only valid for the duration of the callback;
    // consumers that queue work must copy it into their dispatcher.
    void (*action)(void* context, const EditorAction* action) = nullptr;
    // Native file-drop completion is reported independently from typed actions
    // so hosts can expose unsupported/multi-file rejection without guessing
    // from an absent import action.
    void (*fileDrop)(void* context, int slot, const char* path, bool accepted) = nullptr;
    void (*dialogError)(void* context, int kind, const char* code, const char* text) = nullptr;
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
    virtual bool showStandalone() = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool isShowing() const = 0;
    virtual void* nativeHandle() const = 0;
};

} // namespace ats::ui

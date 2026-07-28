#pragma once

#include "EditorHost.h"
#include "PopupManager.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace ats::ui
{

enum class DialogKind : std::uint8_t { importWav, importAts, exportAts, savePreset, loadPreset, bounceTimeline };

struct DialogRequest
{
    DialogKind kind = DialogKind::importWav;
    std::uint64_t generation = 0;
    int slot = 0;
    std::string ownerId;
    std::string suggestedName;
};

class DialogModel
{
public:
    bool open(DialogRequest request)
    {
        if (active_)
            return false;
        request.generation = ++generation_;
        request_ = std::move(request);
        active_ = true;
        return true;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const DialogRequest& request() const noexcept { return request_; }

    [[nodiscard]] std::optional<EditorAction> accept(ats::Path path)
    {
        if (!active_ || path.empty())
            return std::nullopt;
        const auto request = request_;
        close();
        switch (request.kind)
        {
            case DialogKind::importWav: return EditorAction { ImportWavAction { std::move(path), request.slot } };
            case DialogKind::importAts: return EditorAction { ImportAtsAction { std::move(path), request.slot } };
            case DialogKind::exportAts: return EditorAction { ExportAtsAction { std::move(path), request.slot } };
            case DialogKind::savePreset: return EditorAction { SavePresetAction { std::move(path) } };
            case DialogKind::loadPreset: return EditorAction { LoadPresetAction { std::move(path) } };
            case DialogKind::bounceTimeline: return EditorAction { BounceMorphTimelineAction { std::move(path), request.slot } };
        }
        return std::nullopt;
    }

    bool cancel() noexcept
    {
        if (!active_)
            return false;
        close();
        return true;
    }

    void close() noexcept
    {
        active_ = false;
        request_ = {};
    }

private:
    std::uint64_t generation_ = 0;
    bool active_ = false;
    DialogRequest request_{};
};

class EditorLifecycleModel
{
public:
    bool attach() noexcept
    {
        if (lifecycle_ != EditorLifecycle::detached)
            return false;
        lifecycle_ = EditorLifecycle::attached;
        return true;
    }

    bool openDialog(DialogRequest request)
    {
        return lifecycle_ == EditorLifecycle::attached && dialog_.open(std::move(request));
    }

    [[nodiscard]] std::optional<EditorAction> acceptDialog(ats::Path path)
    {
        if (lifecycle_ != EditorLifecycle::attached)
            return std::nullopt;
        return dialog_.accept(std::move(path));
    }

    bool cancelDialog() noexcept { return dialog_.cancel(); }
    [[nodiscard]] const DialogModel& dialog() const noexcept { return dialog_; }

    std::uint64_t openPopup(PopupRequest request, std::string restoreFocusId = {})
    {
        if (lifecycle_ != EditorLifecycle::attached)
            return 0;
        focus_ = FocusOwner::popup;
        return popup_.open(std::move(request), std::move(restoreFocusId));
    }

    void setPointerCaptured(bool captured) noexcept { pointerCaptured_ = captured; }
    [[nodiscard]] bool pointerCaptured() const noexcept { return pointerCaptured_; }
    [[nodiscard]] FocusOwner focus() const noexcept { return focus_; }
    [[nodiscard]] EditorLifecycle lifecycle() const noexcept { return lifecycle_; }

    void beginClose() noexcept
    {
        if (lifecycle_ != EditorLifecycle::attached)
            return;
        lifecycle_ = EditorLifecycle::closing;
        dialog_.close();
        popup_.dismissAll();
        pointerCaptured_ = false;
        focus_ = FocusOwner::none;
    }

    void finishClose() noexcept
    {
        if (lifecycle_ == EditorLifecycle::closing)
            lifecycle_ = EditorLifecycle::detached;
    }

private:
    EditorLifecycle lifecycle_ = EditorLifecycle::detached;
    DialogModel dialog_{};
    PopupManager popup_{};
    FocusOwner focus_ = FocusOwner::none;
    bool pointerCaptured_ = false;
};

} // namespace ats::ui

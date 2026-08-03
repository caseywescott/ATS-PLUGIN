#pragma once

#include "EditorHost.h"
#include "EditorActionProtocol.h"
#include "PopupManager.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace ats::ui
{

enum class DialogKind : std::uint8_t { importWav, importAts, exportAts, savePreset, loadPreset, bounceTimeline };
enum class DialogOutcome : std::uint8_t { idle, pending, accepted, cancelled, invalid, closed };

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
        outcome_ = DialogOutcome::pending;
        error_ = {};
        return true;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const DialogRequest& request() const noexcept { return request_; }
    [[nodiscard]] DialogOutcome outcome() const noexcept { return outcome_; }
    [[nodiscard]] const Error& error() const noexcept { return error_; }

    [[nodiscard]] std::optional<EditorAction> accept(ats::Path path)
    {
        if (!active_)
        {
            outcome_ = DialogOutcome::invalid;
            error_ = { "dialog_not_active", "no native dialog is awaiting a result" };
            return std::nullopt;
        }
        if (path.empty())
        {
            outcome_ = DialogOutcome::invalid;
            error_ = { "empty_dialog_path", "accepted dialogs must provide a path" };
            return std::nullopt;
        }
        const auto request = request_;
        std::optional<EditorAction> action;
        switch (request.kind)
        {
            case DialogKind::importWav: action = EditorAction { ImportWavAction { std::move(path), request.slot } }; break;
            case DialogKind::importAts: action = EditorAction { ImportAtsAction { std::move(path), request.slot } }; break;
            case DialogKind::exportAts: action = EditorAction { ExportAtsAction { std::move(path), request.slot } }; break;
            case DialogKind::savePreset: action = EditorAction { SavePresetAction { std::move(path) } }; break;
            case DialogKind::loadPreset: action = EditorAction { LoadPresetAction { std::move(path) } }; break;
            case DialogKind::bounceTimeline: action = EditorAction { BounceMorphTimelineAction { std::move(path), request.slot } }; break;
        }
        if (!action)
        {
            outcome_ = DialogOutcome::invalid;
            error_ = { "unknown_dialog_kind", "dialog kind cannot produce an action" };
            return std::nullopt;
        }
        const auto validation = validateAction(*action);
        if (!validation.valid)
        {
            outcome_ = DialogOutcome::invalid;
            error_ = { std::string(validation.code), std::string(validation.text) };
            return std::nullopt;
        }
        finish(DialogOutcome::accepted);
        return action;
    }

    bool cancel() noexcept
    {
        if (!active_)
            return false;
        finish(DialogOutcome::cancelled);
        return true;
    }

    void close() noexcept
    {
        if (active_)
            outcome_ = DialogOutcome::closed;
        active_ = false;
        request_ = {};
    }

private:
    void finish(DialogOutcome outcome) noexcept
    {
        active_ = false;
        request_ = {};
        outcome_ = outcome;
        error_ = {};
    }

    std::uint64_t generation_ = 0;
    bool active_ = false;
    DialogRequest request_{};
    DialogOutcome outcome_ = DialogOutcome::idle;
    Error error_{};
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

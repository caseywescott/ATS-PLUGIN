#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace ats::ui
{

enum class PopupKind : std::uint8_t { source, preset, view, timeline, context };

struct PopupRequest
{
    PopupKind kind = PopupKind::context;
    std::string ownerId;
    bool dismissOnOutsideClick = true;
    bool modal = false;
};

struct PopupState
{
    std::uint64_t id = 0;
    PopupRequest request;
    std::string restoreFocusId;
};

class PopupManager
{
public:
    [[nodiscard]] std::uint64_t open(PopupRequest request, std::string restoreFocusId = {})
    {
        state_ = PopupState { ++nextId_, std::move(request), std::move(restoreFocusId) };
        return state_.id;
    }

    bool dismiss(std::uint64_t id) noexcept
    {
        if (state_.id == 0 || state_.id != id) return false;
        state_ = {};
        return true;
    }

    bool outsideClick() noexcept
    {
        if (state_.id == 0 || !state_.request.dismissOnOutsideClick) return false;
        state_ = {};
        return true;
    }

    void dismissAll() noexcept { state_ = {}; }
    [[nodiscard]] bool open() const noexcept { return state_.id != 0; }
    [[nodiscard]] const PopupState& state() const noexcept { return state_; }

private:
    std::uint64_t nextId_ = 0;
    PopupState state_ {};
};

} // namespace ats::ui

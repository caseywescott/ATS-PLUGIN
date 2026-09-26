#pragma once

#include "EditorHost.h"

#include <cstdint>
#include <optional>

namespace ats::ui
{

// Framework-free preset combo model. The backend owns the combo widget and
// entry list population; this class owns the action produced by each
// selection and is the single policy source for factory index validation.
class PresetInteractionModel
{
public:
    static constexpr std::int32_t kFactoryCount = 12;

    // Factory preset row selected: validates index 0..11, returns nullopt for
    // out-of-range. The action is dispatched to ApplyFactoryPresetAction
    // which the processor applies without producing an undo entry.
    [[nodiscard]] static std::optional<EditorAction> selectFactory(std::int32_t index) noexcept
    {
        if (index < 0 || index >= kFactoryCount)
            return std::nullopt;
        return EditorAction{ ApplyFactoryPresetAction{ index } };
    }

    // User preset row selected: non-empty path produces LoadPresetAction.
    [[nodiscard]] static std::optional<EditorAction> selectUserPreset(ats::Path path) noexcept
    {
        if (path.empty())
            return std::nullopt;
        return EditorAction{ LoadPresetAction{ std::move(path) } };
    }

    // Save-as dialog accepted: non-empty path produces SavePresetAction.
    [[nodiscard]] static std::optional<EditorAction> savePreset(ats::Path path) noexcept
    {
        if (path.empty())
            return std::nullopt;
        return EditorAction{ SavePresetAction{ std::move(path) } };
    }
};

} // namespace ats::ui

#pragma once

#include "UiBackend.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ats::ui
{

// The column layout engine from docs/ats-mearcstapa-ui-spec.md section 10.7,
// plus the body/column arithmetic of 10.1 and 10.4-10.6. Kept as pure functions
// over rectangles so a headless test can assert exact page geometry; the page
// frames only translate the resulting slots into setBounds calls.

enum class LayoutItemKind : std::uint8_t
{
    control,  // pairable, hidden by the overflow guard
    heading   // never pairs, never hidden
};

struct LayoutItem
{
    float height = 0.0f;
    float gapAbove = 0.0f;
    LayoutItemKind kind = LayoutItemKind::control;
    // Pairing is declared per item rather than inferred from adjacency: the
    // spec pairs specific parameters ("start+duration paired 42") and leaves
    // their equally adjacent neighbours on rows of their own.
    bool pairWithNext = false;
};

struct LayoutSlot
{
    UiRect bounds{};
    bool visible = true;
};

struct ThemeLayout
{
    static constexpr float kBodyMargin = 16.0f;
    static constexpr float kTopBarHeight = 62.0f;
    static constexpr float kPageStripHeight = 30.0f;
    static constexpr float kPageStripGap = 10.0f;
    static constexpr float kPageStripMaxWidth = 640.0f;
    static constexpr float kColumnGap = 16.0f;
    static constexpr float kPanelPad = 14.0f;
    static constexpr float kPairGutter = 10.0f;
    static constexpr float kMinCompactControlH = 38.0f;

    // Section 10.1. The keyboard band is subtracted by the caller because its
    // height depends on the collapsed/expanded state.
    [[nodiscard]] static constexpr UiRect body(float width, float height) noexcept
    {
        return { kBodyMargin, kTopBarHeight + kBodyMargin,
                 std::max(0.0f, width - 2.0f * kBodyMargin),
                 std::max(0.0f, height - kTopBarHeight - 2.0f * kBodyMargin) };
    }

    // Section 10.3. Centred in the body, capped at 640 wide.
    [[nodiscard]] static constexpr UiRect pageStrip(UiRect bodyRect) noexcept
    {
        const float w = std::min(kPageStripMaxWidth, bodyRect.width);
        return { bodyRect.x + (bodyRect.width - w) * 0.5f, bodyRect.y, w, kPageStripHeight };
    }

    [[nodiscard]] static constexpr UiRect pageArea(UiRect bodyRect) noexcept
    {
        const float trim = kPageStripHeight + kPageStripGap;
        return { bodyRect.x, bodyRect.y + trim, bodyRect.width,
                 std::max(0.0f, bodyRect.height - trim) };
    }

    // Sections 10.4-10.6. A width of 0 marks the flex column; the leftover after
    // the fixed columns and the 16px gaps is split evenly between the flex ones.
    [[nodiscard]] static std::vector<UiRect> columns(UiRect area,
                                                     const std::vector<float>& widths)
    {
        std::vector<UiRect> out;
        if (widths.empty())
            return out;
        const float gaps = kColumnGap * static_cast<float>(widths.size() - 1);
        float fixed = 0.0f;
        int flexCount = 0;
        for (const float w : widths)
        {
            if (w > 0.0f) fixed += w;
            else ++flexCount;
        }
        const float flexEach = flexCount > 0
            ? std::max(0.0f, (area.width - gaps - fixed) / static_cast<float>(flexCount))
            : 0.0f;

        float x = area.x;
        out.reserve(widths.size());
        for (const float w : widths)
        {
            const float colW = w > 0.0f ? w : flexEach;
            out.push_back({ x, area.y, colW, area.height });
            x += colW + kColumnGap;
        }
        return out;
    }

    // Section 10.4: "each inner reduced(14) + 14px top pad".
    [[nodiscard]] static constexpr UiRect panelContent(UiRect panel) noexcept
    {
        return { panel.x + kPanelPad, panel.y + 2.0f * kPanelPad,
                 std::max(0.0f, panel.width - 2.0f * kPanelPad),
                 std::max(0.0f, panel.height - 3.0f * kPanelPad) };
    }

    // Section 10.7. Walks top-to-bottom; a paired row holds both controls at
    // width/2 - 5 with a 10px gutter and takes the taller of the two heights.
    [[nodiscard]] static std::vector<LayoutSlot> column(const std::vector<LayoutItem>& items,
                                                        UiRect area)
    {
        std::vector<LayoutSlot> slots(items.size());
        const float bottom = area.y + area.height;
        const float halfWidth = std::max(0.0f, area.width * 0.5f - kPairGutter * 0.5f);
        float y = area.y;

        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            const bool paired = item.pairWithNext && i + 1 < items.size() &&
                                item.kind == LayoutItemKind::control &&
                                items[i + 1].kind == LayoutItemKind::control;
            const float rowHeight = paired ? std::max(item.height, items[i + 1].height)
                                           : item.height;
            y += item.gapAbove;

            // The guard measures what is left of the column, not the row's own
            // height, so a run of trailing controls drops out together instead
            // of the first short one being kept and a later tall one hidden.
            const float remaining = bottom - y;
            if (item.kind == LayoutItemKind::control && remaining < kMinCompactControlH)
            {
                slots[i].visible = false;
                if (paired) slots[i + 1].visible = false;
                if (paired) ++i;
                continue;
            }

            if (paired)
            {
                slots[i].bounds = { area.x, y, halfWidth, items[i].height };
                slots[i + 1].bounds = { area.x + halfWidth + kPairGutter, y, halfWidth,
                                        items[i + 1].height };
                ++i;
            }
            else
            {
                slots[i].bounds = { area.x, y, area.width, item.height };
            }
            y += rowHeight;
        }
        return slots;
    }

    [[nodiscard]] static float columnHeight(const std::vector<LayoutItem>& items) noexcept
    {
        float total = 0.0f;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            const bool paired = item.pairWithNext && i + 1 < items.size() &&
                                item.kind == LayoutItemKind::control &&
                                items[i + 1].kind == LayoutItemKind::control;
            total += item.gapAbove;
            total += paired ? std::max(item.height, items[i + 1].height) : item.height;
            if (paired) ++i;
        }
        return total;
    }

    // Section 10.4's right column places its output rows first, anchored to the
    // bottom of the column. The group still reads top-down within itself; only
    // the group as a whole is pinned, so the ordering rules are unchanged.
    [[nodiscard]] static std::vector<LayoutSlot> columnFromBottom(
        const std::vector<LayoutItem>& items, UiRect area)
    {
        const float used = std::min(columnHeight(items), area.height);
        const UiRect anchored { area.x, area.y + area.height - used, area.width, used };
        return column(items, anchored);
    }
};

} // namespace ats::ui

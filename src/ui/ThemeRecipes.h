#pragma once

#include "ThemeMetrics.h"
#include "ThemeTokens.h"
#include "UiBackend.h"

#include <cstdint>
#include <vector>

namespace ats::ui
{

// The neumorphic drawing recipes from docs/mearcstapa-ui-style-reference.md
// section 3, expressed as a list of primitives rather than immediate canvas
// calls. Parity is defined in terms of exact offsets, alphas and radii, and a
// list can be asserted on in a headless test; direct canvas calls cannot.
// SpectrumRenderer already uses this shape.

enum class ThemeShape : std::uint8_t
{
    roundedRect,          // filled
    roundedRectBorder,    // stroked at `thickness`
    roundedRectGradient,  // diagonal fill, colorFrom at top-left to color at bottom-right
    ellipse               // filled, inscribed in `bounds`
};

struct ThemeDrawOp
{
    ThemeShape shape = ThemeShape::roundedRect;
    UiRect bounds{};
    float radius = 0.0f;
    float thickness = 1.0f;
    UiColor color{};
    UiColor colorFrom{};
    bool clipped = false;
    UiRect clip{};
};

[[nodiscard]] constexpr UiRect translatedRect(UiRect r, float dx, float dy) noexcept
{
    return { r.x + dx, r.y + dy, r.width, r.height };
}

[[nodiscard]] constexpr UiRect expandedRect(UiRect r, float d) noexcept
{
    return { r.x - d, r.y - d, r.width + 2.0f * d, r.height + 2.0f * d };
}

[[nodiscard]] constexpr UiRect reducedRect(UiRect r, float d) noexcept
{
    return expandedRect(r, -d);
}

struct ThemeRecipes
{
    // Section 3.1. Ember stacks three shadow pairs behind a diagonally graded
    // face; Porcelain is a flat fill with a 1px border and no shadow stack.
    [[nodiscard]] static std::vector<ThemeDrawOp> raisedPanel(const ThemeTokens& t, UiRect r,
                                                              float radius)
    {
        std::vector<ThemeDrawOp> ops;
        if (!t.isDark())
        {
            ops.push_back({ ThemeShape::roundedRect, r, radius, 1.0f, t.panel });
            ops.push_back({ ThemeShape::roundedRectBorder, reducedRect(r, 0.5f), radius, 1.0f,
                            t.panelBorder });
            return ops;
        }
        ops.reserve(7);
        for (int i = 1; i <= 3; ++i)
        {
            const auto step = static_cast<float>(i);
            const float offset = 2.0f * step;
            ops.push_back({ ThemeShape::roundedRect,
                            expandedRect(translatedRect(r, offset, offset), step * 0.5f),
                            radius + step, 1.0f, t.shadowDark.withAlpha(0.22f / step) });
            ops.push_back({ ThemeShape::roundedRect,
                            expandedRect(translatedRect(r, -offset, -offset), step * 0.5f),
                            radius + step, 1.0f, t.shadowLight.withAlpha(0.16f / step) });
        }
        ops.push_back({ ThemeShape::roundedRectGradient, r, radius, 1.0f, t.panel.darker(0.06f),
                        t.panel.brighter(0.05f) });
        return ops;
    }

    // Section 3.2. The faint light lip along the bottom 40% is what sells the
    // sunken read in Ember; without it the well is just a darker rectangle.
    [[nodiscard]] static std::vector<ThemeDrawOp> well(const ThemeTokens& t, UiRect r, float radius)
    {
        std::vector<ThemeDrawOp> ops;
        ops.push_back({ ThemeShape::roundedRect, r, radius, 1.0f, t.well });
        if (!t.isDark())
        {
            ops.push_back({ ThemeShape::roundedRectBorder, r, radius, 1.0f, t.wellBorder });
            return ops;
        }
        ops.push_back({ ThemeShape::roundedRectBorder, reducedRect(r, 0.5f), radius, 1.0f,
                        t.shadowDark.withAlpha(0.85f) });
        ThemeDrawOp lip { ThemeShape::roundedRectBorder, expandedRect(r, 0.5f), radius, 1.0f,
                          t.shadowLight.withAlpha(0.4f) };
        lip.clipped = true;
        lip.clip = { r.x, r.y + r.height * 0.6f, r.width, r.height * 0.4f };
        ops.push_back(lip);
        return ops;
    }

    // Section 3.4. Radius is fixed at 8 and the rect is inset by half a pixel so
    // the 1px Porcelain stroke lands on the pixel grid.
    static constexpr float kButtonRadius = 8.0f;

    [[nodiscard]] static std::vector<ThemeDrawOp> buttonBackground(const ThemeTokens& t, UiRect r,
                                                                   bool down, bool hovered)
    {
        const auto body = reducedRect(r, 0.5f);
        if (down)
        {
            auto ops = well(t, body, kButtonRadius);
            ops.push_back({ ThemeShape::roundedRect, body, kButtonRadius, 1.0f,
                            t.accent.withAlpha(0.15f) });
            return ops;
        }
        std::vector<ThemeDrawOp> ops;
        if (!t.isDark())
        {
            ops.push_back({ ThemeShape::roundedRect, body, kButtonRadius, 1.0f,
                            hovered ? t.window : t.panel });
            ops.push_back({ ThemeShape::roundedRectBorder, body, kButtonRadius, 1.0f,
                            hovered ? t.accent.withAlpha(0.6f) : t.panelBorder });
            return ops;
        }
        ops.push_back({ ThemeShape::roundedRect, translatedRect(body, 1.5f, 1.5f), kButtonRadius,
                        1.0f, t.shadowDark.withAlpha(0.4f) });
        ops.push_back({ ThemeShape::roundedRect, translatedRect(body, -1.5f, -1.5f), kButtonRadius,
                        1.0f, t.shadowLight.withAlpha(0.35f) });
        ops.push_back({ ThemeShape::roundedRect, body, kButtonRadius, 1.0f,
                        hovered ? t.window.brighter(0.1f) : t.window.brighter(0.05f) });
        return ops;
    }

    // Section 7.1. The thumb is a ring, not a dot: an inner disc reduced by 4.5
    // is punched out of the accent disc in the surface colour behind it.
    [[nodiscard]] static std::vector<ThemeDrawOp> sliderThumb(const ThemeTokens& t, float cx,
                                                              float cy, float radius)
    {
        const UiRect disc { cx - radius, cy - radius, radius * 2.0f, radius * 2.0f };
        std::vector<ThemeDrawOp> ops;
        if (t.isDark())
            ops.push_back({ ThemeShape::ellipse, translatedRect(disc, 1.5f, 1.5f), 0.0f, 1.0f,
                            t.shadowDark.withAlpha(0.6f) });
        ops.push_back({ ThemeShape::ellipse, disc, 0.0f, 1.0f, t.accent });
        ops.push_back({ ThemeShape::ellipse, reducedRect(disc, 4.5f), 0.0f, 1.0f,
                        t.isDark() ? t.window : t.panel });
        return ops;
    }

    // Section 7.3. A capsule well with a disc that slides end to end; the thumb
    // sits `radius` in from whichever end so its 3px margin is symmetric.
    [[nodiscard]] static std::vector<ThemeDrawOp> togglePill(const ThemeTokens& t, UiRect r,
                                                             bool on, bool hovered)
    {
        const float radius = ThemeRadius::togglePill(r.height);
        auto ops = well(t, r, radius);
        if (on)
            ops.push_back({ ThemeShape::roundedRect, r, radius, 1.0f,
                            t.accent.withAlpha(t.isDark() ? 0.3f : 0.18f) });

        const float thumbRadius = radius - 3.0f;
        const float cy = r.y + r.height * 0.5f;
        const float cx = on ? r.x + r.width - radius : r.x + radius;
        const UiRect thumb { cx - thumbRadius, cy - thumbRadius, thumbRadius * 2.0f,
                             thumbRadius * 2.0f };
        if (on && t.isDark())
            ops.push_back({ ThemeShape::ellipse, expandedRect(thumb, 4.0f), 0.0f, 1.0f,
                            t.accent.withAlpha(0.28f) });
        ops.push_back({ ThemeShape::ellipse, thumb, 0.0f, 1.0f,
                        on ? t.accent : (hovered ? t.textSecondary : t.textMuted) });
        return ops;
    }

    // Section 7.4. Only the active segment gets a body; inactive segments show
    // the sunken track through, which is what makes the active one read as lifted.
    [[nodiscard]] static std::vector<ThemeDrawOp> segmentedSegment(const ThemeTokens& t, UiRect seg,
                                                                   float trackRadius, bool active,
                                                                   bool hovered)
    {
        const auto pill = reducedRect(seg, 3.0f);
        const float radius = ThemeRadius::segmentedActivePill(trackRadius);
        if (active)
        {
            auto ops = raisedPanel(t, pill, radius);
            ops.push_back({ ThemeShape::roundedRect, pill, radius, 1.0f,
                            t.accent.withAlpha(t.isDark() ? 0.16f : 0.1f) });
            ops.push_back({ ThemeShape::roundedRectBorder, pill, radius, 1.2f,
                            t.accent.withAlpha(0.8f) });
            return ops;
        }
        std::vector<ThemeDrawOp> ops;
        if (hovered)
            ops.push_back({ ThemeShape::roundedRect, pill, radius, 1.0f,
                            t.accent.withAlpha(0.08f) });
        return ops;
    }

    [[nodiscard]] static constexpr UiColor segmentedText(const ThemeTokens& t, bool active,
                                                         bool hovered) noexcept
    {
        return active ? t.accent : (hovered ? t.textPrimary : t.textSecondary);
    }

    // Section 3.5. The tooltip window itself is transparent, so this card is the
    // whole of the tooltip and has to carry its own border.
    [[nodiscard]] static std::vector<ThemeDrawOp> tooltip(const ThemeTokens& t, UiRect r)
    {
        const auto card = reducedRect(r, 1.0f);
        std::vector<ThemeDrawOp> ops;
        ops.push_back({ ThemeShape::roundedRect, card, ThemeRadius::control, 1.0f,
                        t.isDark() ? t.window.brighter(0.06f) : t.panel });
        ops.push_back({ ThemeShape::roundedRectBorder, card, ThemeRadius::control, 1.0f,
                        t.panelBorder });
        return ops;
    }

    // Section 7.8. Two stacked 1px lines between 12px pads: a dark line with a
    // faint light lower edge, which is what carves the separator rather than
    // boxing the sub-section in a border.
    static constexpr float kDividerHeight = 27.0f;

    [[nodiscard]] static std::vector<ThemeDrawOp> engravedDivider(const ThemeTokens& t, UiRect r,
                                                                  float top)
    {
        std::vector<ThemeDrawOp> ops;
        ops.push_back({ ThemeShape::roundedRect, { r.x, r.y + top, r.width, 1.0f }, 0.0f, 1.0f,
                        t.isDark() ? t.shadowDark.withAlpha(0.85f) : t.wellBorder });
        ops.push_back({ ThemeShape::roundedRect, { r.x, r.y + top + 1.0f, r.width, 1.0f }, 0.0f,
                        1.0f,
                        t.isDark() ? t.shadowLight.withAlpha(0.6f)
                                   : UiColor { 0xffffffff }.withAlpha(0.6f) });
        return ops;
    }

    // Section 7.7. Constants are the reference's, so the block height a caller
    // reserves in a column matches where the label and rule actually land.
    static constexpr float kSectionLabelTopPad = 12.0f;
    static constexpr float kSectionLabelH = 15.0f;
    static constexpr float kSectionLabelBottomPad = 2.0f;
    static constexpr float kDividerLineH = 2.0f;

    // Section 7.4. The strip a per-control caption occupies above its widget.
    static constexpr float kControlLabelH = 14.0f;

    [[nodiscard]] static constexpr float sectionHeadingHeight(bool withTopDivider) noexcept
    {
        const float base = kSectionLabelTopPad + kSectionLabelH + kSectionLabelBottomPad;
        return withTopDivider ? base + kSectionLabelTopPad + kDividerLineH : base;
    }

    [[nodiscard]] static constexpr float sectionLabelTop(bool withTopDivider) noexcept
    {
        return withTopDivider ? 2.0f * kSectionLabelTopPad + kDividerLineH : kSectionLabelTopPad;
    }

    // The trailing hairline starts 8px past the text and fills the row.
    [[nodiscard]] static std::vector<ThemeDrawOp> sectionRule(const ThemeTokens& t, UiRect r,
                                                              float textWidth)
    {
        std::vector<ThemeDrawOp> ops;
        const float x = r.x + textWidth + 8.0f;
        if (x >= r.x + r.width)
            return ops;
        ops.push_back({ ThemeShape::roundedRect,
                        { x, r.y + r.height * 0.5f, r.x + r.width - x, 1.0f }, 0.0f, 1.0f,
                        t.textFaint.withAlpha(0.4f) });
        return ops;
    }

    // Section 3.3. There is no glow primitive; "lit" is the accent shape with a
    // wider low-alpha copy stamped behind it, and only in Ember.
    [[nodiscard]] static std::vector<ThemeDrawOp> accentHalo(const ThemeTokens& t, UiRect r,
                                                             float radius, float alpha,
                                                             float expandX, float expandY)
    {
        std::vector<ThemeDrawOp> ops;
        if (!t.isDark())
            return ops;
        const UiRect wider { r.x - expandX, r.y - expandY, r.width + 2.0f * expandX,
                             r.height + 2.0f * expandY };
        ops.push_back({ ThemeShape::roundedRect, wider, radius, 1.0f, t.accent.withAlpha(alpha) });
        return ops;
    }
};

} // namespace ats::ui

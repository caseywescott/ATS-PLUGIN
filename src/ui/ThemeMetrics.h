#pragma once

#include <algorithm>

namespace ats::ui
{

// Corner-radius scale from docs/mearcstapa-ui-style-reference.md section 4.
// The Visage widgets previously drew everything at radius 4, which is not a
// value the scale contains at all.
struct ThemeRadius
{
    static constexpr float bodyColumnPanel = 14.0f;
    static constexpr float spectrumWell = 10.0f;
    static constexpr float selectedPanel = 9.0f;   // XY-pad selected cell, selected list panel
    static constexpr float control = 8.0f;         // combo, text button, list row, generic panel
    static constexpr float cheekButton = 5.0f;
    static constexpr float meterTrack = 4.0f;
    static constexpr float meterFill = 3.0f;
    static constexpr float sliderTrack = 3.0f;

    // Capsules are derived from the control's height rather than fixed.
    [[nodiscard]] static constexpr float togglePill(float height) noexcept
    {
        return height * 0.5f;
    }

    [[nodiscard]] static constexpr float segmentedTrack(float height) noexcept
    {
        return std::min(height * 0.5f, 12.0f);
    }

    [[nodiscard]] static constexpr float segmentedActivePill(float trackRadius) noexcept
    {
        return std::max(2.0f, trackRadius - 3.0f);
    }
};

// Typography from section 6. Sizes are in points; kerning is JUCE's extra-kerning
// factor (a multiple of the font height), not a pixel tracking value.
struct ThemeFont
{
    static constexpr float title = 17.0f;
    static constexpr float titleKerning = 0.18f;
    static constexpr float subtitle = 9.0f;
    static constexpr float subtitleKerning = 0.14f;
    static constexpr float sectionLabel = 10.0f;
    static constexpr float sectionLabelKerning = 0.2f;

    static constexpr float listComboPopup = 13.0f;
    static constexpr float controlLabel = 12.0f;
    static constexpr float button = 12.5f;
    static constexpr float cheekButton = 11.0f;
    static constexpr float chip = 8.5f;

    static constexpr float monoValue = 11.5f;
    static constexpr float monoAxis = 8.5f;

    // Segments get smaller type once the row has to hold more than three.
    [[nodiscard]] static constexpr float segment(int segmentCount) noexcept
    {
        return segmentCount > 3 ? 10.0f : 12.5f;
    }
};

} // namespace ats::ui

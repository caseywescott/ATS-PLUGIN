#pragma once

#include <cstdint>

namespace ats::ui
{

enum class ThemeId : std::uint8_t { ember, porcelain };

struct UiColor
{
    std::uint32_t argb = 0xff000000u;

    [[nodiscard]] constexpr std::uint8_t alpha() const noexcept { return (argb >> 24) & 0xffu; }
    [[nodiscard]] constexpr std::uint8_t red() const noexcept { return (argb >> 16) & 0xffu; }
    [[nodiscard]] constexpr std::uint8_t green() const noexcept { return (argb >> 8) & 0xffu; }
    [[nodiscard]] constexpr std::uint8_t blue() const noexcept { return argb & 0xffu; }

    [[nodiscard]] constexpr UiColor withAlpha(float a) const noexcept
    {
        const auto scaled = static_cast<std::uint32_t>(a * 255.0f + 0.5f);
        const auto clamped = scaled > 255u ? 255u : scaled;
        return { (clamped << 24) | (argb & 0x00ffffffu) };
    }

    // JUCE Colour::brighter/darker, reproduced exactly: the drawing recipes in
    // docs/mearcstapa-ui-style-reference.md are stated as offsets from a base
    // token (panel.brighter(0.05), window.brighter(0.1), ...), so an approximation
    // here would put every surface slightly off-spec.
    [[nodiscard]] constexpr UiColor brighter(float amount) const noexcept
    {
        const float k = 1.0f / (1.0f + amount);
        return fromChannels(alpha(),
                            static_cast<std::uint8_t>(255.0f - k * (255.0f - red())),
                            static_cast<std::uint8_t>(255.0f - k * (255.0f - green())),
                            static_cast<std::uint8_t>(255.0f - k * (255.0f - blue())));
    }

    [[nodiscard]] constexpr UiColor darker(float amount) const noexcept
    {
        const float k = 1.0f / (1.0f + amount);
        return fromChannels(alpha(),
                            static_cast<std::uint8_t>(k * red()),
                            static_cast<std::uint8_t>(k * green()),
                            static_cast<std::uint8_t>(k * blue()));
    }

    friend constexpr bool operator==(UiColor a, UiColor b) noexcept { return a.argb == b.argb; }
    friend constexpr bool operator!=(UiColor a, UiColor b) noexcept { return !(a == b); }

private:
    [[nodiscard]] static constexpr UiColor fromChannels(std::uint8_t a, std::uint8_t r,
                                                        std::uint8_t g, std::uint8_t b) noexcept
    {
        return { (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) |
                 (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b) };
    }
};

// Palette from docs/mearcstapa-ui-style-reference.md section 2. Field names match
// the token names in that table so the two can be diffed by eye. The keyFace/keyEdge
// tokens are omitted: they exist only for MEARCSTAPA's keybed, which ATS has no
// counterpart to.
struct ThemeTokens
{
    UiColor window;
    UiColor panel;
    UiColor panelBorder;
    UiColor well;
    UiColor wellBorder;
    UiColor textPrimary;
    UiColor textSecondary;
    UiColor textMuted;
    UiColor textFaint;
    UiColor accent;
    UiColor accentDim;
    UiColor shadowDark;
    UiColor shadowLight;

    [[nodiscard]] static constexpr ThemeTokens forTheme(ThemeId theme) noexcept
    {
        if (theme == ThemeId::porcelain)
            return { { 0xfff2f1eeu }, { 0xffffffffu }, { 0xffd8d6d1u }, { 0xffeceae5u },
                     { 0xffd8d6d1u }, { 0xff23231fu }, { 0xff6e6c66u }, { 0xff9c9a93u },
                     { 0xffb5b3acu }, { 0xff1d6e63u }, { 0xff6fa39bu },
                     // Porcelain draws depth as a flat fill plus a 1px border, so the
                     // neumorphic shadow stack is skipped entirely rather than tinted.
                     { 0x00000000u }, { 0x00000000u } };
        return { { 0xff22242au }, { 0xff22242au }, { 0xff2f343du }, { 0xff1a1c21u },
                 { 0xff15161au }, { 0xffe8e9ecu }, { 0xff9aa0acu }, { 0xff7a7f8au },
                 { 0xff565c68u }, { 0xffe8b96au }, { 0xff8a6b3du },
                 { 0xff15161au }, { 0xff2f343du } };
    }

    [[nodiscard]] constexpr bool isDark() const noexcept { return shadowDark.alpha() != 0; }
};

// The spectrum canvas deliberately does not follow the surrounding Ember /
// Porcelain theme. Keeping this palette fixed preserves the visual hierarchy
// of the spectral data across editor themes.
struct SpectralCanvasTokens
{
    UiColor background       { 0xff0a1220u };
    UiColor bgVignetteEdge   { 0xff050a12u };
    UiColor gridMinor        { 0xff0e2a33u };
    UiColor gridMajor        { 0xff14454fu };
    UiColor axisFrame        { 0xff1e6e6eu };
    UiColor selection        { 0xffffe28au };
    UiColor hover            { 0xffeafffbu };
    UiColor playhead         { 0xff7affd4u };
    UiColor residualLow      { 0xff123b4au };
    UiColor residualHigh     { 0xff1e7a6au };

    [[nodiscard]] static constexpr SpectralCanvasTokens fixed() noexcept { return {}; }
};

} // namespace ats::ui

#pragma once

#include <cstdint>

namespace ats::ui
{

enum class ThemeId : std::uint8_t { ember, porcelain };

struct UiColor
{
    std::uint32_t argb = 0xff000000u;
    friend bool operator==(UiColor a, UiColor b) noexcept { return a.argb == b.argb; }
    friend bool operator!=(UiColor a, UiColor b) noexcept { return !(a == b); }
};

struct ThemeTokens
{
    UiColor background;
    UiColor panel;
    UiColor surface;
    UiColor text;
    UiColor secondaryText;
    UiColor accent;
    UiColor warning;
    UiColor error;
    UiColor spectrumCanvas;

    [[nodiscard]] static constexpr ThemeTokens forTheme(ThemeId theme) noexcept
    {
        if (theme == ThemeId::porcelain)
            return { { 0xffeee9dfu }, { 0xffddd6c9u }, { 0xffcfc7b9u }, { 0xff1b211fu },
                     { 0xff52605au }, { 0xffb85c3au }, { 0xffc58a22u }, { 0xffb83f3fu },
                     { 0xff101513u } };
        return { { 0xff111512u }, { 0xff1a1f1cu }, { 0xff252c28u }, { 0xffe6eee9u },
                 { 0xff9eada4u }, { 0xff5bffbbu }, { 0xffffc857u }, { 0xffff6b6bu },
                 { 0xff101513u } };
    }
};

} // namespace ats::ui

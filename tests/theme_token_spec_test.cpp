// Pins the palette to the table in docs/mearcstapa-ui-style-reference.md section 2.
// That document, not the JUCE build's rendering, is the parity target, so every
// value here is transcribed from it and any drift should fail loudly.

#include "../src/ui/ThemeTokens.h"

#include <cassert>

using ats::ui::ThemeId;
using ats::ui::ThemeTokens;
using ats::ui::UiColor;

static void checkEmberPalette()
{
    constexpr auto t = ThemeTokens::forTheme(ThemeId::ember);
    static_assert(t.window.argb == 0xff22242au);
    static_assert(t.panel.argb == 0xff22242au);
    static_assert(t.panelBorder.argb == 0xff2f343du);
    static_assert(t.well.argb == 0xff1a1c21u);
    static_assert(t.wellBorder.argb == 0xff15161au);
    static_assert(t.textPrimary.argb == 0xffe8e9ecu);
    static_assert(t.textSecondary.argb == 0xff9aa0acu);
    static_assert(t.textMuted.argb == 0xff7a7f8au);
    static_assert(t.textFaint.argb == 0xff565c68u);
    static_assert(t.accent.argb == 0xffe8b96au);
    static_assert(t.accentDim.argb == 0xff8a6b3du);
    static_assert(t.shadowDark.argb == 0xff15161au);
    static_assert(t.shadowLight.argb == 0xff2f343du);

    // "In Ember, window == panel == #22242A; raised panels read as raised purely
    // via the shadow stack, not a different base fill."
    static_assert(t.window == t.panel);
    static_assert(t.isDark());
}

static void checkPorcelainPalette()
{
    constexpr auto t = ThemeTokens::forTheme(ThemeId::porcelain);
    static_assert(t.window.argb == 0xfff2f1eeu);
    static_assert(t.panel.argb == 0xffffffffu);
    static_assert(t.panelBorder.argb == 0xffd8d6d1u);
    static_assert(t.well.argb == 0xffeceae5u);
    static_assert(t.wellBorder.argb == 0xffd8d6d1u);
    static_assert(t.textPrimary.argb == 0xff23231fu);
    static_assert(t.textSecondary.argb == 0xff6e6c66u);
    static_assert(t.textMuted.argb == 0xff9c9a93u);
    static_assert(t.textFaint.argb == 0xffb5b3acu);
    static_assert(t.accent.argb == 0xff1d6e63u);
    static_assert(t.accentDim.argb == 0xff6fa39bu);

    // "Porcelain shadows are transparent — the light theme does not use the
    // feathered shadow stack." isDark() keys the recipes off exactly this.
    static_assert(t.shadowDark.alpha() == 0);
    static_assert(t.shadowLight.alpha() == 0);
    static_assert(!t.isDark());

    // The accent is teal, not a tint of Ember's amber.
    static_assert(t.accent != ThemeTokens::forTheme(ThemeId::ember).accent);
}

// The recipes are specified as offsets from a base token, so brighter/darker must
// match JUCE's Colour arithmetic bit for bit or every derived surface lands off-spec.
static void checkJuceColourArithmetic()
{
    constexpr UiColor window { 0xff22242au };

    // JUCE: k = 1/(1+amount); channel = 255 - k*(255-channel), truncated.
    // window.brighter(0.05): k = 0.952380952
    //   r: 255 - 0.952381*(255-0x22=221) = 255 - 210.476 = 44.52 -> 44 = 0x2c
    //   g: 255 - 0.952381*(255-0x24=219) = 255 - 208.571 = 46.43 -> 46 = 0x2e
    //   b: 255 - 0.952381*(255-0x2a=213) = 255 - 202.857 = 52.14 -> 52 = 0x34
    static_assert(window.brighter(0.05f).argb == 0xff2c2e34u);

    // window.darker(0.06): k = 0.943396
    //   r: 0.943396*34 = 32.07 -> 32 = 0x20
    //   g: 0.943396*36 = 33.96 -> 33 = 0x21
    //   b: 0.943396*42 = 39.62 -> 39 = 0x27
    static_assert(window.darker(0.06f).argb == 0xff202127u);

    // Zero is the identity for both, and alpha is carried through untouched.
    static_assert(window.brighter(0.0f) == window);
    static_assert(window.darker(0.0f) == window);
    static_assert(window.withAlpha(0.25f).alpha() == 64);
    static_assert(window.withAlpha(0.25f).brighter(0.1f).alpha() == 64);
    static_assert(window.withAlpha(1.0f).argb == window.argb);
}

int main()
{
    checkEmberPalette();
    checkPorcelainPalette();
    checkJuceColourArithmetic();
    return 0;
}

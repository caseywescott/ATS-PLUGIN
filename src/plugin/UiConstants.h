#pragma once

#include <cstdint>

#include <JuceHeader.h>

namespace ats_editor
{
/** Documented soft cap for partials × frames (see BUILDING.md). */
constexpr int64_t kModelSizeSoftCap = 2'000'000;
} // namespace ats_editor

namespace ats_ui
{
// ---------------------------------------------------------------------------
// Theme palette (MEARCSTAPA UI spec §2). Two themes: 0 = Porcelain (light),
// 1 = Ember (dark). Every token exists for both themes. A single runtime
// `activePalette()` is read by the LookAndFeel and by panels at paint time, so
// switching themes is a global swap + repaint.
// ---------------------------------------------------------------------------
struct Palette
{
    bool dark = true;

    juce::Colour window, panel, panelBorder, well, wellBorder;
    juce::Colour textPrimary, textSecondary, textMuted, textFaint;
    juce::Colour accent, accentDim, shadowDark, shadowLight;
    juce::Colour keyFaceW, keyEdgeW, keyFaceB, keyEdgeB;
};

inline Palette emberPalette()
{
    Palette p;
    p.dark          = true;
    p.window        = juce::Colour(0xff22242A);
    p.panel         = juce::Colour(0xff22242A);
    p.panelBorder   = juce::Colour(0xff2F343D);
    p.well          = juce::Colour(0xff1A1C21);
    p.wellBorder    = juce::Colour(0xff15161A);
    p.textPrimary   = juce::Colour(0xffE8E9EC);
    p.textSecondary = juce::Colour(0xff9AA0AC);
    p.textMuted     = juce::Colour(0xff7A7F8A);
    p.textFaint     = juce::Colour(0xff565C68);
    p.accent        = juce::Colour(0xffE8B96A);
    p.accentDim     = juce::Colour(0xff8A6B3D);
    p.shadowDark    = juce::Colour(0xff15161A);
    p.shadowLight   = juce::Colour(0xff2F343D);
    p.keyFaceW      = juce::Colour(0xff3A3D46);
    p.keyEdgeW      = juce::Colour(0xff44474F);
    p.keyFaceB      = juce::Colour(0xff191A1F);
    p.keyEdgeB      = juce::Colour(0xff2B2D34);
    return p;
}

inline Palette porcelainPalette()
{
    Palette p;
    p.dark          = false;
    p.window        = juce::Colour(0xffF2F1EE);
    p.panel         = juce::Colour(0xffFFFFFF);
    p.panelBorder   = juce::Colour(0xffD8D6D1);
    p.well          = juce::Colour(0xffECEAE5);
    p.wellBorder    = juce::Colour(0xffD8D6D1);
    p.textPrimary   = juce::Colour(0xff23231F);
    p.textSecondary = juce::Colour(0xff6E6C66);
    p.textMuted     = juce::Colour(0xff9C9A93);
    p.textFaint     = juce::Colour(0xffB5B3AC);
    p.accent        = juce::Colour(0xff1D6E63);
    p.accentDim     = juce::Colour(0xff6FA39B);
    p.shadowDark    = juce::Colours::transparentBlack;
    p.shadowLight   = juce::Colours::transparentBlack;
    p.keyFaceW      = juce::Colour(0xffFAF9F6);
    p.keyEdgeW      = juce::Colour(0xffFFFFFF);
    p.keyFaceB      = juce::Colour(0xff33332F);
    p.keyEdgeB      = juce::Colour(0xff45453E);
    return p;
}

// Storage for the active palette + theme index. Header-only via a function
// local static (message-thread only, so no synchronisation needed).
inline Palette& mutableActivePalette()
{
    static Palette p = emberPalette();
    return p;
}

inline int& mutableActiveTheme()
{
    static int t = 1; // Ember default
    return t;
}

inline const Palette& activePalette() { return mutableActivePalette(); }
inline int activeTheme()              { return mutableActiveTheme(); }

/** 0 = Porcelain (light), 1 = Ember (dark). */
inline void setActiveTheme(int theme)
{
    mutableActiveTheme()   = theme;
    mutableActivePalette() = (theme == 1) ? emberPalette() : porcelainPalette();
}

namespace colour
{
// New spec tokens.
inline juce::Colour window()        { return activePalette().window; }
inline juce::Colour panel()         { return activePalette().panel; }
inline juce::Colour panelBorder()   { return activePalette().panelBorder; }
inline juce::Colour well()          { return activePalette().well; }
inline juce::Colour wellBorder()    { return activePalette().wellBorder; }
inline juce::Colour textPrimary()   { return activePalette().textPrimary; }
inline juce::Colour textSecondary() { return activePalette().textSecondary; }
inline juce::Colour textFaint()     { return activePalette().textFaint; }
inline juce::Colour accentDim()     { return activePalette().accentDim; }
inline juce::Colour shadowDark()    { return activePalette().shadowDark; }
inline juce::Colour shadowLight()   { return activePalette().shadowLight; }

// Legacy tokens (existing panels) redirected onto the active palette so they
// become theme-aware without touching every call site.
inline juce::Colour background()     { return activePalette().window; }
inline juce::Colour header()         { return activePalette().well; }
inline juce::Colour surface()        { return activePalette().well; }
inline juce::Colour surfaceRaised()  { return activePalette().panel; }
inline juce::Colour surfacePressed()
{
    const auto& p = activePalette();
    return p.dark ? p.well.brighter(0.10f) : p.well.darker(0.04f);
}
inline juce::Colour border()         { return activePalette().panelBorder; }
inline juce::Colour borderStrong()   { return activePalette().textFaint; }
inline juce::Colour text()           { return activePalette().textPrimary; }
inline juce::Colour textMuted()      { return activePalette().textMuted; }
inline juce::Colour accent()         { return activePalette().accent; }
inline juce::Colour accentSoft()     { return activePalette().accentDim; }
inline juce::Colour amber()          { return activePalette().accent; }
inline juce::Colour error()          { return juce::Colour(0xffC96A5A); }
inline juce::Colour warning()        { return activePalette().accent; }
} // namespace colour

// ---------------------------------------------------------------------------
// Typography (spec §6).
// ---------------------------------------------------------------------------
namespace font
{
inline juce::Font ui(float h)      { return juce::Font(h); }
inline juce::Font uiBold(float h)  { return juce::Font(h, juce::Font::bold); }
inline juce::Font mono(float h)    { return juce::Font(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain); }
inline juce::Font section()        { return juce::Font(10.0f).withExtraKerningFactor(0.2f); }
} // namespace font

// ---------------------------------------------------------------------------
// Spacing / layout constants (spec §5, §10.7).
// ---------------------------------------------------------------------------
constexpr int kMinCompactControlH = 38;
constexpr int kCompactRowH        = 42;
constexpr int kSectionTopPad      = 12;
constexpr int kSectionLabelH      = 15;
constexpr int kPairGutter         = 10;

/** Height of a section-heading block (spec §5). */
inline int sectionHeadingHeight(bool withTopDivider)
{
    return kSectionTopPad + (withTopDivider ? 3 : 0) + kSectionLabelH + 2;
}

/** Engraved 2px divider: one dark line + one light line (spec §5). */
inline void drawEngravedDivider(juce::Graphics& g, float x0, float x1, float y)
{
    const auto& p = activePalette();
    g.setColour(p.dark ? p.shadowDark.withAlpha(0.9f) : p.panelBorder);
    g.drawHorizontalLine((int) y, x0, x1);
    g.setColour(p.dark ? p.shadowLight.withAlpha(0.5f) : p.window);
    g.drawHorizontalLine((int) y + 1, x0, x1);
}

/** Uppercase section caption, colour textMuted (spec §5/§6). Optional top divider. */
inline void drawSectionHeading(juce::Graphics& g, juce::Rectangle<int> block,
                               const juce::String& text, bool withTopDivider, bool ruleMode = false)
{
    const auto& p = activePalette();
    auto r = block;
    r.removeFromTop(kSectionTopPad);
    if (withTopDivider)
    {
        drawEngravedDivider(g, (float) r.getX(), (float) r.getRight(), (float) r.getY());
        r.removeFromTop(3);
    }
    g.setColour(ruleMode ? p.textFaint : p.textMuted);
    g.setFont(font::section());
    g.drawText(text.toUpperCase(), r.removeFromTop(kSectionLabelH),
               juce::Justification::centredLeft, false);
}

// ---------------------------------------------------------------------------
// Neumorphic drawing recipes (spec §3). Ember draws the soft shadow stack;
// Porcelain draws a flat fill + 1px border and no shadows.
// ---------------------------------------------------------------------------
inline void drawRaisedPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    const auto& p = activePalette();
    if (p.dark)
    {
        for (int i = 1; i <= 3; ++i)
        {
            const float offset = 2.0f * (float) i;
            const float exp    = (float) i * 0.5f;
            const float rr     = radius + (float) i;

            g.setColour(p.shadowDark.withAlpha(0.22f / (float) i));
            g.fillRoundedRectangle(r.translated(offset, offset).expanded(exp), rr);
            g.setColour(p.shadowLight.withAlpha(0.16f / (float) i));
            g.fillRoundedRectangle(r.translated(-offset, -offset).expanded(exp), rr);
        }

        juce::ColourGradient grad(p.panel.brighter(0.05f), r.getTopLeft(),
                                  p.panel.darker(0.06f), r.getBottomRight(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, radius);
    }
    else
    {
        g.setColour(p.panel);
        g.fillRoundedRectangle(r, radius);
        g.setColour(p.panelBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
}

inline void drawWell(juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    const auto& p = activePalette();
    g.setColour(p.well);
    g.fillRoundedRectangle(r, radius);

    if (p.dark)
    {
        g.setColour(p.shadowDark.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);

        // Faint bottom light lip clipped to the lower 40% sells the sunken read.
        juce::Graphics::ScopedSaveState save(g);
        auto lip = r.withTop(r.getY() + r.getHeight() * 0.6f);
        g.reduceClipRegion(lip.toNearestInt());
        g.setColour(p.shadowLight.withAlpha(0.4f));
        g.drawRoundedRectangle(r.expanded(0.5f), radius, 1.0f);
    }
    else
    {
        g.setColour(p.wellBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
}

/** Tooltip card (spec §3.5). Caller supplies text; window bg is transparent. */
inline void drawTooltipCard(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text)
{
    const auto& p = activePalette();
    auto r = bounds.toFloat().reduced(1.0f);
    g.setColour(p.dark ? p.window.brighter(0.06f) : p.panel);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(p.panelBorder);
    g.drawRoundedRectangle(r, 8.0f, 1.0f);

    juce::AttributedString s;
    s.append(text, juce::Font(12.5f), p.textPrimary);
    s.setJustification(juce::Justification::centredLeft);
    s.draw(g, bounds.reduced(8, 4).toFloat());
}

inline void applyEditorPalette(juce::LookAndFeel_V4& lookAndFeel)
{
    const auto& p = activePalette();

    lookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, p.window);

    // Text ids -> textPrimary.
    lookAndFeel.setColour(juce::Label::textColourId, p.textPrimary);
    lookAndFeel.setColour(juce::Slider::textBoxTextColourId, p.textPrimary);
    lookAndFeel.setColour(juce::ComboBox::textColourId, p.textPrimary);
    lookAndFeel.setColour(juce::PopupMenu::textColourId, p.textPrimary);
    lookAndFeel.setColour(juce::ToggleButton::textColourId, p.textPrimary);
    lookAndFeel.setColour(juce::AlertWindow::textColourId, p.textPrimary);
    lookAndFeel.setColour(juce::AlertWindow::backgroundColourId, p.window);
    lookAndFeel.setColour(juce::AlertWindow::outlineColourId, p.panelBorder);
    lookAndFeel.setColour(juce::TextEditor::textColourId, p.textPrimary);

    lookAndFeel.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour(juce::TooltipWindow::backgroundColourId, juce::Colours::transparentBlack);

    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, p.dark ? p.well : p.panel);
    lookAndFeel.setColour(juce::ComboBox::outlineColourId, p.dark ? p.wellBorder : p.panelBorder);
    lookAndFeel.setColour(juce::ComboBox::arrowColourId, p.accent);

    lookAndFeel.setColour(juce::PopupMenu::backgroundColourId, p.dark ? p.window.brighter(0.04f) : p.panel);
    lookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId,
                          p.accent.withAlpha(p.dark ? 0.22f : 0.14f));
    lookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, p.textPrimary);

    lookAndFeel.setColour(juce::TextButton::buttonColourId, p.dark ? p.window.brighter(0.05f) : p.panel);
    lookAndFeel.setColour(juce::TextButton::buttonOnColourId, p.accent);
    lookAndFeel.setColour(juce::TextButton::textColourOffId, p.textPrimary);
    lookAndFeel.setColour(juce::TextButton::textColourOnId, p.dark ? p.window : p.panel);

    lookAndFeel.setColour(juce::TextEditor::backgroundColourId, p.dark ? p.well : p.window);
    lookAndFeel.setColour(juce::TextEditor::focusedOutlineColourId, p.accent);
    lookAndFeel.setColour(juce::TextEditor::highlightColourId, p.accent.withAlpha(0.3f));
    lookAndFeel.setColour(juce::CaretComponent::caretColourId, p.accent);

    lookAndFeel.setColour(juce::BubbleComponent::backgroundColourId, p.dark ? p.window.brighter(0.05f) : p.panel);

    // Sliders.
    lookAndFeel.setColour(juce::Slider::rotarySliderFillColourId, p.accent);
    lookAndFeel.setColour(juce::Slider::rotarySliderOutlineColourId, p.panelBorder);
    lookAndFeel.setColour(juce::Slider::trackColourId, p.accentDim);
    lookAndFeel.setColour(juce::Slider::thumbColourId, p.textPrimary);

    lookAndFeel.setColour(juce::ProgressBar::backgroundColourId, p.well);
    lookAndFeel.setColour(juce::ProgressBar::foregroundColourId, p.accent);
    lookAndFeel.setColour(juce::TabbedComponent::backgroundColourId, p.window);
    lookAndFeel.setColour(juce::TabbedComponent::outlineColourId, p.panelBorder);
}
} // namespace ats_ui

#pragma once

#include <memory>
#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mearcstapa_toggle_button_dropin
{
struct TogglePalette
{
    bool dark = true;
    juce::Colour window, panel, panelBorder, well, wellBorder;
    juce::Colour textPrimary, textSecondary, textMuted, textFaint;
    juce::Colour accent, accentDim;
    juce::Colour shadowDark, shadowLight;
};

inline TogglePalette emberPalette()
{
    return {
        true,
        juce::Colour(0xFF22242A), juce::Colour(0xFF22242A), juce::Colour(0xFF2F343D),
        juce::Colour(0xFF1A1C21), juce::Colour(0xFF15161A),
        juce::Colour(0xFFE8E9EC), juce::Colour(0xFF9AA0AC), juce::Colour(0xFF7A7F8A), juce::Colour(0xFF565C68),
        juce::Colour(0xFFE8B96A), juce::Colour(0xFF8A6B3D),
        juce::Colour(0xFF15161A), juce::Colour(0xFF2F343D)
    };
}

inline TogglePalette porcelainPalette()
{
    return {
        false,
        juce::Colour(0xFFF2F1EE), juce::Colour(0xFFFFFFFF), juce::Colour(0xFFD8D6D1),
        juce::Colour(0xFFECEAE5), juce::Colour(0xFFD8D6D1),
        juce::Colour(0xFF23231F), juce::Colour(0xFF6E6C66), juce::Colour(0xFF9C9A93), juce::Colour(0xFFB5B3AC),
        juce::Colour(0xFF1D6E63), juce::Colour(0xFF6FA39B),
        juce::Colours::transparentBlack, juce::Colours::transparentBlack
    };
}

inline juce::Font uiFont(float height)
{
    return juce::Font(juce::FontOptions(height));
}

inline void drawWell(juce::Graphics& g, juce::Rectangle<float> r, float radius, const TogglePalette& p)
{
    g.setColour(p.well);
    g.fillRoundedRectangle(r, radius);

    if (p.dark)
    {
        g.setColour(p.shadowDark.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);

        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(r.withTrimmedTop(r.getHeight() * 0.6f).toNearestInt());
        g.setColour(p.shadowLight.withAlpha(0.4f));
        g.drawRoundedRectangle(r.expanded(0.5f), radius + 0.5f, 1.0f);
    }
    else
    {
        g.setColour(p.wellBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    }
}

class ToggleLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit ToggleLookAndFeel(TogglePalette paletteIn = emberPalette())
        : palette(paletteIn)
    {
        applyColours();
    }

    void setPalette(TogglePalette paletteIn)
    {
        palette = paletteIn;
        applyColours();
    }

    const TogglePalette& getPalette() const noexcept { return palette; }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto on = button.getToggleState();
        auto area = button.getLocalBounds().toFloat();
        const auto pillW = juce::jmin(area.getWidth(), 42.0f);
        const auto pillH = juce::jmin(area.getHeight(), 21.0f);
        const auto pill = area.withSizeKeepingCentre(pillW, pillH);
        const auto radius = pillH * 0.5f;

        drawWell(g, pill, radius, palette);
        if (on)
        {
            g.setColour(palette.accent.withAlpha(palette.dark ? 0.3f : 0.18f));
            g.fillRoundedRectangle(pill, radius);
        }

        const auto thumbR = radius - 3.0f;
        const auto thumbX = on ? pill.getRight() - radius : pill.getX() + radius;
        const juce::Rectangle<float> thumb(thumbX - thumbR, pill.getCentreY() - thumbR,
                                           thumbR * 2.0f, thumbR * 2.0f);

        if (on && palette.dark)
        {
            g.setColour(palette.accent.withAlpha(0.28f));
            g.fillEllipse(thumb.expanded(4.0f));
        }

        g.setColour(on ? palette.accent : (highlighted ? palette.textSecondary : palette.textMuted));
        g.fillEllipse(thumb);
    }

private:
    void applyColours()
    {
        setColour(juce::ToggleButton::textColourId, palette.textPrimary);
    }

    TogglePalette palette;
};

class LabeledToggle final : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    LabeledToggle(juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterId,
                  TogglePalette palette = emberPalette(),
                  bool centeredStackedLayout = true)
        : apvts(state),
          id(parameterId),
          centeredStacked(centeredStackedLayout),
          lookAndFeel(palette)
    {
        if (auto* parameter = apvts.getParameter(id))
            labelText = parameter->getName(64);
        else
            labelText = id;

        toggle.setButtonText("");
        toggle.setLookAndFeel(&lookAndFeel);
        addAndMakeVisible(toggle);
        attachment = std::make_unique<ButtonAttachment>(apvts, id, toggle);
    }

    ~LabeledToggle() override
    {
        toggle.setLookAndFeel(nullptr);
    }

    static constexpr int preferredCenteredHeight() noexcept { return 40; }
    static constexpr int preferredCenteredWidth() noexcept { return 180; }

    void setPalette(TogglePalette palette)
    {
        lookAndFeel.setPalette(palette);
        repaint();
        toggle.repaint();
    }

    void setLabelText(juce::String text)
    {
        labelText = std::move(text);
        repaint();
    }

    juce::ToggleButton& getToggle() noexcept { return toggle; }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = lookAndFeel.getPalette();
        g.setColour(palette.textSecondary);
        g.setFont(uiFont(12.0f));

        if (centeredStacked || getHeight() >= 32)
        {
            g.drawFittedText(labelText,
                             getLocalBounds().removeFromTop(14),
                             centeredStacked ? juce::Justification::centred : juce::Justification::centredLeft,
                             1,
                             0.7f);
        }
        else
        {
            g.drawFittedText(labelText,
                             getLocalBounds().withTrimmedRight(50),
                             juce::Justification::centredLeft,
                             1,
                             0.7f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();

        if (centeredStacked || getHeight() >= 32)
        {
            area.removeFromTop(15);
            auto row = area.removeFromTop(24);
            if (centeredStacked)
                toggle.setBounds(row.withSizeKeepingCentre(44, 22));
            else
                toggle.setBounds(row.removeFromLeft(44).withSizeKeepingCentre(44, 22));
        }
        else
        {
            toggle.setBounds(area.removeFromRight(48).withSizeKeepingCentre(44, 22));
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::String labelText;
    bool centeredStacked = true;
    ToggleLookAndFeel lookAndFeel;
    juce::ToggleButton toggle;
    std::unique_ptr<ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledToggle)
};
} // namespace mearcstapa_toggle_button_dropin

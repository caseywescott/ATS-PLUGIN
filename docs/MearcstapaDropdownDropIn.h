#pragma once

#include <memory>
#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mearcstapa_dropdown_dropin
{
struct DropdownPalette
{
    bool dark = true;
    juce::Colour window, panel, panelBorder, well, wellBorder;
    juce::Colour textPrimary, textSecondary, textMuted, textFaint;
    juce::Colour accent, accentDim;
    juce::Colour shadowDark, shadowLight;
};

inline DropdownPalette emberPalette()
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

inline DropdownPalette porcelainPalette()
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

inline void drawWell(juce::Graphics& g, juce::Rectangle<float> r, float radius, const DropdownPalette& p)
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

class DropdownLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit DropdownLookAndFeel(DropdownPalette paletteIn = emberPalette())
        : palette(paletteIn)
    {
        applyColours();
    }

    void setPalette(DropdownPalette paletteIn)
    {
        palette = paletteIn;
        applyColours();
    }

    const DropdownPalette& getPalette() const noexcept { return palette; }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float>(0.0f, 0.0f,
                                              static_cast<float>(width),
                                              static_cast<float>(height));
        drawWell(g, r.reduced(0.5f), 8.0f, palette);

        juce::Path chevron;
        const auto cx = static_cast<float>(width) - 13.0f;
        const auto cy = static_cast<float>(height) * 0.5f;
        chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
        chevron.lineTo(cx, cy + 2.5f);
        chevron.lineTo(cx + 4.0f, cy - 2.0f);
        g.setColour(box.isEnabled() ? palette.accent : palette.textFaint);
        g.strokePath(chevron, juce::PathStrokeType(1.6f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(13.0f); }
    juce::Font getPopupMenuFont() override { return uiFont(13.0f); }
    juce::Font getLabelFont(juce::Label& label) override { return label.getFont(); }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont(uiFont(13.0f));
    }

private:
    void applyColours()
    {
        setColour(juce::Label::textColourId, palette.textPrimary);
        setColour(juce::ComboBox::backgroundColourId, palette.dark ? palette.well : palette.panel);
        setColour(juce::ComboBox::outlineColourId, palette.dark ? palette.wellBorder : palette.panelBorder);
        setColour(juce::ComboBox::textColourId, palette.textPrimary);
        setColour(juce::ComboBox::arrowColourId, palette.accent);
        setColour(juce::PopupMenu::backgroundColourId, palette.dark ? palette.window.brighter(0.04f) : palette.panel);
        setColour(juce::PopupMenu::textColourId, palette.textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId,
                  palette.accent.withAlpha(palette.dark ? 0.22f : 0.14f));
        setColour(juce::PopupMenu::highlightedTextColourId, palette.textPrimary);
    }

    DropdownPalette palette;
};

class LabeledDropdown final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    LabeledDropdown(juce::AudioProcessorValueTreeState& state,
                    const juce::String& parameterId,
                    DropdownPalette palette = emberPalette())
        : apvts(state),
          id(parameterId),
          lookAndFeel(palette)
    {
        auto* parameter = apvts.getParameter(id);
        labelText = parameter != nullptr ? parameter->getName(64) : id;

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter))
        {
            for (int i = 0; i < choice->choices.size(); ++i)
                combo.addItem(choice->choices[i], i + 1);
        }
        else
        {
            jassertfalse;
        }

        combo.setLookAndFeel(&lookAndFeel);
        addAndMakeVisible(combo);
        attachment = std::make_unique<ComboBoxAttachment>(apvts, id, combo);
    }

    ~LabeledDropdown() override
    {
        combo.setLookAndFeel(nullptr);
    }

    static constexpr int preferredHeight() noexcept { return 42; }
    static constexpr int labelHeight() noexcept { return 14; }
    static constexpr int comboHeight() noexcept { return 24; }

    void setPalette(DropdownPalette palette)
    {
        lookAndFeel.setPalette(palette);
        repaint();
        combo.repaint();
    }

    void setLabelText(juce::String text)
    {
        labelText = std::move(text);
        repaint();
    }

    juce::ComboBox& getComboBox() noexcept { return combo; }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = lookAndFeel.getPalette();
        g.setColour(palette.textSecondary);
        g.setFont(uiFont(12.0f));
        g.drawFittedText(labelText,
                         getLocalBounds().removeFromTop(labelHeight()),
                         juce::Justification::centredLeft,
                         1,
                         0.7f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(15);
        combo.setBounds(area.removeFromTop(comboHeight()));
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::String labelText;
    DropdownLookAndFeel lookAndFeel;
    juce::ComboBox combo;
    std::unique_ptr<ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledDropdown)
};
} // namespace mearcstapa_dropdown_dropin

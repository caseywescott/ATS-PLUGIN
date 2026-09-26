#pragma once

#include <memory>
#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mearcstapa_slider_dropin
{
struct SliderPalette
{
    bool dark = true;
    juce::Colour window, panel, panelBorder, well, wellBorder;
    juce::Colour textPrimary, textSecondary, textMuted, textFaint;
    juce::Colour accent, accentDim;
    juce::Colour shadowDark, shadowLight;
};

inline SliderPalette emberPalette()
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

inline SliderPalette porcelainPalette()
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

inline juce::Font monoFont(float height)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

inline void drawWell(juce::Graphics& g, juce::Rectangle<float> r, float radius, const SliderPalette& p)
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

class CompactSliderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit CompactSliderLookAndFeel(SliderPalette paletteIn = emberPalette())
        : palette(paletteIn)
    {
        applyColours();
    }

    void setPalette(SliderPalette paletteIn)
    {
        palette = paletteIn;
        applyColours();
    }

    const SliderPalette& getPalette() const noexcept { return palette; }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(monoFont(11.5f));
        label->setJustificationType(slider.getTextBoxPosition() == juce::Slider::TextBoxRight
                                        ? juce::Justification::centredRight
                                        : juce::Justification::centred);
        label->setColour(juce::Label::textColourId, palette.textPrimary);
        label->setColour(juce::Label::outlineWhenEditingColourId, palette.accent);
        return label;
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        juce::ignoreUnused(minSliderPos, maxSliderPos, slider);

        if (style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                   minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto fx = static_cast<float>(x);
        const auto fw = static_cast<float>(width);
        const auto cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const juce::Rectangle<float> track(fx, cy - 3.0f, fw, 6.0f);
        drawWell(g, track, 3.0f, palette);

        const auto fillW = juce::jlimit(0.0f, fw, sliderPos - fx);
        if (fillW > 1.0f)
        {
            const juce::Rectangle<float> fill(fx, cy - 3.0f, fillW, 6.0f);
            if (palette.dark)
            {
                g.setColour(palette.accent.withAlpha(0.25f));
                g.fillRoundedRectangle(fill.expanded(0.0f, 2.0f), 5.0f);
            }

            juce::ColourGradient gradient(palette.accentDim, fx, cy, palette.accent, fx + fw, cy, false);
            g.setGradientFill(gradient);
            g.fillRoundedRectangle(fill, 3.0f);
        }

        const auto thumbR = 7.0f;
        const juce::Rectangle<float> thumb(sliderPos - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f);
        if (palette.dark)
        {
            g.setColour(palette.shadowDark.withAlpha(0.6f));
            g.fillEllipse(thumb.translated(1.5f, 1.5f));
        }

        g.setColour(palette.accent);
        g.fillEllipse(thumb);
        g.setColour(palette.dark ? palette.window : palette.panel);
        g.fillEllipse(thumb.reduced(4.5f));
    }

private:
    void applyColours()
    {
        setColour(juce::Slider::textBoxTextColourId, palette.textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::CaretComponent::caretColourId, palette.accent);
        setColour(juce::TextEditor::highlightColourId, palette.accent.withAlpha(0.3f));
    }

    SliderPalette palette;
};

class CompactSliderRow final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    CompactSliderRow(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterId,
                     SliderPalette palette = emberPalette())
        : apvts(state), id(parameterId), lookAndFeel(palette)
    {
        if (auto* parameter = apvts.getParameter(id))
        {
            labelText = parameter->getName(64);
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        }
        else
        {
            labelText = id;
        }

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, kValueBoxW, kTextBoxH);
        slider.setLookAndFeel(&lookAndFeel);

        addAndMakeVisible(slider);
        attachment = std::make_unique<SliderAttachment>(apvts, id, slider);
    }

    ~CompactSliderRow() override
    {
        slider.setLookAndFeel(nullptr);
    }

    static constexpr int preferredHeight() noexcept { return kPreferredH; }

    void setPalette(SliderPalette palette)
    {
        lookAndFeel.setPalette(palette);
        repaint();
        slider.repaint();
    }

    void setLabelText(juce::String text)
    {
        labelText = std::move(text);
        repaint();
    }

    void setForceTwoDecimalDisplay(bool shouldForce)
    {
        if (shouldForce)
            slider.textFromValueFunction = [] (double value) { return juce::String(value, 2); };
        else
            slider.textFromValueFunction = {};

        slider.updateText();
    }

    juce::Slider& getSlider() noexcept { return slider; }

    void paint(juce::Graphics& g) override
    {
        const auto& palette = lookAndFeel.getPalette();
        auto labelRow = getLocalBounds().removeFromTop(kLabelH);

        g.setColour(palette.textSecondary);
        g.setFont(uiFont(12.0f));
        g.drawFittedText(labelText, labelRow, juce::Justification::centredLeft, 1, 0.7f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(kLabelH);
        slider.setBounds(area.removeFromTop(kSliderRowH));
    }

private:
    static constexpr int kPreferredH = 42;
    static constexpr int kLabelH = 14;
    static constexpr int kSliderRowH = 24;
    static constexpr int kValueBoxW = 52;
    static constexpr int kTextBoxH = 15;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::String labelText;
    CompactSliderLookAndFeel lookAndFeel;
    juce::Slider slider;
    std::unique_ptr<SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompactSliderRow)
};
} // namespace mearcstapa_slider_dropin

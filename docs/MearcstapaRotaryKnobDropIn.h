#pragma once

#include <memory>
#include <utility>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mearcstapa_rotary_knob_dropin
{
struct RotaryKnobPalette
{
    bool dark = true;
    juce::Colour window, panel, panelBorder, well, wellBorder;
    juce::Colour textPrimary, textSecondary, textMuted, textFaint;
    juce::Colour accent, accentDim;
    juce::Colour shadowDark, shadowLight;
};

inline RotaryKnobPalette emberPalette()
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

inline RotaryKnobPalette porcelainPalette()
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

class RotaryKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit RotaryKnobLookAndFeel(RotaryKnobPalette paletteIn = emberPalette())
        : palette(paletteIn)
    {
        applyColours();
    }

    void setPalette(RotaryKnobPalette paletteIn)
    {
        palette = paletteIn;
        applyColours();
    }

    const RotaryKnobPalette& getPalette() const noexcept { return palette; }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centre = bounds.getCentre();
        const auto arcRadius = radius - 2.5f;
        const auto knobRadius = juce::jmax(6.0f, arcRadius - 7.0f);
        const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        const juce::Rectangle<float> knob(centre.x - knobRadius, centre.y - knobRadius,
                                          knobRadius * 2.0f, knobRadius * 2.0f);
        if (palette.dark)
        {
            g.setColour(palette.shadowDark.withAlpha(0.55f));
            g.fillEllipse(knob.translated(2.0f, 2.0f).expanded(0.5f));
            g.setColour(palette.shadowLight.withAlpha(0.45f));
            g.fillEllipse(knob.translated(-2.0f, -2.0f).expanded(0.5f));

            juce::ColourGradient gradient(palette.panel.brighter(0.09f), knob.getX(), knob.getY(),
                                          palette.panel.darker(0.22f), knob.getRight(), knob.getBottom(), false);
            g.setGradientFill(gradient);
            g.fillEllipse(knob);
        }
        else
        {
            g.setColour(palette.panel);
            g.fillEllipse(knob);
            g.setColour(palette.panelBorder);
            g.drawEllipse(knob.reduced(0.5f), 1.0f);
        }

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(palette.dark ? palette.well : palette.well.darker(0.04f));
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, angle, true);
        if (palette.dark)
        {
            g.setColour(palette.accent.withAlpha(0.26f));
            g.strokePath(value, juce::PathStrokeType(6.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }

        g.setColour(palette.accent);
        g.strokePath(value, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        const auto pointer = juce::Line<float>::fromStartAndAngle({ centre.x, centre.y },
                                                                  knobRadius - 2.5f,
                                                                  angle);
        g.setColour(palette.textPrimary);
        g.drawLine(juce::Line<float>(pointer.getPointAlongLine(knobRadius * 0.35f),
                                     pointer.getEnd()),
                   2.0f);
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout(slider);

        if (slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
            && slider.getTextBoxPosition() == juce::Slider::TextBoxBelow
            && static_cast<bool>(slider.getProperties()["tightRotary"]))
        {
            auto bounds = slider.getLocalBounds();
            const auto textH = 13;
            const auto gap = 2;
            const auto knobSide = juce::jmin(bounds.getWidth(), bounds.getHeight() - textH - gap);

            layout.sliderBounds = bounds.removeFromTop(knobSide);
            layout.textBoxBounds = bounds.removeFromTop(textH);
        }

        return layout;
    }

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

private:
    void applyColours()
    {
        setColour(juce::Slider::textBoxTextColourId, palette.textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, palette.accent);
        setColour(juce::TextEditor::highlightColourId, palette.accent.withAlpha(0.3f));
        setColour(juce::CaretComponent::caretColourId, palette.accent);
    }

    RotaryKnobPalette palette;
};

class RotaryKnob final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    RotaryKnob(juce::AudioProcessorValueTreeState& state,
               const juce::String& parameterId,
               RotaryKnobPalette palette = emberPalette(),
               bool useTightLayout = false)
        : apvts(state),
          id(parameterId),
          tightLayout(useTightLayout),
          lookAndFeel(palette)
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

        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f,
                                   true);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, tightLayout ? 13 : 15);
        if (tightLayout)
            slider.getProperties().set("tightRotary", true);

        slider.setLookAndFeel(&lookAndFeel);
        addAndMakeVisible(slider);
        attachment = std::make_unique<SliderAttachment>(apvts, id, slider);
    }

    ~RotaryKnob() override
    {
        slider.setLookAndFeel(nullptr);
    }

    static constexpr int preferredCellHeight() noexcept { return 132; }

    void setPalette(RotaryKnobPalette palette)
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
        g.setColour(palette.textSecondary);
        g.setFont(uiFont(12.0f));
        g.drawFittedText(labelText,
                         getLocalBounds().removeFromBottom(tightLayout ? 12 : 14),
                         juce::Justification::centred,
                         1,
                         0.7f);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        if (tightLayout)
        {
            constexpr auto labelH = 12;
            constexpr auto textH = 13;
            constexpr auto gap = 2;
            area.removeFromBottom(labelH);
            const auto knobSide = juce::jmin(area.getWidth(), area.getHeight() - textH - gap);
            slider.setBounds(area.getX(), area.getY(), area.getWidth(), knobSide + gap + textH);
        }
        else
        {
            area.removeFromBottom(14);
            slider.setBounds(area);
        }
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::String labelText;
    bool tightLayout = false;
    RotaryKnobLookAndFeel lookAndFeel;
    juce::Slider slider;
    std::unique_ptr<SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnob)
};
} // namespace mearcstapa_rotary_knob_dropin

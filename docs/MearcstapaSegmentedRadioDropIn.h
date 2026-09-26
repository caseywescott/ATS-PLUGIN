#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mearcstapa_segmented_radio_dropin
{
struct SegmentedRadioPalette
{
    bool dark = true;
    juce::Colour window, panel, panelBorder, well, wellBorder;
    juce::Colour textPrimary, textSecondary, textMuted, textFaint;
    juce::Colour accent, accentDim;
    juce::Colour shadowDark, shadowLight;
};

inline SegmentedRadioPalette emberPalette()
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

inline SegmentedRadioPalette porcelainPalette()
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

inline void drawRaisedPanel(juce::Graphics& g, juce::Rectangle<float> r, float radius, const SegmentedRadioPalette& p)
{
    if (p.dark)
    {
        for (int i = 1; i <= 3; ++i)
        {
            const auto offset = 2.0f * static_cast<float>(i);
            g.setColour(p.shadowDark.withAlpha(0.22f / static_cast<float>(i)));
            g.fillRoundedRectangle(r.translated(offset, offset).expanded(static_cast<float>(i) * 0.5f), radius + static_cast<float>(i));
            g.setColour(p.shadowLight.withAlpha(0.16f / static_cast<float>(i)));
            g.fillRoundedRectangle(r.translated(-offset, -offset).expanded(static_cast<float>(i) * 0.5f), radius + static_cast<float>(i));
        }

        juce::ColourGradient gradient(p.panel.brighter(0.05f), r.getX(), r.getY(),
                                      p.panel.darker(0.06f), r.getRight(), r.getBottom(), false);
        g.setGradientFill(gradient);
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

inline void drawWell(juce::Graphics& g, juce::Rectangle<float> r, float radius, const SegmentedRadioPalette& p)
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

class SegmentedRadio final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    SegmentedRadio(juce::AudioProcessorValueTreeState& state,
                   const juce::String& parameterId,
                   SegmentedRadioPalette paletteIn = emberPalette())
        : apvts(state),
          id(parameterId),
          palette(paletteIn),
          parameter(dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id)))
    {
        jassert(parameter != nullptr);

        if (parameter != nullptr)
            attachment = std::make_unique<juce::ParameterAttachment>(*parameter, [this] (float) { repaint(); });
    }

    static constexpr int preferredHeight() noexcept { return 30; }
    static constexpr int preferredWidthForTwoChoices() noexcept { return 280; }
    static constexpr int preferredGapBelow() noexcept { return 10; }

    void setPalette(SegmentedRadioPalette paletteIn)
    {
        palette = paletteIn;
        repaint();
    }

    void setGreyedOut(bool shouldGreyOut)
    {
        if (greyedOut == shouldGreyOut)
            return;

        greyedOut = shouldGreyOut;
        hoveredSeg = -1;
        setInterceptsMouseClicks(! greyedOut, false);
        repaint();
    }

    bool isGreyedOut() const noexcept { return greyedOut; }

    void paint(juce::Graphics& g) override
    {
        if (parameter == nullptr)
            return;

        const auto bounds = getLocalBounds().toFloat();
        const auto radius = juce::jmin(bounds.getHeight() * 0.5f, 12.0f);

        drawWell(g, bounds, radius, palette);

        const auto count = parameter->choices.size();
        if (count <= 0)
            return;

        const auto selected = parameter->getIndex();
        const auto segW = bounds.getWidth() / static_cast<float>(count);

        for (int i = 0; i < count; ++i)
        {
            auto seg = juce::Rectangle<float>(bounds.getX() + segW * static_cast<float>(i),
                                              bounds.getY(), segW, bounds.getHeight());
            const auto isSelected = i == selected;
            const auto isHovered = ! greyedOut && i == hoveredSeg;
            const auto pill = seg.reduced(3.0f);
            const auto pillRadius = juce::jmax(2.0f, radius - 3.0f);

            if (greyedOut)
            {
                if (isSelected)
                {
                    g.setColour(palette.textFaint.withAlpha(0.18f));
                    g.fillRoundedRectangle(pill, pillRadius);
                }
            }
            else if (isSelected)
            {
                drawRaisedPanel(g, pill, pillRadius, palette);
                g.setColour(palette.accent.withAlpha(palette.dark ? 0.16f : 0.1f));
                g.fillRoundedRectangle(pill, pillRadius);
                g.setColour(palette.accent.withAlpha(0.8f));
                g.drawRoundedRectangle(pill.reduced(0.75f), pillRadius, 1.2f);
            }
            else if (isHovered)
            {
                g.setColour(palette.accent.withAlpha(0.08f));
                g.fillRoundedRectangle(pill, pillRadius);
            }

            g.setColour(greyedOut ? palette.textFaint.withAlpha(0.5f)
                                  : (isSelected ? palette.accent : (isHovered ? palette.textPrimary : palette.textSecondary)));
            g.setFont(uiFont(count > 3 ? 10.0f : 12.5f));
            g.drawFittedText(parameter->choices[i], seg.toNearestInt(), juce::Justification::centred, 1, 0.85f);
        }

        if (greyedOut)
        {
            g.setColour(palette.panel.withAlpha(palette.dark ? 0.28f : 0.18f));
            g.fillRoundedRectangle(bounds, radius);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override { updateHover(event); }
    void mouseEnter(const juce::MouseEvent& event) override { updateHover(event); }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredSeg = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (greyedOut)
            return;

        const auto seg = segAt(event.getPosition());
        if (seg >= 0 && attachment != nullptr)
            attachment->setValueAsCompleteGesture(static_cast<float>(seg));
    }

private:
    int segAt(juce::Point<int> position) const
    {
        if (parameter == nullptr || getWidth() <= 0)
            return -1;

        const auto count = parameter->choices.size();
        if (count <= 0 || ! getLocalBounds().contains(position))
            return -1;

        const auto seg = position.x * count / getWidth();
        return juce::jlimit(0, count - 1, seg);
    }

    void updateHover(const juce::MouseEvent& event)
    {
        if (greyedOut)
        {
            if (hoveredSeg != -1)
            {
                hoveredSeg = -1;
                repaint();
            }
            return;
        }

        const auto seg = segAt(event.getPosition());
        if (seg != hoveredSeg)
        {
            hoveredSeg = seg;
            repaint();
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    SegmentedRadioPalette palette;
    juce::AudioParameterChoice* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int hoveredSeg = -1;
    bool greyedOut = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentedRadio)
};
} // namespace mearcstapa_segmented_radio_dropin

#include "NeumorphicCheckboxLookAndFeel.h"
#include "UiFonts.h"

#include <BinaryData.h>

NeumorphicCheckboxLookAndFeel::NeumorphicCheckboxLookAndFeel()
    : outerBevelImage (loadPng (BinaryData::outer_bevel_2x_png, BinaryData::outer_bevel_2x_pngSize,
                                BinaryData::outer_bevel_1x_png, BinaryData::outer_bevel_1x_pngSize))
    , checkmarkImage  (loadPng (BinaryData::checkmark_2x_png,   BinaryData::checkmark_2x_pngSize,
                                BinaryData::checkmark_1x_png,   BinaryData::checkmark_1x_pngSize))
{
    // Register Outfit SemiBold as the default sans-serif so that all JUCE
    // text rendering (toggle button labels, tab bar, combo boxes, etc.)
    // automatically uses it when constructed with a plain juce::Font(size).
    if (auto tf = UiFonts::getTypefacePtr())
        setDefaultSansSerifTypeface (tf);
}

void NeumorphicCheckboxLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                      juce::ToggleButton& button,
                                                      bool /*shouldDrawButtonAsHighlighted*/,
                                                      bool /*shouldDrawButtonAsDown*/)
{
    const auto bounds  = button.getLocalBounds().toFloat();

    // Match JUCE LookAndFeel_V4's native ToggleButton tick: a 16.5 px square
    // in the standard 24 px-high transport row.  Keeping this in the same
    // footprint prevents the Figma asset from appearing oversized or stretched
    // relative to the label.
    const float boxSize = juce::jmin (bounds.getHeight(), 16.5f);
    const float boxX    = bounds.getX();
    const float boxY    = bounds.getCentreY() - boxSize * 0.5f;

    // Uniform scale: both axes use the same factor so the 120×120 Figma frame
    // maps to the square box without distortion.
    const float s = boxSize / kFrameSize;

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const float opacity = button.isEnabled() ? 1.0f : 0.38f;

    // Maps one PNG layer (with Figma shadow padding) to the correct position inside
    // the checkbox square.  Uses explicit 9-arg drawImage so the src→dst stretch is
    // unambiguous regardless of which JUCE overload the compiler resolves.
    // logX/logY/logW/logH: visual content position in 1x Figma space (120×120 frame).
    // pngW/pngH: actual 1x PNG pixel size (includes shadow bleed on all sides).
    auto drawLayer = [&] (const juce::Image& img,
                          float logX, float logY, float logW, float logH,
                          float pngW, float pngH)
    {
        if (!img.isValid())
            return;
        const float padX = (pngW - logW) * 0.5f;
        const float padY = (pngH - logH) * 0.5f;
        g.drawImage (img,
                     boxX + (logX - padX) * s,  boxY + (logY - padY) * s,
                     pngW * s,                    pngH * s,
                     0.0f, 0.0f,
                     (float) img.getWidth(), (float) img.getHeight(),
                     false);
    };

    // The main_body and inner_cap exports each contain a complete, offset button
    // shell and shadow. Stacking them here caused the visible duplicate checkbox.
    // outer_bevel is the complete unchecked surface.
    {
        const float glowBudget = 2.0f * s;   // outer_bevel horizontal pad = (124-120)/2 = 2 logical units
        const juce::Rectangle<float> clipBox (boxX - glowBudget, boxY - glowBudget,
                                              boxSize + 2.0f * glowBudget,
                                              boxSize + 2.0f * glowBudget);

        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clipBox.toNearestIntEdges());
        g.setOpacity (opacity);

        drawLayer (outerBevelImage, kBevelLogX, kBevelLogY, kBevelLogW, kBevelLogH, kBevelPngW, kBevelPngH);

        if (button.getToggleState())
            drawLayer (checkmarkImage, kMarkLogX, kMarkLogY, kMarkLogW, kMarkLogH, kMarkPngW, kMarkPngH);
    }

    // Label text drawn outside the clip state so it is never accidentally clipped.
    {
        // JUCE positions the label tickWidth + 10 px from the component's left.
        const float gap      = 10.0f;
        const float textX    = boxX + boxSize + gap;
        const float textW    = bounds.getRight() - textX;
        const float fontSize = juce::jmin (boxSize * 0.75f, 13.0f);

        juce::Graphics::ScopedSaveState ss (g);
        g.setOpacity (opacity);
        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (juce::Font (fontSize));  // resolves to Outfit SemiBold via default typeface
        g.drawFittedText (button.getButtonText(),
                          juce::Rectangle<float> (textX, bounds.getY(), textW, bounds.getHeight()).toNearestInt(),
                          juce::Justification::centredLeft,
                          1);
    }
}

void NeumorphicCheckboxLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Knob labels: gradient fill + 1-px drop shadow per the Figma neumorphic spec.
    if (label.getComponentID() == "knobLabel")
    {
        if (label.isBeingEdited())
            return;

        const auto bounds = label.getLocalBounds();
        const auto font   = label.getFont();

        if (!label.isEnabled())
            g.setOpacity (0.38f);

        UiFonts::paintKnobLabel (g, bounds, label.getText(), font);
        return;
    }

    // All other labels: delegate to LookAndFeel_V4 default but use Outfit typeface.
    juce::LookAndFeel_V4::drawLabel (g, label);
}

juce::Image NeumorphicCheckboxLookAndFeel::loadPng (const void* data2x, int size2x,
                                                    const void* data1x,  int size1x)
{
    auto image = juce::ImageCache::getFromMemory (data2x, size2x);
    if (!image.isValid())
        image = juce::ImageCache::getFromMemory (data1x, size1x);
    return image;
}

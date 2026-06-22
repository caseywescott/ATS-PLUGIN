#include "NeumorphicSliderLookAndFeel.h"

#include <BinaryData.h>

namespace
{
float getFloatProperty(const juce::DynamicObject* object, const juce::Identifier& name, float fallback)
{
    if (object == nullptr)
        return fallback;

    const auto value = object->getProperty(name);
    return value.isDouble() || value.isInt() ? (float) value : fallback;
}

const juce::DynamicObject* getObjectProperty(const juce::DynamicObject* object, const juce::Identifier& name)
{
    if (object == nullptr)
        return nullptr;

    return object->getProperty(name).getDynamicObject();
}
} // namespace

NeumorphicSliderLookAndFeel::NeumorphicSliderLookAndFeel()
    : metrics(loadMetrics())
    , trackImage(loadPng(BinaryData::track_recess_2x_png,
                         BinaryData::track_recess_2x_pngSize,
                         BinaryData::track_recess_1x_png,
                         BinaryData::track_recess_1x_pngSize))
    , fillImage(loadPng(BinaryData::active_fill_2x_png,
                        BinaryData::active_fill_2x_pngSize,
                        BinaryData::active_fill_1x_png,
                        BinaryData::active_fill_1x_pngSize))
    , thumbImage(loadPng(BinaryData::handle_2x_png,
                         BinaryData::handle_2x_pngSize,
                         BinaryData::handle_1x_png,
                         BinaryData::handle_1x_pngSize))
{
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xfff0f0f5));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(0x5537ff75));
}

juce::Slider::SliderLayout NeumorphicSliderLookAndFeel::getSliderLayout(juce::Slider& slider)
{
    juce::Slider::SliderLayout layout;
    auto bounds = slider.getLocalBounds();

    const auto textBoxPos = slider.getTextBoxPosition();
    if (textBoxPos == juce::Slider::NoTextBox)
    {
        layout.sliderBounds = bounds;
        return layout;
    }

    const int gap = 4;
    const int textW = juce::jmin(slider.getTextBoxWidth(), bounds.getWidth());
    const int textH = juce::jmin(slider.getTextBoxHeight(), bounds.getHeight());

    if (textBoxPos == juce::Slider::TextBoxRight)
    {
        layout.textBoxBounds = bounds.removeFromRight(textW).withSizeKeepingCentre(textW, textH);
        bounds.removeFromRight(juce::jmin(gap, bounds.getWidth()));
        layout.sliderBounds = bounds;
        return layout;
    }

    if (textBoxPos == juce::Slider::TextBoxLeft)
    {
        layout.textBoxBounds = bounds.removeFromLeft(textW).withSizeKeepingCentre(textW, textH);
        bounds.removeFromLeft(juce::jmin(gap, bounds.getWidth()));
        layout.sliderBounds = bounds;
        return layout;
    }

    if (textBoxPos == juce::Slider::TextBoxAbove)
    {
        layout.textBoxBounds = bounds.removeFromTop(textH).withSizeKeepingCentre(textW, textH);
        bounds.removeFromTop(juce::jmin(gap, bounds.getHeight()));
        layout.sliderBounds = bounds;
        return layout;
    }

    layout.textBoxBounds = bounds.removeFromBottom(textH).withSizeKeepingCentre(textW, textH);
    bounds.removeFromBottom(juce::jmin(gap, bounds.getHeight()));
    layout.sliderBounds = bounds;
    return layout;
}

void NeumorphicSliderLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                                   int x,
                                                   int y,
                                                   int width,
                                                   int height,
                                                   float sliderPos,
                                                   float minSliderPos,
                                                   float maxSliderPos,
                                                   juce::Slider::SliderStyle style,
                                                   juce::Slider& slider)
{
    juce::ignoreUnused(sliderPos, minSliderPos, maxSliderPos);

    if (style != juce::Slider::LinearHorizontal
        || !metrics.horizontal
        || !trackImage.isValid()
        || !fillImage.isValid()
        || !thumbImage.isValid())
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos,
                                               maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    if (bounds.isEmpty())
        return;

    const float scale = juce::jmin(bounds.getWidth() / metrics.exportFrameWidth,
                                  bounds.getHeight() / metrics.exportFrameHeight);
    const auto frame = juce::Rectangle<float>(metrics.exportFrameWidth * scale,
                                             metrics.exportFrameHeight * scale)
                           .withCentre(bounds.getCentre());
    const auto track = juce::Rectangle<float>(frame.getX() + metrics.track.getX() * scale,
                                             frame.getY() + metrics.track.getY() * scale,
                                             metrics.track.getWidth() * scale,
                                             metrics.track.getHeight() * scale);

    const float normalised = (float) juce::jlimit(0.0, 1.0, slider.valueToProportionOfLength(slider.getValue()));
    const float thumbX = frame.getX()
                         + (metrics.thumbMinCenterX
                            + normalised * (metrics.thumbMaxCenterX - metrics.thumbMinCenterX))
                               * scale;
    const float thumbY = frame.getY() + metrics.thumbCenterY * scale;
    const float fillWidth = normalised * track.getWidth();

    juce::Graphics::ScopedSaveState alphaState(g);
    g.setOpacity(slider.isEnabled() ? 1.0f : 0.38f);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

    g.drawImage(trackImage, track, juce::RectanglePlacement::stretchToFit, false);

    if (fillWidth > 0.5f)
    {
        juce::Graphics::ScopedSaveState fillState(g);
        g.reduceClipRegion(juce::Rectangle<int>(juce::roundToInt(track.getX()),
                                                juce::roundToInt(track.getY()),
                                                juce::roundToInt(fillWidth),
                                                juce::roundToInt(track.getHeight())));
        g.drawImage(fillImage, track, juce::RectanglePlacement::stretchToFit, false);
    }

    const float maxThumbH = 2.0f * juce::jmin(thumbY - frame.getY(), frame.getBottom() - thumbY);
    const float thumbH = juce::jmax(1.0f, maxThumbH);
    const float thumbW = thumbImage.getWidth() > 0 && thumbImage.getHeight() > 0
                             ? thumbH * ((float) thumbImage.getWidth() / (float) thumbImage.getHeight())
                             : metrics.thumbDiameter * scale;
    const auto thumb = juce::Rectangle<float>(thumbW, thumbH).withCentre({ thumbX, thumbY });
    g.drawImage(thumbImage, thumb, juce::RectanglePlacement::stretchToFit, false);
}

NeumorphicSliderLookAndFeel::Metrics NeumorphicSliderLookAndFeel::loadMetrics()
{
    Metrics result;

    const auto jsonText = juce::String::fromUTF8(BinaryData::manifest_json, BinaryData::manifest_jsonSize);
    const auto parsed = juce::JSON::parse(jsonText);
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return result;

    const auto orientation = root->getProperty("orientation").toString();
    if (orientation.isNotEmpty())
        result.horizontal = orientation.equalsIgnoreCase("horizontal");

    result.baseDisplayWidth = getFloatProperty(root, "baseDisplayWidth", result.baseDisplayWidth);
    result.baseDisplayHeight = getFloatProperty(root, "baseDisplayHeight", result.baseDisplayHeight);
    result.exportFrameWidth = getFloatProperty(root, "exportFrameWidth", result.exportFrameWidth);
    result.exportFrameHeight = getFloatProperty(root, "exportFrameHeight", result.exportFrameHeight);

    if (const auto* track = getObjectProperty(root, "track"))
    {
        result.track = {
            getFloatProperty(track, "x", result.track.getX()),
            getFloatProperty(track, "y", result.track.getY()),
            getFloatProperty(track, "width", result.track.getWidth()),
            getFloatProperty(track, "height", result.track.getHeight())
        };
    }

    if (const auto* thumb = getObjectProperty(root, "thumb"))
    {
        result.thumbDiameter = getFloatProperty(thumb, "diameter", result.thumbDiameter);
        result.thumbCenterY = getFloatProperty(thumb, "centerY", result.thumbCenterY);
        result.thumbMinCenterX = getFloatProperty(thumb, "minCenterX", result.thumbMinCenterX);
        result.thumbMaxCenterX = getFloatProperty(thumb, "maxCenterX", result.thumbMaxCenterX);
    }

    return result;
}

juce::Image NeumorphicSliderLookAndFeel::loadPng(const void* data2x,
                                                 int data2xSize,
                                                 const void* data1x,
                                                 int data1xSize)
{
    auto image = juce::ImageCache::getFromMemory(data2x, data2xSize);
    if (!image.isValid())
        image = juce::ImageCache::getFromMemory(data1x, data1xSize);
    return image;
}

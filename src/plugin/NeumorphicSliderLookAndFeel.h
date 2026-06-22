#pragma once

#include <JuceHeader.h>

class NeumorphicSliderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeumorphicSliderLookAndFeel();

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override;

    void drawLinearSlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

private:
    struct Metrics
    {
        bool horizontal = true;
        float baseDisplayWidth = 180.0f;
        float baseDisplayHeight = 40.0f;
        float exportFrameWidth = 220.0f;
        float exportFrameHeight = 64.0f;
        juce::Rectangle<float> track { 44.0f, 28.0f, 132.0f, 7.0f };
        float thumbDiameter = 16.0f;
        float thumbCenterY = 38.8f;
        float thumbMinCenterX = 44.0f;
        float thumbMaxCenterX = 176.0f;
    };

    static Metrics loadMetrics();
    static juce::Image loadPng(const void* data2x,
                               int data2xSize,
                               const void* data1x,
                               int data1xSize);

    Metrics metrics;
    juce::Image trackImage;
    juce::Image fillImage;
    juce::Image thumbImage;
};

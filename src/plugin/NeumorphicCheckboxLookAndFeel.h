#pragma once
#include <JuceHeader.h>

/**
 * Plugin-wide LookAndFeel that replaces the native JUCE toggle-button checkbox
 * with Figma-exported neumorphic PNG assets from assets/checkbox/static/.
 *
 * Extends juce::LookAndFeel_V4 and only overrides drawToggleButton.
 * All other JUCE rendering (sliders, combo-boxes, progress bars, …) falls
 * through to the standard LookAndFeel_V4 implementation unchanged.
 *
 * Asset layout (Figma 1x logical space, base frame 120x120):
 *   outer_bevel  (0,0,120,120)    – complete unchecked button surface
 *   checkmark    (30,42,56,39)    – tick mark, drawn only when toggled ON
 *
 * PNG padding = (pngPixels - logicalSize) / 2 per side (shadow bleed).
 * Assets loaded from BinaryData (2x preferred, 1x fallback).
 *
 * Labels with componentID "knobLabel" receive a gradient/shadow paint treatment
 * per the Figma neumorphic spec (muted blue-grey, LIGHTEN-style, 1-px drop shadow).
 */
class NeumorphicCheckboxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeumorphicCheckboxLookAndFeel();

    void drawToggleButton (juce::Graphics& g,
                           juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawLabel (juce::Graphics& g, juce::Label& label) override;

private:
    // Figma 1x logical coordinates within the 120x120 export frame.
    // PNG padding = (pngSize_1x - logicalSize) / 2 per side.
    static constexpr float kFrameSize  = 120.0f;

    // outer_bevel: logical (0,0,120,120), 1x PNG: (124,127)
    static constexpr float kBevelLogX = 0.0f,  kBevelLogY = 0.0f;
    static constexpr float kBevelLogW = 120.0f, kBevelLogH = 120.0f;
    static constexpr float kBevelPngW = 124.0f, kBevelPngH = 127.0f;

    // main_body: logical (8,8,105,105), 1x PNG: (149,173)
    static constexpr float kBodyLogX  = 8.0f,   kBodyLogY  = 8.0f;
    static constexpr float kBodyLogW  = 105.0f,  kBodyLogH  = 105.0f;
    static constexpr float kBodyPngW  = 149.0f,  kBodyPngH  = 173.0f;

    // inner_cap: logical (17,17,87,87), 1x PNG: (125,127)
    static constexpr float kCapLogX   = 17.0f,  kCapLogY   = 17.0f;
    static constexpr float kCapLogW   = 87.0f,   kCapLogH   = 87.0f;
    static constexpr float kCapPngW   = 125.0f,  kCapPngH   = 127.0f;

    // checkmark: logical (30,42,56,39), 1x PNG: (133,116)
    static constexpr float kMarkLogX  = 30.0f,  kMarkLogY  = 42.0f;
    static constexpr float kMarkLogW  = 56.0f,   kMarkLogH  = 39.0f;
    static constexpr float kMarkPngW  = 133.0f,  kMarkPngH  = 116.0f;

    juce::Image outerBevelImage;
    juce::Image checkmarkImage;

    static juce::Image loadPng (const void* data2x, int size2x,
                                const void* data1x,  int size1x);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeumorphicCheckboxLookAndFeel)
};

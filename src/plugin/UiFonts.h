#pragma once
#include <JuceHeader.h>
#include <BinaryData.h>
#include <algorithm>
#include <cmath>

/**
 * Central font factory for the ATS plugin UI.
 * Primary typeface: Outfit SemiBold (embedded).
 * Fallback: Inter Medium (embedded).
 *
 * Size tiers mirror the plugin's visual hierarchy:
 *   heading  13–15 px  section titles, large readouts
 *   body     11–12 px  standard labels, control rows
 *   small    10–10.5 px  compact rows, hints
 *   tiny     9 px       axis ticks, dense annotations
 *   mono     any px     code editors (system monospace)
 *
 * knobLabel(S) implements the Figma knob-label spec:
 *   size  = max(12, round(S * 0.054))
 *   tracking = 8 % extra kerning
 */
namespace UiFonts
{

inline juce::Typeface::Ptr getTypefacePtr()
{
    static auto tf = []() -> juce::Typeface::Ptr
    {
        // BinaryData names: hyphens stripped, e.g. Outfit-SemiBold.ttf → OutfitSemiBold_ttf
        auto p = juce::Typeface::createSystemTypefaceFor(
            BinaryData::OutfitSemiBold_ttf, BinaryData::OutfitSemiBold_ttfSize);
        if (p != nullptr)
            return p;
        return juce::Typeface::createSystemTypefaceFor(
            BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
    }();
    return tf;
}

inline juce::Font at(float px)
{
    auto tf = getTypefacePtr();
    return tf ? juce::Font(tf).withHeight(px) : juce::Font(px);
}

// 8 % letter-spacing as JUCE extra kerning factor
inline juce::Font withTracking(juce::Font f)
{
    return f.withExtraKerningFactor(0.08f);
}

// ── size tiers ──────────────────────────────────────────────────────────────

inline juce::Font heading(float px = 13.0f) { return at(px); }
inline juce::Font body(float px = 11.0f)    { return at(px); }
inline juce::Font small_(float px = 10.5f)  { return at(px); }
inline juce::Font tiny(float px = 9.0f)     { return at(px); }

inline juce::Font mono(float px = 13.0f)
{
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain);
}

// Knob-label font: size scales with knob diameter S per Figma spec
inline juce::Font knobLabel(float knobSize = 0.0f)
{
    const float px = knobSize > 0.0f
                         ? std::max(12.0f, std::round(knobSize * 0.054f))
                         : 12.0f;
    return withTracking(at(px));
}

// ── knob-label colour spec ───────────────────────────────────────────────────
// Figma gradient: rgba(125,131,145) → rgba(106,112,126) vertical, LIGHTEN blend.
// Used as solid fill; the muted blue-grey reads as engraved on the dark background.
inline juce::Colour knobLabelColour()
{
    return juce::Colour(0xff7d8391); // midpoint of the two gradient stops
}

// ── knob-label painting helper ───────────────────────────────────────────────
// Draws text with a two-stop vertical gradient (engraved / recessed look) and
// a 1-px drop shadow. Call from a custom Label paint override.
inline void paintKnobLabel(juce::Graphics& g,
                            const juce::Rectangle<int>& bounds,
                            const juce::String& text,
                            juce::Font font)
{
    // Shadow (offset 0,1, 1-px blur, dark semi-transparent)
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.setColour(juce::Colour(0x88000000));
        g.setFont(font);
        g.drawFittedText(text,
                         bounds.translated(0, 1),
                         juce::Justification::centredTop,
                         1);
    }

    // Gradient fill: two-stop vertical, rgba(125,131,145) → rgba(106,112,126)
    const juce::ColourGradient grad(juce::Colour(0xff7d8391),
                                    0.0f, (float) bounds.getY(),
                                    juce::Colour(0xff6a707e),
                                    0.0f, (float) bounds.getBottom(),
                                    false);
    g.setGradientFill(grad);
    g.setFont(font);
    g.drawFittedText(text, bounds, juce::Justification::centredTop, 1);
}

} // namespace UiFonts

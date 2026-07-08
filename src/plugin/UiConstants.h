#pragma once

#include <cstdint>

#include <JuceHeader.h>

namespace ats_editor
{
/** Documented soft cap for partials × frames (see BUILDING.md). */
constexpr int64_t kModelSizeSoftCap = 2'000'000;
} // namespace ats_editor

namespace ats_ui
{
namespace colour
{
inline juce::Colour background()     { return juce::Colour(0xff0f1113); }
inline juce::Colour header()         { return juce::Colour(0xff15191d); }
inline juce::Colour surface()        { return juce::Colour(0xff191f24); }
inline juce::Colour surfaceRaised()  { return juce::Colour(0xff222a30); }
inline juce::Colour surfacePressed() { return juce::Colour(0xff2b343b); }
inline juce::Colour border()         { return juce::Colour(0xff344149); }
inline juce::Colour borderStrong()   { return juce::Colour(0xff4f626b); }
inline juce::Colour text()           { return juce::Colour(0xfff1f6f2); }
inline juce::Colour textMuted()      { return juce::Colour(0xffa8b5ae); }
inline juce::Colour accent()         { return juce::Colour(0xff5bd6b2); }
inline juce::Colour accentSoft()     { return juce::Colour(0xff2f7e6d); }
inline juce::Colour amber()          { return juce::Colour(0xffffc857); }
inline juce::Colour error()          { return juce::Colour(0xffff8a7a); }
inline juce::Colour warning()        { return juce::Colour(0xffffc88a); }
} // namespace colour

inline void applyEditorPalette(juce::LookAndFeel_V4& lookAndFeel)
{
    using namespace colour;

    lookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, background());
    lookAndFeel.setColour(juce::Label::textColourId, text());
    lookAndFeel.setColour(juce::TextButton::buttonColourId, surfaceRaised());
    lookAndFeel.setColour(juce::TextButton::buttonOnColourId, accentSoft());
    lookAndFeel.setColour(juce::TextButton::textColourOffId, text());
    lookAndFeel.setColour(juce::TextButton::textColourOnId, text());
    lookAndFeel.setColour(juce::ToggleButton::textColourId, text());
    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, surface());
    lookAndFeel.setColour(juce::ComboBox::textColourId, text());
    lookAndFeel.setColour(juce::ComboBox::outlineColourId, border());
    lookAndFeel.setColour(juce::ComboBox::arrowColourId, accent());
    lookAndFeel.setColour(juce::PopupMenu::backgroundColourId, surface());
    lookAndFeel.setColour(juce::PopupMenu::textColourId, text());
    lookAndFeel.setColour(juce::Slider::rotarySliderFillColourId, accent());
    lookAndFeel.setColour(juce::Slider::rotarySliderOutlineColourId, border());
    lookAndFeel.setColour(juce::Slider::trackColourId, accentSoft());
    lookAndFeel.setColour(juce::Slider::thumbColourId, text());
    lookAndFeel.setColour(juce::TextEditor::backgroundColourId, surface());
    lookAndFeel.setColour(juce::TextEditor::textColourId, text());
    lookAndFeel.setColour(juce::TextEditor::outlineColourId, border());
    lookAndFeel.setColour(juce::ProgressBar::backgroundColourId, surface());
    lookAndFeel.setColour(juce::ProgressBar::foregroundColourId, accent());
    lookAndFeel.setColour(juce::TabbedComponent::backgroundColourId, background());
    lookAndFeel.setColour(juce::TabbedComponent::outlineColourId, border());
}
} // namespace ats_ui

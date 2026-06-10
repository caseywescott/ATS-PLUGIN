#pragma once

#include <JuceHeader.h>

namespace ats_ui
{
inline void setTooltipIfSupported(juce::Component& component, const juce::String& hint)
{
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(&component))
        client->setTooltip(hint);
}

/** Accessibility help text plus hover tooltip for a control. */
inline void applyControlHint(juce::Component& component, const juce::String& hint)
{
    component.setHelpText(hint);
    setTooltipIfSupported(component, hint);
}

/** Same as applyControlHint, but also forwards the tooltip to slider text-box children. */
inline void applySliderHint(juce::Slider& slider, const juce::String& hint)
{
    slider.setHelpText(hint);
    slider.setTooltip(hint);

    for (int i = 0; i < slider.getNumChildComponents(); ++i)
        if (auto* child = slider.getChildComponent(i))
            setTooltipIfSupported(*child, hint);
}

} // namespace ats_ui

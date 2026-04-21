#pragma once

#include <JuceHeader.h>

#include "EnvelopeOverlay.h"

class AtsAudioProcessor;

class EnvelopeOverlayPanel : public juce::Component, private juce::Timer
{
public:
    explicit EnvelopeOverlayPanel(AtsAudioProcessor&);
    ~EnvelopeOverlayPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildOverlay();
    void drawCurve(juce::Graphics& g,
                   const ats::EnvelopeOverlayCurve& curve,
                   juce::Rectangle<int> plot,
                   juce::Colour colour,
                   float minDb,
                   float maxDb);

    AtsAudioProcessor& processor;
    ats::EnvelopeOverlayData overlay;
    int lastModelRevision = -1;
    bool hasOverlay = false;
    bool rebuildPending = true;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeOverlayPanel)
};

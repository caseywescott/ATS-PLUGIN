#pragma once

#include <JuceHeader.h>

#include "EnvelopeOverlayPanel.h"
#include "MathExprPanel.h"
#include "PartialEditPanel.h"
#include "SessionTabPanel.h"
#include "SpectralEnvelopeWorkspacePanel.h"
#include "SynthTabPanel.h"

class AtsAudioProcessor;

class AtsAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AtsAudioProcessorEditor(AtsAudioProcessor&);
    ~AtsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    int scaled(int px) const;

private:
    void timerCallback() override;

    AtsAudioProcessor& audioProcessor;

    juce::LookAndFeel_V4 pluginLookAndFeel;

    SessionTabPanel sessionTab;
    SynthTabPanel synthTab;
    PartialEditPanel partialTab;
    EnvelopeOverlayPanel envelopeTab;
    SpectralEnvelopeWorkspacePanel transferTab;
    MathExprPanel mathTab;

    juce::TabbedComponent tabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessorEditor)
};

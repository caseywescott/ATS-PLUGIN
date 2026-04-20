#pragma once

#include <JuceHeader.h>

class AtsAudioProcessor;

/** Global synthesis / time–frequency factors (APVTS, host-automatable). */
class SynthTabPanel : public juce::Component
{
public:
    explicit SynthTabPanel(AtsAudioProcessor&);
    void resized() override;

private:
    AtsAudioProcessor& audioProcessor;

    juce::Label hint;

    juce::Slider freqSlider;
    juce::Slider timeStretchSlider;
    juce::Slider synStartSlider;
    juce::Slider synDurSlider;
    juce::ToggleButton usePhaseButton;
    juce::ToggleButton useAllPartialsButton;
    juce::Label analysisTypeLabel;
    juce::ComboBox analysisTypeBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> synStartAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> synDurAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> usePhaseAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> useAllAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analysisAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthTabPanel)
};

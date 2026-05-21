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

    juce::Label freqLabel;
    juce::Label timeStretchLabel;
    juce::Label synStartLabel;
    juce::Label synDurLabel;

    juce::Slider freqSlider;
    juce::Slider timeStretchSlider;
    juce::Slider synStartSlider;
    juce::Slider synDurSlider;
    juce::ToggleButton usePhaseButton;
    juce::ToggleButton useAllPartialsButton;
    juce::ToggleButton residualOnlyButton;
    juce::Label analysisTypeLabel;
    juce::ComboBox analysisTypeBox;
    juce::Label morphHeaderLabel;
    juce::Label morphSourceALabel;
    juce::ComboBox morphSourceABox;
    juce::Label morphSourceBLabel;
    juce::ComboBox morphSourceBBox;
    juce::Label morphRecipeLabel;
    juce::ComboBox morphRecipeBox;
    juce::Slider morphFreqSlider;
    juce::Slider morphAmpSlider;
    juce::Slider morphNoiseSlider;
    juce::Slider morphOrphanSlider;
    juce::Slider morphTimeSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> synStartAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> synDurAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> usePhaseAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> useAllAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> residualOnlyAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analysisAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphSourceAAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphSourceBAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphRecipeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphFreqAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphAmpAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphNoiseAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphOrphanAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphTimeAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthTabPanel)
};

#pragma once

#include <JuceHeader.h>

class AtsAudioProcessor;

/** Global synthesis / time–frequency factors (APVTS, host-automatable). */
class SynthTabPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit SynthTabPanel(AtsAudioProcessor&);
    void resized() override;

private:
    void timerCallback() override;
    void updateFrequencyShapeUi();

    AtsAudioProcessor& audioProcessor;

    juce::Label hint;

    juce::Label freqLabel;
    juce::Label timeStretchLabel;
    juce::Label synStartLabel;
    juce::Label synDurLabel;
    juce::Label freqShapeSectionLabel;
    juce::Label freqShapeModeLabel;
    juce::Label freqShapeAmountLabel;
    juce::Label freqShapePivotLabel;
    juce::Label classicPreviewModeLabel;
    juce::Label classicPreviewModeHint;

    juce::Slider freqSlider;
    juce::Slider timeStretchSlider;
    juce::Slider synStartSlider;
    juce::Slider synDurSlider;
    juce::ToggleButton usePhaseButton;
    juce::ToggleButton useAllPartialsButton;
    juce::ToggleButton residualOnlyButton;
    juce::ComboBox classicPreviewModeBox;
    juce::ComboBox freqShapeModeBox;
    juce::Slider freqShapeAmountSlider;
    juce::Slider freqShapePivotSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> synStartAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> synDurAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> usePhaseAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> useAllAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> residualOnlyAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> classicPreviewModeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> freqShapeModeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqShapeAmountAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqShapePivotAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthTabPanel)
};

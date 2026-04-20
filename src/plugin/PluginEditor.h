#pragma once

#include <JuceHeader.h>

class AtsAudioProcessor;

class AtsAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AtsAudioProcessorEditor(AtsAudioProcessor&);
    ~AtsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshUi();
    void pickFile();

    AtsAudioProcessor& audioProcessor;

    juce::TextButton loadButton { "Import WAV" };
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label statusLabel;
    juce::Label errorLabel;
    double displayProgress = 0.0;
    juce::ProgressBar progressBar { displayProgress };

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Slider partGainSlider;
    juce::Slider resGainSlider;
    juce::Slider outputTrimSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> partGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputTrimAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessorEditor)
};

#pragma once

#include <JuceHeader.h>

#include "SpectrogramPanel.h"
#include "UiConstants.h"

class AtsAudioProcessor;

/** Main session tab: import, analyze, presets, status, core knobs, spectrogram. */
class SessionTabPanel : public juce::Component
{
public:
    explicit SessionTabPanel(AtsAudioProcessor&);
    void resized() override;

    void refreshFromProcessor();

private:
    void pickFile();
    void rebuildPresetList();
    void onPresetComboChanged();
    void saveUserPreset();

    AtsAudioProcessor& audioProcessor;

    juce::TextButton loadButton { "Import WAV" };
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextButton cancelButton { "Cancel" };
    juce::ComboBox presetCombo;
    juce::TextButton savePresetButton { "Save preset" };
    juce::Label presetLabel;
    juce::Label statusLabel;
    juce::Label errorLabel;
    juce::Label cpuWarningLabel;
    double displayProgress = 0.0;
    juce::ProgressBar progressBar { displayProgress };

    SpectrogramPanel spectroPanel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> presetSaveChooser;

    juce::Array<juce::File> userPresetFiles;

    juce::Slider partGainSlider;
    juce::Slider resGainSlider;
    juce::Slider outputTrimSlider;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> partGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputTrimAttach;

    static constexpr int kUserPresetComboIdBase = 1000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionTabPanel)
};

#pragma once

#include <JuceHeader.h>

#include "SpectrogramPanel.h"

class AtsAudioProcessor;

namespace ats_editor
{
/** Documented soft cap for partials × frames (see BUILDING.md). */
constexpr int64_t kModelSizeSoftCap = 2'000'000;
} // namespace ats_editor

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
    void rebuildPresetList();
    void onPresetComboChanged();
    void saveUserPreset();

    int scaled(int px) const;

    AtsAudioProcessor& audioProcessor;

    juce::LookAndFeel_V4 pluginLookAndFeel;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessorEditor)
};

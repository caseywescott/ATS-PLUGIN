#include "PluginEditor.h"

#include "PluginProcessor.h"

AtsAudioProcessorEditor::AtsAudioProcessorEditor(AtsAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(520, 360);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    addAndMakeVisible(statusLabel);

    errorLabel.setColour(juce::Label::textColourId, juce::Colours::tomato);
    errorLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(errorLabel);

    addAndMakeVisible(progressBar);

    loadButton.onClick = [this] { pickFile(); };
    addAndMakeVisible(loadButton);

    analyzeButton.onClick = [this] { audioProcessor.startAnalysis(); };
    addAndMakeVisible(analyzeButton);

    cancelButton.onClick = [this] { audioProcessor.cancelJobs(); };
    addAndMakeVisible(cancelButton);

    auto setupSlider = [](juce::Slider& s, const juce::String& name) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s.setName(name);
    };
    setupSlider(partGainSlider, "Partials");
    setupSlider(resGainSlider, "Residual");
    setupSlider(outputTrimSlider, "Trim");
    addAndMakeVisible(partGainSlider);
    addAndMakeVisible(resGainSlider);
    addAndMakeVisible(outputTrimSlider);

    partGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "partGain", partGainSlider);
    resGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "resGain", resGainSlider);
    outputTrimAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "outputTrim", outputTrimSlider);

    startTimerHz(10);
    refreshUi();
}

AtsAudioProcessorEditor::~AtsAudioProcessorEditor() { stopTimer(); }

void AtsAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AtsAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    auto top = r.removeFromTop(28);
    loadButton.setBounds(top.removeFromLeft(110));
    top.removeFromLeft(8);
    analyzeButton.setBounds(top.removeFromLeft(90));
    top.removeFromLeft(8);
    cancelButton.setBounds(top.removeFromLeft(90));

    r.removeFromTop(10);
    statusLabel.setBounds(r.removeFromTop(24));
    r.removeFromTop(6);
    errorLabel.setBounds(r.removeFromTop(44));
    r.removeFromTop(8);
    progressBar.setBounds(r.removeFromTop(14));
    r.removeFromTop(16);

    auto knobRow = r.removeFromTop(120);
    const int w = knobRow.getWidth() / 3;
    partGainSlider.setBounds(knobRow.removeFromLeft(w).reduced(6));
    resGainSlider.setBounds(knobRow.removeFromLeft(w).reduced(6));
    outputTrimSlider.setBounds(knobRow.removeFromLeft(w).reduced(6));
}

void AtsAudioProcessorEditor::timerCallback() { refreshUi(); }

void AtsAudioProcessorEditor::refreshUi()
{
    displayProgress = (double) audioProcessor.getAnalysisProgress();

    switch (audioProcessor.getUiState())
    {
    case AtsUiState::NoSource:
        statusLabel.setText("State: No source", juce::dontSendNotification);
        break;
    case AtsUiState::ReadyToAnalyze:
        statusLabel.setText("State: Ready to analyze", juce::dontSendNotification);
        break;
    case AtsUiState::Analyzing:
        statusLabel.setText("State: Analyzing (CPU heavy)", juce::dontSendNotification);
        break;
    case AtsUiState::Analyzed:
        statusLabel.setText("State: Analyzed / playing resynth buffer", juce::dontSendNotification);
        break;
    case AtsUiState::Resynthesizing:
        statusLabel.setText("State: Resynthesizing", juce::dontSendNotification);
        break;
    case AtsUiState::Error:
        statusLabel.setText("State: Error", juce::dontSendNotification);
        break;
    }

    const auto err = audioProcessor.getLastError();
    errorLabel.setText(err.isEmpty() ? juce::String() : ("Last error: " + err),
                       juce::dontSendNotification);
}

void AtsAudioProcessorEditor::pickFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select mono/stereo WAV", juce::File{}, "*.wav");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            audioProcessor.loadSourceFile(f);
    });
}

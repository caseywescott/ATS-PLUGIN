#include "SynthTabPanel.h"

#include "PluginProcessor.h"

SynthTabPanel::SynthTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    hint.setText("These parameters mirror SPARAMS-style global synthesis (transpose, time window, phase). "
                 "Analysis type is read when you press Analyze on the Session tab.",
                 juce::dontSendNotification);
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setFont(juce::Font(13.0f));
    addAndMakeVisible(hint);

    auto setup = [](juce::Slider& s, const juce::String& name) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
        s.setName(name);
    };
    setup(freqSlider, "Freq factor");
    setup(timeStretchSlider, "Time factor");
    setup(synStartSlider, "Synth start (s)");
    setup(synDurSlider, "Synth duration (s)");

    addAndMakeVisible(freqSlider);
    addAndMakeVisible(timeStretchSlider);
    addAndMakeVisible(synStartSlider);
    addAndMakeVisible(synDurSlider);

    usePhaseButton.setButtonText("Use partial phases");
    useAllPartialsButton.setButtonText("Use all partials");
    addAndMakeVisible(usePhaseButton);
    addAndMakeVisible(useAllPartialsButton);

    analysisTypeLabel.setText("Analysis type (next Analyze)", juce::dontSendNotification);
    analysisTypeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(analysisTypeLabel);

    analysisTypeBox.addItemList(juce::StringArray { "1 amp+freq", "2 +phase", "3 +noise", "4 full" }, 1);
    addAndMakeVisible(analysisTypeBox);

    freqAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "freq", freqSlider);
    timeAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "timeStretch", timeStretchSlider);
    synStartAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "synStart", synStartSlider);
    synDurAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "synDur", synDurSlider);
    usePhaseAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "usePhase", usePhaseButton);
    useAllAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "useAllPartials", useAllPartialsButton);
    analysisAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "analysisType", analysisTypeBox);
}

void SynthTabPanel::resized()
{
    auto r = getLocalBounds().reduced(12);
    hint.setBounds(r.removeFromTop(56));
    r.removeFromTop(8);

    auto row1 = r.removeFromTop(120);
    const int w = juce::jmax(1, row1.getWidth() / 4);
    freqSlider.setBounds(row1.removeFromLeft(w).reduced(4));
    timeStretchSlider.setBounds(row1.removeFromLeft(w).reduced(4));
    synStartSlider.setBounds(row1.removeFromLeft(w).reduced(4));
    synDurSlider.setBounds(row1.removeFromLeft(w).reduced(4));

    r.removeFromTop(10);
    auto row2 = r.removeFromTop(28);
    usePhaseButton.setBounds(row2.removeFromLeft(180));
    row2.removeFromLeft(12);
    useAllPartialsButton.setBounds(row2.removeFromLeft(200));
    row2.removeFromLeft(16);
    analysisTypeLabel.setBounds(row2.removeFromLeft(200));
    analysisTypeBox.setBounds(row2.removeFromLeft(juce::jmin(220, row2.getWidth())));
}

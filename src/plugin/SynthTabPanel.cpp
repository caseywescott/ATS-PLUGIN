#include "SynthTabPanel.h"

#include "PluginProcessor.h"

SynthTabPanel::SynthTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    hint.setText("These parameters mirror SPARAMS-style global synthesis (transpose, time window, phase). "
                 "Choose the analysis family on the Session tab; analysis type here is read the next time you press Analyze.",
                 juce::dontSendNotification);
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setFont(juce::Font(13.0f));
    addAndMakeVisible(hint);

    auto setupLab = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(11.0f, juce::Font::bold));
    };
    setupLab(freqLabel, "freq - Frequency factor");
    setupLab(timeStretchLabel, "timeStretch - Time factor");
    setupLab(synStartLabel, "synStart - Synth start (s)");
    setupLab(synDurLabel, "synDur - Duration (s, 0=full)");
    addAndMakeVisible(freqLabel);
    addAndMakeVisible(timeStretchLabel);
    addAndMakeVisible(synStartLabel);
    addAndMakeVisible(synDurLabel);

    const juce::Colour knobLabCol(0xfff0f0f5);
    freqLabel.setColour(juce::Label::textColourId, knobLabCol);
    timeStretchLabel.setColour(juce::Label::textColourId, knobLabCol);
    synStartLabel.setColour(juce::Label::textColourId, knobLabCol);
    synDurLabel.setColour(juce::Label::textColourId, knobLabCol);

    auto setup = [](juce::Slider& s, const juce::String& name) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
        s.setName(name);
    };
    setup(freqSlider, "freq");
    setup(timeStretchSlider, "timeStretch");
    setup(synStartSlider, "synStart");
    setup(synDurSlider, "synDur");
    freqSlider.setHelpText("Frequency factor (SPARAMS frec), before MIDI transpose.");
    timeStretchSlider.setHelpText("Time stretch factor (SPARAMS max_stretch).");
    synStartSlider.setHelpText("Synthesis window start in seconds (SPARAMS beg).");
    synDurSlider.setHelpText("Synthesis duration in seconds, 0 = full sound (SPARAMS end).");

    addAndMakeVisible(freqSlider);
    addAndMakeVisible(timeStretchSlider);
    addAndMakeVisible(synStartSlider);
    addAndMakeVisible(synDurSlider);

    usePhaseButton.setButtonText("Use partial phases");
    useAllPartialsButton.setButtonText("Use all partials");
    residualOnlyButton.setButtonText("Residual only (mute partials)");
    residualOnlyButton.setHelpText("Synthesize only the noise / residual path (SPARAMS amp=0). "
                                   "Requires analysis type 3+ with residual data.");
    addAndMakeVisible(usePhaseButton);
    addAndMakeVisible(useAllPartialsButton);
    addAndMakeVisible(residualOnlyButton);

    analysisTypeLabel.setText("Analysis type (next Analyze)", juce::dontSendNotification);
    analysisTypeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(analysisTypeLabel);

    analysisTypeBox.addItemList(juce::StringArray { "1 amp+freq", "2 +phase", "3 +noise", "4 full" }, 1);
    addAndMakeVisible(analysisTypeBox);
    morphHeaderLabel.setText("A/B Morph workspace", juce::dontSendNotification);
    addAndMakeVisible(morphHeaderLabel);
    morphSourceALabel.setText("Source A", juce::dontSendNotification);
    morphSourceBLabel.setText("Source B", juce::dontSendNotification);
    morphRecipeLabel.setText("Recipe", juce::dontSendNotification);
    morphSourceABox.addItemList(juce::StringArray { "Current modern model", "Previous modern model", "Disabled" }, 1);
    morphSourceBBox.addItemList(juce::StringArray { "Current modern model", "Previous modern model", "Disabled" }, 1);
    morphRecipeBox.addItemList(juce::StringArray { "Linear blend", "Harmonic lock", "Percussive protect", "Texture drift" }, 1);
    addAndMakeVisible(morphSourceALabel);
    addAndMakeVisible(morphSourceBLabel);
    addAndMakeVisible(morphRecipeLabel);
    addAndMakeVisible(morphSourceABox);
    addAndMakeVisible(morphSourceBBox);
    addAndMakeVisible(morphRecipeBox);

    auto setupMorph = [](juce::Slider& s, const juce::String& nm) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
        s.setName(nm);
    };
    setupMorph(morphFreqSlider, "Morph frequency envelope");
    setupMorph(morphAmpSlider, "Morph amplitude envelope");
    setupMorph(morphNoiseSlider, "Morph partial-noise envelope");
    setupMorph(morphOrphanSlider, "Morph orphan-residual envelope");
    setupMorph(morphTimeSlider, "Morph time envelope");
    addAndMakeVisible(morphFreqSlider);
    addAndMakeVisible(morphAmpSlider);
    addAndMakeVisible(morphNoiseSlider);
    addAndMakeVisible(morphOrphanSlider);
    addAndMakeVisible(morphTimeSlider);

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
    residualOnlyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "residualOnly", residualOnlyButton);
    analysisAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "analysisType", analysisTypeBox);
    morphSourceAAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "morphSourceA", morphSourceABox);
    morphSourceBAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "morphSourceB", morphSourceBBox);
    morphRecipeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "morphRecipe", morphRecipeBox);
    morphFreqAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "morphFreqEnv", morphFreqSlider);
    morphAmpAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "morphAmpEnv", morphAmpSlider);
    morphNoiseAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "morphNoiseEnv", morphNoiseSlider);
    morphOrphanAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "morphOrphanEnv", morphOrphanSlider);
    morphTimeAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "morphTimeEnv", morphTimeSlider);
}

void SynthTabPanel::resized()
{
    auto r = getLocalBounds().reduced(12);
    hint.setBounds(r.removeFromTop(56));
    r.removeFromTop(8);

    constexpr int labelH = 34;
    constexpr int knobBlockH = 104;
    auto row1 = r.removeFromTop(labelH + knobBlockH + 4);
    const int w = juce::jmax(1, row1.getWidth() / 4);

    auto placeKnobColumn = [&](juce::Label& lab, juce::Slider& sld) {
        auto col = row1.removeFromLeft(w).reduced(4);
        lab.setBounds(col.removeFromTop(labelH));
        sld.setBounds(col);
    };
    placeKnobColumn(freqLabel, freqSlider);
    placeKnobColumn(timeStretchLabel, timeStretchSlider);
    placeKnobColumn(synStartLabel, synStartSlider);
    placeKnobColumn(synDurLabel, synDurSlider);

    r.removeFromTop(10);
    auto row2 = r.removeFromTop(28);
    usePhaseButton.setBounds(row2.removeFromLeft(180));
    row2.removeFromLeft(12);
    useAllPartialsButton.setBounds(row2.removeFromLeft(200));
    row2.removeFromLeft(12);
    residualOnlyButton.setBounds(row2.removeFromLeft(240));
    row2.removeFromLeft(16);
    analysisTypeLabel.setBounds(row2.removeFromLeft(200));
    analysisTypeBox.setBounds(row2.removeFromLeft(180));
    r.removeFromTop(12);
    morphHeaderLabel.setBounds(r.removeFromTop(24));
    auto srcRow = r.removeFromTop(24);
    morphSourceALabel.setBounds(srcRow.removeFromLeft(64));
    morphSourceABox.setBounds(srcRow.removeFromLeft(180));
    srcRow.removeFromLeft(10);
    morphSourceBLabel.setBounds(srcRow.removeFromLeft(64));
    morphSourceBBox.setBounds(srcRow.removeFromLeft(180));
    srcRow.removeFromLeft(10);
    morphRecipeLabel.setBounds(srcRow.removeFromLeft(56));
    morphRecipeBox.setBounds(srcRow.removeFromLeft(180));
    r.removeFromTop(8);
    morphFreqSlider.setBounds(r.removeFromTop(24));
    morphAmpSlider.setBounds(r.removeFromTop(24));
    morphNoiseSlider.setBounds(r.removeFromTop(24));
    morphOrphanSlider.setBounds(r.removeFromTop(24));
    morphTimeSlider.setBounds(r.removeFromTop(24));
}

#include "SynthTabPanel.h"

#include "AtsFrequencyShape.h"
#include "PluginProcessor.h"

SynthTabPanel::SynthTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    hint.setText("These parameters mirror SPARAMS-style global synthesis (transpose, time window, phase). "
                 "Frequency shaping warps partial spacing at playback time; the trace display updates immediately. "
                 "Use the Analysis tab to tune the next analyze pass.",
                 juce::dontSendNotification);
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setFont(juce::Font(13.0f));
    addAndMakeVisible(hint);

    auto setupLab = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(11.0f, juce::Font::bold));
        l.setMinimumHorizontalScale(0.65f);
    };
    setupLab(freqLabel, "freq - Frequency factor");
    setupLab(timeStretchLabel, "timeStretch - Time factor");
    setupLab(synStartLabel, "synStart - Synth start (s)");
    setupLab(synDurLabel, "synDur - Duration (s, 0=full)");
    setupLab(freqShapeSectionLabel, "Frequency shaping");
    setupLab(freqShapeModeLabel, "Mode");
    setupLab(freqShapeAmountLabel, "Amount");
    setupLab(freqShapePivotLabel, "Pivot (Hz)");
    setupLab(classicPreviewModeLabel, "Both playback");
    addAndMakeVisible(freqLabel);
    addAndMakeVisible(timeStretchLabel);
    addAndMakeVisible(synStartLabel);
    addAndMakeVisible(synDurLabel);
    addAndMakeVisible(freqShapeSectionLabel);
    addAndMakeVisible(freqShapeModeLabel);
    addAndMakeVisible(freqShapeAmountLabel);
    addAndMakeVisible(freqShapePivotLabel);
    addAndMakeVisible(classicPreviewModeLabel);

    const juce::Colour knobLabCol(0xfff0f0f5);
    freqLabel.setColour(juce::Label::textColourId, knobLabCol);
    timeStretchLabel.setColour(juce::Label::textColourId, knobLabCol);
    synStartLabel.setColour(juce::Label::textColourId, knobLabCol);
    synDurLabel.setColour(juce::Label::textColourId, knobLabCol);
    freqShapeSectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xffbaffea));
    freqShapeModeLabel.setColour(juce::Label::textColourId, knobLabCol);
    freqShapeAmountLabel.setColour(juce::Label::textColourId, knobLabCol);
    freqShapePivotLabel.setColour(juce::Label::textColourId, knobLabCol);
    classicPreviewModeLabel.setColour(juce::Label::textColourId, knobLabCol);

    auto setup = [](juce::Slider& s, const juce::String& name) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 18);
        s.setName(name);
    };
    setup(freqSlider, "freq");
    setup(timeStretchSlider, "timeStretch");
    setup(synStartSlider, "synStart");
    setup(synDurSlider, "synDur");

    freqShapeAmountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    freqShapeAmountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    freqShapeAmountSlider.setName("Frequency shaping amount");
    freqShapePivotSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    freqShapePivotSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    freqShapePivotSlider.setName("Frequency shaping pivot");
    freqSlider.setHelpText("Frequency factor (SPARAMS frec), before MIDI transpose.");
    timeStretchSlider.setHelpText("Time stretch factor (SPARAMS max_stretch).");
    synStartSlider.setHelpText("Synthesis window start in seconds (SPARAMS beg).");
    synDurSlider.setHelpText("Synthesis duration in seconds, 0 = full sound (SPARAMS end).");
    freqShapePivotSlider.setHelpText("Frequency in Hz about which warping is applied. 0 = auto.");
    freqShapePivotSlider.setTextValueSuffix(" Hz");
    freqShapePivotSlider.textFromValueFunction = [](double value) {
        return value <= 0.005 ? juce::String("Auto") : juce::String(value, 0) + " Hz";
    };
    freqShapePivotSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.trim().startsWithIgnoreCase("auto") ? 0.0 : text.getDoubleValue();
    };

    addAndMakeVisible(freqSlider);
    addAndMakeVisible(timeStretchSlider);
    addAndMakeVisible(synStartSlider);
    addAndMakeVisible(synDurSlider);
    addAndMakeVisible(freqShapeAmountSlider);
    addAndMakeVisible(freqShapePivotSlider);

    freqShapeModeBox.addItemList(juce::StringArray { "Linear", "Stretch", "Compress", "Harmonic" }, 1);
    freqShapeModeBox.setHelpText("Choose the frequency shaping model applied before the main freq transpose.");
    freqShapeModeBox.onChange = [this] { updateFrequencyShapeUi(); };
    addAndMakeVisible(freqShapeModeBox);

    usePhaseButton.setButtonText("Use partial phases");
    useAllPartialsButton.setButtonText("Use all partials");
    useIfftSynthButton.setButtonText("Use IFFT synthesis");
    useIfftSynthButton.setHelpText("Overlap-add inverse-FFT resynthesis (faster on dense models). "
                                   "Default off preserves the oscillator bank path.");
    residualOnlyButton.setButtonText("Residual only (mute partials)");
    residualOnlyButton.setHelpText("Synthesize only the noise / residual path (SPARAMS amp=0). "
                                   "Requires analysis type 3+ with residual data.");
    addAndMakeVisible(usePhaseButton);
    addAndMakeVisible(useAllPartialsButton);
    addAndMakeVisible(useIfftSynthButton);
    addAndMakeVisible(residualOnlyButton);

    classicPreviewModeBox.addItemList(juce::StringArray { "Classic combined", "Layer mix" }, 1);
    classicPreviewModeBox.setHelpText("Classic combined uses ATSA's direct partial+noise path for Both playback. "
                                      "Layer mix sums the separate partial and residual previews so the gain sliders remain independent.");
    classicPreviewModeBox.setTooltip("Classic combined uses ATSA's direct partial+noise path for Both playback. "
                                     "Layer mix sums separate partial and residual layers with independent gains.");
    addAndMakeVisible(classicPreviewModeBox);

    classicPreviewModeHint.setText("Classic combined bypasses Partials/Residual gain for Both playback; Layer mix keeps those gains active.",
                                   juce::dontSendNotification);
    classicPreviewModeHint.setJustificationType(juce::Justification::centredLeft);
    classicPreviewModeHint.setMinimumHorizontalScale(0.7f);
    classicPreviewModeHint.setFont(juce::Font(11.0f));
    classicPreviewModeHint.setColour(juce::Label::textColourId, juce::Colour(0xffb8c7d9));
    classicPreviewModeHint.setHelpText(classicPreviewModeHint.getText());
    classicPreviewModeHint.setTooltip(classicPreviewModeHint.getText());
    addAndMakeVisible(classicPreviewModeHint);

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
    useIfftSynthAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, ats_param::kUseIfftSynth, useIfftSynthButton);
    residualOnlyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "residualOnly", residualOnlyButton);
    classicPreviewModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kClassicPreviewMode, classicPreviewModeBox);
    freqShapeModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kFreqShapeMode, freqShapeModeBox);
    freqShapeAmountAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, ats_param::kFreqShapeAmount, freqShapeAmountSlider);
    freqShapePivotAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, ats_param::kFreqShapePivotHz, freqShapePivotSlider);

    updateFrequencyShapeUi();
    startTimerHz(4);
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
    auto shapeRow = r.removeFromTop(130);
    freqShapeSectionLabel.setBounds(shapeRow.removeFromTop(20));
    auto shapeControls = shapeRow;
    auto modeCol = shapeControls.removeFromLeft(180).reduced(4);
    freqShapeModeLabel.setBounds(modeCol.removeFromTop(28));
    freqShapeModeBox.setBounds(modeCol.removeFromTop(28));
    shapeControls.removeFromLeft(10);

    const int sliderColW = juce::jmax(160, shapeControls.getWidth() / 2 - 10);
    auto amountCol = shapeControls.removeFromLeft(sliderColW).reduced(4);
    freqShapeAmountLabel.setBounds(amountCol.removeFromTop(24));
    freqShapeAmountSlider.setBounds(amountCol.removeFromTop(36));
    shapeControls.removeFromLeft(10);

    auto pivotCol = shapeControls.removeFromLeft(sliderColW).reduced(4);
    freqShapePivotLabel.setBounds(pivotCol.removeFromTop(24));
    freqShapePivotSlider.setBounds(pivotCol.removeFromTop(36));

    r.removeFromTop(10);
    auto row3 = r.removeFromTop(28);
    usePhaseButton.setBounds(row3.removeFromLeft(180));
    row3.removeFromLeft(12);
    useAllPartialsButton.setBounds(row3.removeFromLeft(200));
    row3.removeFromLeft(12);
    useIfftSynthButton.setBounds(row3.removeFromLeft(200));
    r.removeFromTop(6);
    auto row3b = r.removeFromTop(28);
    residualOnlyButton.setBounds(row3b.removeFromLeft(240));

    r.removeFromTop(10);
    auto row4 = r.removeFromTop(30);
    classicPreviewModeLabel.setBounds(row4.removeFromLeft(128));
    row4.removeFromLeft(8);
    classicPreviewModeBox.setBounds(row4.removeFromLeft(260));
    row4.removeFromLeft(10);
    classicPreviewModeHint.setBounds(row4);
}

void SynthTabPanel::timerCallback()
{
    const uint64_t rev = audioProcessor.getDisplayShapeRevision();
    if (rev != lastDisplayShapeRevision)
    {
        lastDisplayShapeRevision = rev;
        updateFrequencyShapeUi();
    }
}

void SynthTabPanel::updateFrequencyShapeUi()
{
    const auto shape = audioProcessor.getCurrentFrequencyShapeSettings();
    const bool linear = shape.mode == ats::FrequencyShapeMode::Linear;

    juce::String amountLabel = "Amount";
    juce::String amountHelp;
    switch (shape.mode)
    {
        case ats::FrequencyShapeMode::Stretch:
            amountLabel = "Spacing expand";
            amountHelp = "1 = no warp; >1 spreads partials in log space around pivot.";
            break;
        case ats::FrequencyShapeMode::Compress:
            amountLabel = "Spacing tighten";
            amountHelp = "<1 pulls partials together in log space around pivot.";
            break;
        case ats::FrequencyShapeMode::Harmonic:
            amountLabel = "Harmonic ratio";
            amountHelp = "1 = no warp; >1 exaggerates harmonic spacing; <1 collapses.";
            break;
        case ats::FrequencyShapeMode::Linear:
        default:
            amountHelp = "Linear mode uses only the freq factor; shaping amount is ignored.";
            break;
    }

    freqShapeAmountLabel.setText(amountLabel, juce::dontSendNotification);
    freqShapeAmountSlider.setHelpText(amountHelp);
    freqShapeAmountSlider.setTooltip(amountHelp);

    juce::String pivotLabel = shape.mode == ats::FrequencyShapeMode::Harmonic ? "F0 (Hz)" : "Pivot (Hz)";
    if (!linear && shape.pivotHz <= 0.0 && shape.resolvedPivotHz > 0.0)
        pivotLabel << " Auto -> " << juce::String(shape.resolvedPivotHz, 0) << " Hz";
    freqShapePivotLabel.setText(pivotLabel, juce::dontSendNotification);

    const juce::String pivotHelp = "Frequency in Hz about which warping is applied. 0 = auto.";
    freqShapePivotSlider.setHelpText(pivotHelp);
    freqShapePivotSlider.setTooltip(pivotHelp);

    if (linear != lastLinearState)
    {
        lastLinearState = linear;
        freqShapeAmountSlider.setEnabled(!linear);
        freqShapePivotSlider.setEnabled(!linear);
        const float alpha = linear ? 0.45f : 1.0f;
        freqShapeAmountLabel.setAlpha(alpha);
        freqShapePivotLabel.setAlpha(alpha);
        freqShapeAmountSlider.setAlpha(alpha);
        freqShapePivotSlider.setAlpha(alpha);
    }

    const juce::String modeHelp = "Frequency shaping mode: Linear leaves partial spacing unchanged; "
                                  "Stretch and Compress use a log-pivot warp; Harmonic uses approximate partial rank.";
    freqShapeModeBox.setHelpText(modeHelp);
    freqShapeModeBox.setTooltip(modeHelp);
}

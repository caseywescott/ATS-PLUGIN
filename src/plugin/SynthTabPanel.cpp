#include "SynthTabPanel.h"

#include "AtsFrequencyShape.h"
#include "PluginProcessor.h"
#include "UiConstants.h"

#include <array>

SynthTabPanel::SynthTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    hint.setText("These parameters mirror SPARAMS-style global synthesis (transpose, time window, phase). "
                 "Frequency shaping warps partial spacing at playback time; the trace display updates immediately. "
                 "Use the Analysis tab to tune the next analyze pass.",
                 juce::dontSendNotification);
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setFont(ats_ui::font::ui(12.5f));
    hint.setColour(juce::Label::textColourId, ats_ui::colour::textSecondary());
    hint.setMinimumHorizontalScale(0.72f);
    addAndMakeVisible(hint);

    auto setupSection = [](juce::Label& l, const juce::String& text) {
        l.setText(text.toUpperCase(), juce::dontSendNotification);
        l.setJustificationType(juce::Justification::bottomLeft);
        l.setFont(ats_ui::font::section());
        l.setColour(juce::Label::textColourId, ats_ui::colour::textMuted());
    };
    setupSection(pitchSectionLabel, "Pitch");
    setupSection(timeSectionLabel, "Time");
    setupSection(renderSectionLabel, "Render");
    addAndMakeVisible(pitchSectionLabel);
    addAndMakeVisible(timeSectionLabel);
    addAndMakeVisible(renderSectionLabel);

    auto setupLab = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredLeft);
        l.setFont(ats_ui::font::ui(12.0f));
        l.setMinimumHorizontalScale(0.7f);
    };
    setupLab(freqLabel, "Freq factor");
    setupLab(timeStretchLabel, "Time factor");
    setupLab(synStartLabel, "Synth start");
    setupLab(synDurLabel, "Duration");
    setupLab(freqShapeModeLabel, "Shape");
    setupLab(freqShapeAmountLabel, "Amount");
    setupLab(freqShapePivotLabel, "Pivot");
    setupLab(classicPreviewModeLabel, "Both playback");
    addAndMakeVisible(freqLabel);
    addAndMakeVisible(timeStretchLabel);
    addAndMakeVisible(synStartLabel);
    addAndMakeVisible(synDurLabel);
    addAndMakeVisible(freqShapeModeLabel);
    addAndMakeVisible(freqShapeAmountLabel);
    addAndMakeVisible(freqShapePivotLabel);
    addAndMakeVisible(classicPreviewModeLabel);

    const auto knobLabCol = ats_ui::colour::textSecondary();
    freqLabel.setColour(juce::Label::textColourId, knobLabCol);
    timeStretchLabel.setColour(juce::Label::textColourId, knobLabCol);
    synStartLabel.setColour(juce::Label::textColourId, knobLabCol);
    synDurLabel.setColour(juce::Label::textColourId, knobLabCol);
    freqShapeModeLabel.setColour(juce::Label::textColourId, knobLabCol);
    freqShapeAmountLabel.setColour(juce::Label::textColourId, knobLabCol);
    freqShapePivotLabel.setColour(juce::Label::textColourId, knobLabCol);
    classicPreviewModeLabel.setColour(juce::Label::textColourId, knobLabCol);

    freqShapeSectionLabel.setVisible(false);   // superseded by pitch/time headings

    auto setup = [](juce::Slider& s, const juce::String& name) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
        s.setName(name);
    };
    setup(freqSlider, "freq");
    setup(timeStretchSlider, "timeStretch");
    setup(synStartSlider, "synStart");
    setup(synDurSlider, "synDur");

    synDurSlider.textFromValueFunction = [](double value) {
        return value <= 0.0005 ? juce::String("FULL") : juce::String(value, 2) + " s";
    };
    synDurSlider.valueFromTextFunction = [](const juce::String& text) {
        return text.trim().startsWithIgnoreCase("full") ? 0.0 : text.getDoubleValue();
    };

    freqShapeAmountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    freqShapeAmountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
    freqShapeAmountSlider.setName("Frequency shaping amount");
    freqShapePivotSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    freqShapePivotSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
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
    classicPreviewModeHint.setColour(juce::Label::textColourId, ats_ui::colour::textMuted());
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

void SynthTabPanel::paint(juce::Graphics&)
{
    // Controls sit directly on the window background (spec §10.6); section
    // headings are Labels, so nothing to paint here.
}

void SynthTabPanel::resized()
{
    // A compact control row: caption on the left, control filling the rest.
    auto placeRow = [](juce::Rectangle<int>& col, juce::Label& cap, juce::Component& ctl,
                       int capW = 96) {
        auto row = col.removeFromTop(ats_ui::kCompactRowH);
        cap.setBounds(row.removeFromLeft(capW));
        ctl.setBounds(row.reduced(0, 6));
        col.removeFromTop(4);
    };
    auto placeHeading = [](juce::Rectangle<int>& col, juce::Label& h) {
        col.removeFromTop(ats_ui::kSectionTopPad);
        h.setBounds(col.removeFromTop(ats_ui::kSectionLabelH + 2));
    };

    auto r = getLocalBounds().reduced(16);
    const bool narrow = r.getWidth() < 620;

    hint.setBounds(r.removeFromTop(narrow ? 68 : 52));
    r.removeFromTop(8);

    const int gutter = 20;
    auto left = r.removeFromLeft((r.getWidth() - gutter) / 2);
    r.removeFromLeft(gutter);
    auto right = r;

    // ---- Left column: PITCH + TIME ----
    placeHeading(left, pitchSectionLabel);
    placeRow(left, freqLabel, freqSlider);

    {
        auto row = left.removeFromTop(ats_ui::kCompactRowH);
        freqShapeModeLabel.setBounds(row.removeFromLeft(96));
        freqShapeModeBox.setBounds(row.reduced(0, 6));
        left.removeFromTop(4);
    }
    placeRow(left, freqShapeAmountLabel, freqShapeAmountSlider);
    placeRow(left, freqShapePivotLabel, freqShapePivotSlider);

    placeHeading(left, timeSectionLabel);
    placeRow(left, timeStretchLabel, timeStretchSlider);
    placeRow(left, synStartLabel, synStartSlider);
    placeRow(left, synDurLabel, synDurSlider);

    // ---- Right column: RENDER ----
    placeHeading(right, renderSectionLabel);
    usePhaseButton.setBounds(right.removeFromTop(26));
    right.removeFromTop(4);
    useAllPartialsButton.setBounds(right.removeFromTop(26));
    right.removeFromTop(4);
    residualOnlyButton.setBounds(right.removeFromTop(26));
    right.removeFromTop(4);
    useIfftSynthButton.setBounds(right.removeFromTop(26));
    right.removeFromTop(8);

    {
        auto row = right.removeFromTop(ats_ui::kCompactRowH);
        classicPreviewModeLabel.setBounds(row.removeFromLeft(110));
        classicPreviewModeBox.setBounds(row.reduced(0, 6));
        right.removeFromTop(6);
    }
    classicPreviewModeHint.setBounds(right.removeFromTop(juce::jmin(48, right.getHeight())));
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

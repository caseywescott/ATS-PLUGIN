#include "SpectralEnvelopeWorkspacePanel.h"

#include "PluginProcessor.h"

namespace
{

static void setupKnobLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(11.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, juce::Colour(0xfff0f0f5));
}

static void setupKnob(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
}

static void setupSectionLabel(juce::Label& l, const juce::String& text)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centredLeft);
    l.setFont(juce::Font(12.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, juce::Colour(0xff9090c0));
}

static void setupValueLabel(juce::Label& l)
{
    l.setText("(none)", juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centredLeft);
    l.setFont(juce::Font(11.0f));
    l.setColour(juce::Label::textColourId, juce::Colour(0xffb0c8ff));
}

} // namespace

SpectralEnvelopeWorkspacePanel::SpectralEnvelopeWorkspacePanel(AtsAudioProcessor& p)
    : processor(p)
{
    // Model slot labels
    setupSectionLabel(sourceSlotLabel, "Source model (de-color):");
    setupSectionLabel(targetSlotLabel, "Target model (re-color):");
    setupValueLabel(sourceIdLabel);
    setupValueLabel(targetIdLabel);

    setSourceButton.setHelpText("Assign the currently analyzed ATS model as the source for envelope de-coloring.");
    setTargetButton.setHelpText("Assign the currently analyzed ATS model as the target for envelope re-coloring.");

    setSourceButton.onClick = [this] {
        // Tag the currently analyzed model as the source using its revision number,
        // which is stable across UI refreshes but changes when a new model is loaded.
        const auto rev = processor.getModelRevision();
        const auto id = juce::String("model-rev-") + juce::String(rev);
        processor.setEnvelopeTransferSourceId(id);
        sourceIdLabel.setText(id, juce::dontSendNotification);
    };

    setTargetButton.onClick = [this] {
        const auto rev = processor.getModelRevision();
        const auto id = juce::String("model-rev-") + juce::String(rev);
        processor.setEnvelopeTransferTargetId(id);
        targetIdLabel.setText(id, juce::dontSendNotification);
    };

    addAndMakeVisible(sourceSlotLabel);
    addAndMakeVisible(sourceIdLabel);
    addAndMakeVisible(setSourceButton);
    addAndMakeVisible(targetSlotLabel);
    addAndMakeVisible(targetIdLabel);
    addAndMakeVisible(setTargetButton);

    // Estimator preset selector
    setupSectionLabel(estimatorPresetLabel, "Estimator preset:");
    estimatorPresetBox.addItemList(
        juce::StringArray {
            "Default (RDC, Bark, order 16)",
            "High-resolution (RDC, ERB, order 24)",
            "Smooth voice/singing (RDC, Mel, order 12)",
            "Inharmonic / metal (RDC, Linear, order 20)",
            "LPC fast (LPC order 16)",
            "True envelope (iterative)",
        },
        1);
    estimatorPresetBox.setSelectedId(1, juce::dontSendNotification);
    estimatorPresetBox.setHelpText(
        "Estimator preset determines the envelope fitting algorithm applied to source and target models.");
    estimatorPresetBox.onChange = [this] {
        processor.setEnvelopeOverlayEstimatorPreset(estimatorPresetBox.getSelectedId());
    };
    addAndMakeVisible(estimatorPresetLabel);
    addAndMakeVisible(estimatorPresetBox);

    // Transfer controls
    setupKnobLabel(decolorLabel,      "De-color");
    setupKnobLabel(recolorLabel,      "Re-color");
    setupKnobLabel(envMixLabel,       "Env mix");
    setupKnobLabel(residualMixLabel,  "Res mix");

    setupKnob(decolorSlider);
    setupKnob(recolorSlider);
    setupKnob(envMixSlider);
    setupKnob(residualMixSlider);

    decolorSlider.setHelpText(
        "Amount of source envelope removed before re-coloring (envDecolor). 1.0 = full de-color, 0.0 = no change.");
    recolorSlider.setHelpText(
        "Amount of target envelope applied after de-coloring (envRecolor). 1.0 = full target color.");
    envMixSlider.setHelpText(
        "Blend between unmodified source and fully transferred envelope (envMix).");
    residualMixSlider.setHelpText(
        "Amount of target-color applied to residual/noise bands (envResidualMix). 0 = residual unchanged.");

    addAndMakeVisible(decolorLabel);
    addAndMakeVisible(recolorLabel);
    addAndMakeVisible(envMixLabel);
    addAndMakeVisible(residualMixLabel);
    addAndMakeVisible(decolorSlider);
    addAndMakeVisible(recolorSlider);
    addAndMakeVisible(envMixSlider);
    addAndMakeVisible(residualMixSlider);

    decolorAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "envDecolor", decolorSlider);
    recolorAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "envRecolor", recolorSlider);
    envMixAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "envMix", envMixSlider);
    residualMixAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "envResidualMix", residualMixSlider);

    // Transient policy combo (APVTS choice)
    setupSectionLabel(transientPolicyLabel, "Transient policy:");
    transientPolicyBox.addItemList(
        juce::StringArray { "Preserve source", "Target color", "Crossfade", "Bypass" }, 1);
    transientPolicyBox.setHelpText(
        "How envelope transfer handles frames marked as transients (envTransientPolicy).");
    addAndMakeVisible(transientPolicyLabel);
    addAndMakeVisible(transientPolicyBox);
    transientAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "envTransientPolicy", transientPolicyBox);

    // A/B comparison readout
    abStatusLabel.setText("A/B: no transfer active", juce::dontSendNotification);
    abStatusLabel.setJustificationType(juce::Justification::centredLeft);
    abStatusLabel.setFont(juce::Font(11.0f));
    abStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff80c090));
    addAndMakeVisible(abStatusLabel);

    startTimerHz(4);
}

SpectralEnvelopeWorkspacePanel::~SpectralEnvelopeWorkspacePanel()
{
    stopTimer();
}

void SpectralEnvelopeWorkspacePanel::timerCallback()
{
    refreshModelSlots();
}

void SpectralEnvelopeWorkspacePanel::refreshModelSlots()
{
    const auto srcId = processor.getEnvelopeTransferSourceId();
    const auto tgtId = processor.getEnvelopeTransferTargetId();

    sourceIdLabel.setText(srcId.isEmpty() ? "(none)" : srcId, juce::dontSendNotification);
    targetIdLabel.setText(tgtId.isEmpty() ? "(none)" : tgtId, juce::dontSendNotification);
    const int presetId = processor.getEnvelopeOverlayEstimatorPreset();
    if (presetId > 0 && estimatorPresetBox.getSelectedId() != presetId)
        estimatorPresetBox.setSelectedId(presetId, juce::dontSendNotification);

    const bool hasBoth = srcId.isNotEmpty() && tgtId.isNotEmpty();
    abStatusLabel.setText(
        hasBoth ? "A/B: source + target assigned — transfer parameters active"
                : "A/B: assign source and target models to activate transfer",
        juce::dontSendNotification);
    abStatusLabel.setColour(
        juce::Label::textColourId,
        hasBoth ? juce::Colour(0xff80c090) : juce::Colour(0xffc09060));
}

void SpectralEnvelopeWorkspacePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff12121a));
}

void SpectralEnvelopeWorkspacePanel::resized()
{
    auto r = getLocalBounds().reduced(12);

    // Model slots
    auto slotRow = [&](juce::Label& sectionLab, juce::Label& idLab, juce::TextButton& btn) {
        auto row = r.removeFromTop(22);
        sectionLab.setBounds(row.removeFromLeft(200));
        idLab.setBounds(row.removeFromLeft(juce::jmax(80, row.getWidth() - 180)));
        btn.setBounds(row.removeFromLeft(juce::jmin(180, row.getWidth())));
        r.removeFromTop(4);
    };
    slotRow(sourceSlotLabel, sourceIdLabel, setSourceButton);
    slotRow(targetSlotLabel, targetIdLabel, setTargetButton);

    r.removeFromTop(8);

    // Estimator preset
    {
        auto row = r.removeFromTop(22);
        estimatorPresetLabel.setBounds(row.removeFromLeft(140));
        estimatorPresetBox.setBounds(row.removeFromLeft(juce::jmin(280, row.getWidth())));
    }
    r.removeFromTop(8);

    // Transfer knobs
    constexpr int labelH = 34;
    constexpr int knobH = 96;
    auto knobRow = r.removeFromTop(labelH + knobH + 4);
    const int w = juce::jmax(1, knobRow.getWidth() / 4);

    auto placeKnob = [&](juce::Label& lab, juce::Slider& sld) {
        auto col = knobRow.removeFromLeft(w).reduced(4);
        lab.setBounds(col.removeFromTop(labelH));
        sld.setBounds(col);
    };
    placeKnob(decolorLabel,     decolorSlider);
    placeKnob(recolorLabel,     recolorSlider);
    placeKnob(envMixLabel,      envMixSlider);
    placeKnob(residualMixLabel, residualMixSlider);

    r.removeFromTop(8);

    // Transient policy
    {
        auto row = r.removeFromTop(22);
        transientPolicyLabel.setBounds(row.removeFromLeft(140));
        transientPolicyBox.setBounds(row.removeFromLeft(juce::jmin(220, row.getWidth())));
    }
    r.removeFromTop(10);

    // A/B status
    abStatusLabel.setBounds(r.removeFromTop(22));
}

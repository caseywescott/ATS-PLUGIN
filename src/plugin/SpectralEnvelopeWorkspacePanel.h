#pragma once

#include <JuceHeader.h>

class AtsAudioProcessor;

/**
 * Spectral Envelope Transfer workspace tab.
 *
 * Shows source/target model slots, estimator preset selector, host-automatable
 * transfer controls (de-color, target-color, envelope mix, residual mix,
 * transient policy), and an A/B comparison readout.  All heavy work runs on the
 * processor; this panel is display-only on the audio thread.
 */
class SpectralEnvelopeWorkspacePanel : public juce::Component, private juce::Timer
{
public:
    explicit SpectralEnvelopeWorkspacePanel(AtsAudioProcessor&);
    ~SpectralEnvelopeWorkspacePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshModelSlots();

    AtsAudioProcessor& processor;

    // ---- Model slots ----
    juce::Label sourceSlotLabel;
    juce::Label sourceIdLabel;
    juce::TextButton setSourceButton { "Set from current model" };

    juce::Label targetSlotLabel;
    juce::Label targetIdLabel;
    juce::TextButton setTargetButton { "Set from current model" };

    // ---- Estimator preset selector ----
    juce::Label estimatorPresetLabel;
    juce::ComboBox estimatorPresetBox;

    // ---- Transfer controls (APVTS-attached) ----
    juce::Label decolorLabel;
    juce::Label recolorLabel;
    juce::Label envMixLabel;
    juce::Label residualMixLabel;
    juce::Label transientPolicyLabel;

    juce::Slider decolorSlider;
    juce::Slider recolorSlider;
    juce::Slider envMixSlider;
    juce::Slider residualMixSlider;
    juce::ComboBox transientPolicyBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decolorAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> recolorAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> envMixAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> residualMixAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> transientAttach;

    // ---- A/B comparison readout ----
    juce::Label abStatusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralEnvelopeWorkspacePanel)
};

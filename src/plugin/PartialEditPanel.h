#pragma once

#include <JuceHeader.h>

class AtsAudioProcessor;

/** Frame-level edits for one partial index (Qatsh-style table subset). */
class PartialEditPanel : public juce::Component
{
public:
    explicit PartialEditPanel(AtsAudioProcessor&);
    void resized() override;

    /** Update slider ranges from model; refresh displayed cell values. */
    void tick();

private:
    void refreshDisplayedCell();
    void applyEdits();

    AtsAudioProcessor& audioProcessor;

    juce::Label help;
    juce::Label partialLab;
    juce::Slider partialSlider;
    juce::Label frameLab;
    juce::Slider frameSlider;
    juce::Label readout;
    juce::Label ampLab;
    juce::Label freqLab;
    juce::TextEditor ampEditor;
    juce::TextEditor freqEditor;
    juce::TextButton applyButton { "Apply to cell" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PartialEditPanel)
};

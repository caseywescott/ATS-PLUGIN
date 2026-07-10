#pragma once

#include <JuceHeader.h>

class AtsAudioProcessor;

/** Offline MathExpr-style script apply (see docs/MATHEXPR.md). */
class MathExprPanel : public juce::Component
{
public:
    explicit MathExprPanel(AtsAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

    void tick();

private:
    AtsAudioProcessor& audioProcessor;

    juce::Label help;
    juce::TextEditor scriptEditor;
    juce::TextButton applyButton { "Apply script" };
    juce::Label status;

    juce::String lastStatus;
    juce::uint32 flashUntilMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MathExprPanel)
};

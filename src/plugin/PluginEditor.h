#pragma once

#include <JuceHeader.h>

#include "AnalysisTabPanel.h"
#include "MathExprPanel.h"
#include "PartialEditPanel.h"
#include "SessionTabPanel.h"
#include "SynthTabPanel.h"

class AtsAudioProcessor;

class AtsAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AtsAudioProcessorEditor(AtsAudioProcessor&);
    ~AtsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    int scaled(int px) const;

private:
    void timerCallback() override;
    void refreshUndoButtons();

    AtsAudioProcessor& audioProcessor;

    juce::LookAndFeel_V4 pluginLookAndFeel;
    juce::TooltipWindow tooltipWindow;

    SessionTabPanel sessionTab;
    AnalysisTabPanel analysisTab;
    SynthTabPanel synthTab;
    PartialEditPanel partialTab;
    MathExprPanel mathTab;

    juce::TabbedComponent tabs;

    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessorEditor)
};

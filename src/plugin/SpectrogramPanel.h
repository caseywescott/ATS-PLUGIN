#pragma once

#include <JuceHeader.h>

class AtsAudioProcessor;

/** Read-only partial or residual-band spectrogram; image built off the message thread. */
class SpectrogramPanel : public juce::Component, private juce::Timer
{
public:
    explicit SpectrogramPanel(AtsAudioProcessor&);
    ~SpectrogramPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    enum class Mode
    {
        Partials,
        Residual
    };

    void setMode(Mode m);

private:
    void timerCallback() override;
    void rebuildImageAsync();

    AtsAudioProcessor& processor;
    Mode mode = Mode::Partials;
    juce::Image spectroImage;
    /** Normalized time [0,1] and ARGB color for lifecycle event overlay lines. */
    std::vector<std::pair<float, juce::Colour>> lifecycleLines;
    /** Label text for the analysis mode of the currently displayed image. */
    juce::String currentAnalysisModeText;
    int lastModelRevision = -1;
    bool rebuildPending = false;

    juce::ComboBox modeBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramPanel)
};

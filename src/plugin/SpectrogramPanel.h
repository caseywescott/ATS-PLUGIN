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
    int lastModelRevision = -1;
    bool rebuildPending = false;

    juce::ComboBox modeBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramPanel)
};

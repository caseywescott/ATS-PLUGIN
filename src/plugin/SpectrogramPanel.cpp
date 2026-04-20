#include "SpectrogramPanel.h"

#include "PluginProcessor.h"

namespace
{
constexpr int kTexW = 256;
constexpr int kTexH = 160;
} // namespace

SpectrogramPanel::SpectrogramPanel(AtsAudioProcessor& p)
    : processor(p)
{
    setTitle("Model spectrogram");
    setName("Model spectrogram");
    setHelpText("Read-only spectrogram of the analyzed ATS model.");

    modeBox.addItem("Partials (amp)", 1);
    modeBox.addItem("Residual (bands)", 2);
    modeBox.setSelectedId(1, juce::dontSendNotification);
    modeBox.setName("Spectrogram mode");
    modeBox.setHelpText("Choose partial amplitudes or residual critical-band energy.");
    modeBox.onChange = [this] {
        setMode(modeBox.getSelectedId() == 2 ? Mode::Residual : Mode::Partials);
    };
    addAndMakeVisible(modeBox);

    startTimerHz(8);
}

SpectrogramPanel::~SpectrogramPanel() { stopTimer(); }

void SpectrogramPanel::setMode(Mode m)
{
    if (mode == m)
        return;
    mode = m;
    lastModelRevision = -1;
    rebuildPending = true;
}

void SpectrogramPanel::timerCallback()
{
    const int rev = processor.getModelRevision();
    if (rev != lastModelRevision)
    {
        lastModelRevision = rev;
        rebuildPending = true;
    }

    if (rebuildPending)
    {
        rebuildPending = false;
        rebuildImageAsync();
    }
}

void SpectrogramPanel::rebuildImageAsync()
{
    juce::Image img(juce::Image::ARGB, kTexW, kTexH, true);
    img.clear(juce::Rectangle<int>(kTexW, kTexH), juce::Colours::black);
    processor.buildSpectrogramTexture(mode == Mode::Partials, kTexW, kTexH, img);
    spectroImage = img;
    repaint();
}

void SpectrogramPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121218));

    auto area = getLocalBounds().reduced(4);
    const int header = 28;
    auto plot = area.removeFromBottom(area.getHeight() - header);
    modeBox.setBounds(area.removeFromTop(header));

    if (spectroImage.isValid())
        g.drawImageWithin(spectroImage, plot.getX(), plot.getY(), plot.getWidth(), plot.getHeight(),
                          juce::RectanglePlacement::fillDestination);
    else
    {
        g.setColour(juce::Colours::grey);
        g.drawText("No model", plot, juce::Justification::centred);
    }

    g.setColour(juce::Colour(0xffb0b0c0));
    g.setFont(juce::Font(12.0f));
    g.drawText("Read-only spectrogram (downsampled)", plot.removeFromBottom(16), juce::Justification::centredLeft);
}

void SpectrogramPanel::resized()
{
    rebuildPending = true;
}

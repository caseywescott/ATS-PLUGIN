#include "PartialEditPanel.h"
#include "UiFonts.h"

#include "PluginProcessor.h"

PartialEditPanel::PartialEditPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    help.setText("Pick a partial index and frame, edit amplitude and frequency (Hz), then Apply. "
                 "Amplitude must be non-negative; frequency must be positive. Phase is not edited here.",
                 juce::dontSendNotification);
    help.setJustificationType(juce::Justification::topLeft);
    help.setFont(UiFonts::heading());
    addAndMakeVisible(help);

    partialLab.setText("Partial index (partial #)", juce::dontSendNotification);
    partialLab.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(partialLab);

    partialSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    partialSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 22);
    partialSlider.setName("Partial index");
    partialSlider.setRange(0, 0, 1);
    partialSlider.onValueChange = [this] { refreshDisplayedCell(); };
    addAndMakeVisible(partialSlider);

    frameLab.setText("Frame index (time frame #)", juce::dontSendNotification);
    frameLab.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(frameLab);

    frameSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    frameSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 22);
    frameSlider.setName("Frame index");
    frameSlider.setRange(0, 0, 1);
    frameSlider.onValueChange = [this] { refreshDisplayedCell(); };
    addAndMakeVisible(frameSlider);

    readout.setJustificationType(juce::Justification::topLeft);
    readout.setFont(UiFonts::body(12.0f));
    addAndMakeVisible(readout);

    ampLab.setText("Amplitude", juce::dontSendNotification);
    ampLab.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(ampLab);
    addAndMakeVisible(ampEditor);

    freqLab.setText("Frequency (Hz)", juce::dontSendNotification);
    freqLab.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(freqLab);
    addAndMakeVisible(freqEditor);

    applyButton.onClick = [this] { applyEdits(); };
    addAndMakeVisible(applyButton);
}

void PartialEditPanel::tick()
{
    int P = 0, T = 0;
    bool hr = false;
    if (!audioProcessor.getModelFootprint(P, T, hr) || P <= 0 || T <= 0)
    {
        partialSlider.setRange(0, 0, 1);
        frameSlider.setRange(0, 0, 1);
        partialSlider.setEnabled(false);
        frameSlider.setEnabled(false);
        ampEditor.setEnabled(false);
        freqEditor.setEnabled(false);
        applyButton.setEnabled(false);
        readout.setText("No model loaded.", juce::dontSendNotification);
        return;
    }

    partialSlider.setEnabled(true);
    frameSlider.setEnabled(true);
    ampEditor.setEnabled(true);
    freqEditor.setEnabled(true);
    applyButton.setEnabled(true);

    const double pmax = (double) (P - 1);
    const double tmax = (double) (T - 1);
    if (partialSlider.getMaximum() != pmax)
        partialSlider.setRange(0, pmax, 1);
    if (frameSlider.getMaximum() != tmax)
        frameSlider.setRange(0, tmax, 1);

    partialSlider.setValue(juce::jlimit(0.0, pmax, partialSlider.getValue()), juce::dontSendNotification);
    frameSlider.setValue(juce::jlimit(0.0, tmax, frameSlider.getValue()), juce::dontSendNotification);
}

void PartialEditPanel::refreshDisplayedCell()
{
    int P = 0, T = 0;
    bool hr = false;
    if (!audioProcessor.getModelFootprint(P, T, hr))
    {
        readout.setText({}, juce::dontSendNotification);
        return;
    }

    const int pi = (int) std::lround(partialSlider.getValue());
    const int ti = (int) std::lround(frameSlider.getValue());
    double a = 0, f = 0, ph = 0;
    bool hp = false;
    if (!audioProcessor.readPartialCell(pi, ti, a, f, ph, hp))
    {
        readout.setText("Out of range.", juce::dontSendNotification);
        return;
    }

    readout.setText("Current (read-only): phase " + juce::String(hp ? ph : 0.0, 4) + " rad", juce::dontSendNotification);
    ampEditor.setText(juce::String(a, 8), false);
    freqEditor.setText(juce::String(f, 4), false);
}

void PartialEditPanel::applyEdits()
{
    const int pi = (int) std::lround(partialSlider.getValue());
    const int ti = (int) std::lround(frameSlider.getValue());

    const double a = ampEditor.getText().getDoubleValue();
    const double f = freqEditor.getText().getDoubleValue();

    juce::String err;
    if (!audioProcessor.writePartialCell(pi, ti, a, f, err))
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Partial edit", err);
    else
        refreshDisplayedCell();
}

void PartialEditPanel::resized()
{
    auto r = getLocalBounds().reduced(12);
    help.setBounds(r.removeFromTop(52));
    r.removeFromTop(8);

    auto rowP = r.removeFromTop(28);
    partialLab.setBounds(rowP.removeFromLeft(200));
    partialSlider.setBounds(rowP);

    r.removeFromTop(6);
    auto rowF = r.removeFromTop(28);
    frameLab.setBounds(rowF.removeFromLeft(200));
    frameSlider.setBounds(rowF);

    r.removeFromTop(8);
    readout.setBounds(r.removeFromTop(22));
    r.removeFromTop(8);

    auto rowA = r.removeFromTop(28);
    ampLab.setBounds(rowA.removeFromLeft(110));
    ampEditor.setBounds(rowA);

    r.removeFromTop(6);
    auto rowFr = r.removeFromTop(28);
    freqLab.setBounds(rowFr.removeFromLeft(110));
    freqEditor.setBounds(rowFr);

    r.removeFromTop(12);
    applyButton.setBounds(r.removeFromTop(32).removeFromLeft(160));
}

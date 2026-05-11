#include "PluginEditor.h"

#include "PluginProcessor.h"

AtsAudioProcessorEditor::AtsAudioProcessorEditor(AtsAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , sessionTab(p)
    , synthTab(p)
    , partialTab(p)
    , mathTab(p)
    , tabs(juce::TabbedButtonBar::TabsAtTop)
{
    setLookAndFeel(&pluginLookAndFeel);
    pluginLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff12121a));
    pluginLookAndFeel.setColour(juce::Label::textColourId, juce::Colour(0xfff0f0f5));
    pluginLookAndFeel.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a38));
    pluginLookAndFeel.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a3a50));
    pluginLookAndFeel.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0f0f5));
    pluginLookAndFeel.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1e1e2a));
    pluginLookAndFeel.setColour(juce::ComboBox::textColourId, juce::Colour(0xfff0f0f5));
    pluginLookAndFeel.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff4a4a62));
    pluginLookAndFeel.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff6a9cff));
    pluginLookAndFeel.setColour(juce::Slider::thumbColourId, juce::Colour(0xffb8d4ff));
    pluginLookAndFeel.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff1e1e28));
    pluginLookAndFeel.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff5a8cff));
    pluginLookAndFeel.setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(0xff12121a));
    pluginLookAndFeel.setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0xff3a3a48));

    tabs.setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(0xff12121a));
    tabs.setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0xff3a3a48));

    addAndMakeVisible(tabs);
    tabs.addTab("Session", juce::Colours::transparentBlack, &sessionTab, false);
    tabs.addTab("Synthesis", juce::Colours::transparentBlack, &synthTab, false);
    tabs.addTab("Partials", juce::Colours::transparentBlack, &partialTab, false);
    tabs.addTab("Math", juce::Colours::transparentBlack, &mathTab, false);

    // Auto-fit: size the window so the spectrogram panel is visible on launch.
    auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const int screenW = disp ? disp->totalArea.getWidth() : 1920;
    const int screenH = disp ? disp->totalArea.getHeight() : 1080;
    const int resizeMinW = 500;
    const int resizeMinH = 350;
    const int resizeMaxW = juce::jmax(resizeMinW, screenW);
    const int resizeMaxH = juce::jmax(resizeMinH, screenH);
    const int defaultMinW = juce::jmin(700, juce::jmax(resizeMinW, screenW - 50));
    const int defaultMinH = juce::jmin(420, juce::jmax(resizeMinH, screenH - 100));
    const int defaultMaxW = juce::jmax(defaultMinW, screenW - 50);
    const int defaultMaxH = juce::jmax(defaultMinH, screenH - 100);
    const int defaultW = juce::jlimit(defaultMinW, defaultMaxW, (int) (screenW * 0.55f));
    const int defaultH = juce::jlimit(defaultMinH, defaultMaxH, (int) (screenH * 0.58f));
    setResizable(true, true);
    setResizeLimits(resizeMinW, resizeMinH, resizeMaxW, resizeMaxH);
    setSize(defaultW, defaultH);

    startTimerHz(10);
}

AtsAudioProcessorEditor::~AtsAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

int AtsAudioProcessorEditor::scaled(int px) const
{
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return juce::roundToInt((float) px * disp->scale);
    return px;
}

void AtsAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AtsAudioProcessorEditor::resized()
{
    tabs.setBounds(getLocalBounds());
}

void AtsAudioProcessorEditor::timerCallback()
{
    sessionTab.refreshFromProcessor();
    partialTab.tick();
    mathTab.tick();
}

#include "PluginEditor.h"

#include "PluginProcessor.h"

AtsAudioProcessorEditor::AtsAudioProcessorEditor(AtsAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , tooltipWindow(this, 650)
    , sessionTab(p)
    , analysisTab(p)
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
    tabs.addTab("Analysis", juce::Colours::transparentBlack, &analysisTab, false);
    tabs.addTab("Synthesis", juce::Colours::transparentBlack, &synthTab, false);
    tabs.addTab("Partials", juce::Colours::transparentBlack, &partialTab, false);
    tabs.addTab("Math", juce::Colours::transparentBlack, &mathTab, false);

    // Undo / redo, drawn on top of the empty space at the right of the tab bar.
    addAndMakeVisible(undoButton);
    addAndMakeVisible(redoButton);
    undoButton.onClick = [this] { audioProcessor.undo(); refreshUndoButtons(); };
    redoButton.onClick = [this] { audioProcessor.redo(); refreshUndoButtons(); };
    refreshUndoButtons();

    // Allow Cmd/Ctrl+Z and Cmd/Ctrl+Shift+Z to reach keyPressed().
    setWantsKeyboardFocus(true);

    setResizable(true, true);
    setResizeLimits(scaled(580), scaled(400), scaled(1800), scaled(1200));
    setSize(scaled(800), scaled(560));

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
    auto area = getLocalBounds();

    // Reserve a top toolbar strip for the right-aligned undo/redo buttons so they
    // never overlap the tab buttons, then lay the tabs out beneath it.
    const int h = juce::jmax(18, scaled(20));
    const int w = juce::jmax(40, scaled(46));
    const int pad = juce::jmax(2, scaled(3));
    auto strip = area.removeFromTop(h + pad * 2).reduced(pad);
    redoButton.setBounds(strip.removeFromRight(w));
    strip.removeFromRight(pad);
    undoButton.setBounds(strip.removeFromRight(w));

    tabs.setBounds(area);
}

bool AtsAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // commandModifier maps to Cmd on macOS and Ctrl on Windows/Linux.
    const bool cmd = key.getModifiers().isCommandDown();
    if (cmd && key.getKeyCode() == (int) 'Z')
    {
        if (key.getModifiers().isShiftDown())
            audioProcessor.redo();
        else
            audioProcessor.undo();
        refreshUndoButtons();
        return true;
    }
    if (cmd && key.getKeyCode() == (int) 'Y') // Windows-style redo
    {
        audioProcessor.redo();
        refreshUndoButtons();
        return true;
    }
    return false;
}

void AtsAudioProcessorEditor::refreshUndoButtons()
{
    const bool canUndo = audioProcessor.canUndo();
    const bool canRedo = audioProcessor.canRedo();
    undoButton.setEnabled(canUndo);
    redoButton.setEnabled(canRedo);

    const auto undoDesc = audioProcessor.getUndoDescription();
    const auto redoDesc = audioProcessor.getRedoDescription();
    undoButton.setTooltip(canUndo && undoDesc.isNotEmpty() ? "Undo " + undoDesc : juce::String("Nothing to undo"));
    redoButton.setTooltip(canRedo && redoDesc.isNotEmpty() ? "Redo " + redoDesc : juce::String("Nothing to redo"));
}

void AtsAudioProcessorEditor::timerCallback()
{
    sessionTab.refreshFromProcessor();
    analysisTab.refreshFromProcessor();
    partialTab.tick();
    mathTab.tick();
    refreshUndoButtons();
}

#pragma once

#include <JuceHeader.h>

#include "AnalysisTabPanel.h"
#include "MathExprPanel.h"
#include "PartialEditPanel.h"
#include "SessionTabPanel.h"
#include "SynthTabPanel.h"
#include "UiComposites.h"
#include "UiConstants.h"

class AtsAudioProcessor;

class AtsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AtsLookAndFeel();

    void drawButtonBackground(juce::Graphics&,
                              juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawButtonText(juce::Graphics&,
                        juce::TextButton&,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;
    void drawToggleButton(juce::Graphics&,
                          juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;
    void drawComboBox(juce::Graphics&,
                      int width,
                      int height,
                      bool isButtonDown,
                      int buttonX,
                      int buttonY,
                      int buttonW,
                      int buttonH,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          const juce::Slider::SliderStyle,
                          juce::Slider&) override;
    void drawRotarySlider(juce::Graphics&,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider&) override;
    void drawGroupComponentOutline(juce::Graphics&,
                                   int width,
                                   int height,
                                   const juce::String& text,
                                   const juce::Justification& position,
                                   juce::GroupComponent&) override;
    void drawAlertBox(juce::Graphics&,
                      juce::AlertWindow&,
                      const juce::Rectangle<int>& textArea,
                      juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    void drawPopupMenuItem(juce::Graphics&,
                           const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked,
                           bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
};

class HistoryButton : public juce::Button
{
public:
    enum class Direction
    {
        Undo,
        Redo
    };

    explicit HistoryButton(Direction);

    void paintButton(juce::Graphics&,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

private:
    Direction direction;
};

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
    void applyTheme(int theme);
    void showPage(int page);

    AtsAudioProcessor& audioProcessor;

    AtsLookAndFeel pluginLookAndFeel;
    juce::TooltipWindow tooltipWindow;

    SessionTabPanel sessionTab;
    AnalysisTabPanel analysisTab;
    SynthTabPanel synthTab;
    PartialEditPanel partialTab;
    MathExprPanel mathTab;

    ats_ui::SegmentedRadio pageStrip;
    int currentPage = 0;

    juce::Label titleLabel;
    juce::Label contextLabel;

    juce::ComboBox themeBox;
    ats_ui::ActivityDots engineDots;

    HistoryButton undoButton { HistoryButton::Direction::Undo };
    HistoryButton redoButton { HistoryButton::Direction::Redo };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessorEditor)
};

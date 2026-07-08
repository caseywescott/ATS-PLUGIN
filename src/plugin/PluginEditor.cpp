#include "PluginEditor.h"

#include "PluginProcessor.h"

namespace
{
juce::Colour buttonFill(const juce::Button& button, bool highlighted, bool down)
{
    if (!button.isEnabled())
        return ats_ui::colour::surface().withAlpha(0.72f);

    if (button.getToggleState())
        return down ? ats_ui::colour::accentSoft().brighter(0.14f)
                    : ats_ui::colour::accentSoft();

    if (down)
        return ats_ui::colour::surfacePressed();

    return highlighted ? ats_ui::colour::surfaceRaised().brighter(0.08f)
                       : ats_ui::colour::surfaceRaised();
}

void drawChevron(juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::Path arrow;
    arrow.startNewSubPath(area.getX(), area.getY());
    arrow.lineTo(area.getCentreX(), area.getBottom());
    arrow.lineTo(area.getRight(), area.getY());
    g.strokePath(arrow, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace

AtsLookAndFeel::AtsLookAndFeel()
{
    ats_ui::applyEditorPalette(*this);
}

void AtsLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                          juce::Button& button,
                                          const juce::Colour&,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto radius = juce::jmin(6.0f, bounds.getHeight() * 0.28f);

    g.setColour(buttonFill(button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown));
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(button.getToggleState() ? ats_ui::colour::accent().withAlpha(0.62f)
                                        : ats_ui::colour::border().withAlpha(0.86f));
    g.drawRoundedRectangle(bounds, radius, 1.0f);
}

void AtsLookAndFeel::drawButtonText(juce::Graphics& g,
                                    juce::TextButton& button,
                                    bool,
                                    bool)
{
    const float alpha = button.isEnabled() ? 1.0f : 0.48f;
    g.setColour(ats_ui::colour::text().withAlpha(alpha));
    g.setFont(juce::Font(juce::jlimit(10.5f, 12.5f, button.getHeight() * 0.44f),
                         button.getToggleState() ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().reduced(8, 1),
                     juce::Justification::centred,
                     1,
                     0.68f);
}

void AtsLookAndFeel::drawToggleButton(juce::Graphics& g,
                                      juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted,
                                      bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto indicatorSize = juce::jmin(15.0f, bounds.getHeight() - 6.0f);
    auto indicator = bounds.removeFromLeft(indicatorSize + 6.0f)
                         .withSizeKeepingCentre(indicatorSize, indicatorSize);
    auto textArea = bounds.toNearestInt().withTrimmedLeft(1);

    const bool on = button.getToggleState();
    const float alpha = button.isEnabled() ? 1.0f : 0.42f;

    g.setColour(buttonFill(button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown).withAlpha(alpha));
    g.fillRoundedRectangle(indicator, 3.5f);
    g.setColour((on ? ats_ui::colour::accent() : ats_ui::colour::borderStrong()).withAlpha(alpha));
    g.drawRoundedRectangle(indicator, 3.5f, 1.2f);

    if (on)
    {
        juce::Path tick;
        tick.startNewSubPath(indicator.getX() + indicator.getWidth() * 0.24f,
                             indicator.getY() + indicator.getHeight() * 0.52f);
        tick.lineTo(indicator.getX() + indicator.getWidth() * 0.43f,
                    indicator.getY() + indicator.getHeight() * 0.70f);
        tick.lineTo(indicator.getX() + indicator.getWidth() * 0.78f,
                    indicator.getY() + indicator.getHeight() * 0.30f);
        g.setColour(ats_ui::colour::text().withAlpha(alpha));
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour(ats_ui::colour::text().withAlpha(alpha));
    g.setFont(juce::Font(juce::jlimit(9.5f, 11.0f, button.getHeight() * 0.42f), juce::Font::bold));
    g.drawFittedText(button.getButtonText(), textArea, juce::Justification::centredLeft, 1, 0.65f);
}

void AtsLookAndFeel::drawComboBox(juce::Graphics& g,
                                  int width,
                                  int height,
                                  bool isButtonDown,
                                  int,
                                  int,
                                  int,
                                  int,
                                  juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
    const auto fill = isButtonDown ? ats_ui::colour::surfacePressed() : ats_ui::colour::surface();

    g.setColour(box.isEnabled() ? fill : fill.withAlpha(0.62f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(box.hasKeyboardFocus(false) ? ats_ui::colour::accent() : ats_ui::colour::border());
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    g.setColour((box.isEnabled() ? ats_ui::colour::accent() : ats_ui::colour::textMuted()).withAlpha(0.9f));
    drawChevron(g, bounds.withLeft(bounds.getRight() - 22.0f).reduced(6.5f, 8.0f));
}

juce::Font AtsLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return juce::Font(juce::jlimit(10.5f, 12.0f, box.getHeight() * 0.44f));
}

void AtsLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void AtsLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      float sliderPos,
                                      float minSliderPos,
                                      float maxSliderPos,
                                      const juce::Slider::SliderStyle style,
                                      juce::Slider& slider)
{
    juce::ignoreUnused(minSliderPos, maxSliderPos);

    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g,
                                               x,
                                               y,
                                               width,
                                               height,
                                               sliderPos,
                                               minSliderPos,
                                               maxSliderPos,
                                               style,
                                               slider);
        return;
    }

    auto track = juce::Rectangle<float>((float) x + 3.0f,
                                        (float) y + (float) height * 0.5f - 2.0f,
                                        (float) width - 6.0f,
                                        4.0f);
    const float pos = juce::jlimit(track.getX(), track.getRight(), sliderPos);
    const float alpha = slider.isEnabled() ? 1.0f : 0.38f;

    g.setColour(ats_ui::colour::border().withAlpha(alpha));
    g.fillRoundedRectangle(track, 2.0f);
    g.setColour(ats_ui::colour::accent().withAlpha(alpha));
    g.fillRoundedRectangle(track.withRight(pos), 2.0f);

    const auto thumb = juce::Rectangle<float>(pos - 5.0f, track.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour(ats_ui::colour::text().withAlpha(alpha));
    g.fillEllipse(thumb);
    g.setColour(ats_ui::colour::background().withAlpha(alpha));
    g.drawEllipse(thumb, 1.0f);
}

void AtsLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      float sliderPosProportional,
                                      float rotaryStartAngle,
                                      float rotaryEndAngle,
                                      juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(6.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float alpha = slider.isEnabled() ? 1.0f : 0.38f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(ats_ui::colour::border().withAlpha(alpha));
    g.strokePath(track, juce::PathStrokeType(4.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(ats_ui::colour::accent().withAlpha(alpha));
    g.strokePath(valueArc, juce::PathStrokeType(4.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(ats_ui::colour::surfaceRaised().withAlpha(alpha));
    g.fillEllipse(bounds.reduced(radius * 0.30f));
    g.setColour(ats_ui::colour::text().withAlpha(alpha));
    g.fillEllipse(juce::Rectangle<float>(centre.x - 2.5f, centre.y - radius + 1.5f, 5.0f, 5.0f)
                      .transformedBy(juce::AffineTransform::rotation(angle, centre.x, centre.y)));
}

void AtsLookAndFeel::drawGroupComponentOutline(juce::Graphics& g,
                                               int width,
                                               int height,
                                               const juce::String& text,
                                               const juce::Justification&,
                                               juce::GroupComponent& group)
{
    auto bounds = juce::Rectangle<float>(0.5f, 9.5f, (float) width - 1.0f, (float) height - 10.0f);
    g.setColour(ats_ui::colour::surface().withAlpha(group.isEnabled() ? 0.80f : 0.44f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(ats_ui::colour::border().withAlpha(group.isEnabled() ? 0.84f : 0.40f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    if (text.isNotEmpty())
    {
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        const auto textWidth = juce::jmin(width - 18, g.getCurrentFont().getStringWidth(text) + 16);
        auto textArea = juce::Rectangle<int>(10, 0, textWidth, 20);
        g.setColour(ats_ui::colour::header());
        g.fillRoundedRectangle(textArea.toFloat(), 4.0f);
        g.setColour(ats_ui::colour::text());
        g.drawFittedText(text, textArea.reduced(8, 0), juce::Justification::centredLeft, 1, 0.72f);
    }
}

HistoryButton::HistoryButton(Direction d)
    : juce::Button(d == Direction::Undo ? "Undo" : "Redo")
    , direction(d)
{
    setWantsKeyboardFocus(false);
}

void HistoryButton::paintButton(juce::Graphics& g,
                                bool shouldDrawButtonAsHighlighted,
                                bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto radius = juce::jmin(6.0f, bounds.getHeight() * 0.28f);
    const float alpha = isEnabled() ? 1.0f : 0.36f;

    g.setColour(buttonFill(*this, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown).withAlpha(alpha));
    g.fillRoundedRectangle(bounds, radius);
    g.setColour((shouldDrawButtonAsHighlighted ? ats_ui::colour::accent() : ats_ui::colour::border()).withAlpha(alpha));
    g.drawRoundedRectangle(bounds, radius, 1.0f);

    auto icon = bounds.reduced(bounds.getWidth() * 0.26f, bounds.getHeight() * 0.24f);
    const auto centre = icon.getCentre();
    const float r = juce::jmin(icon.getWidth(), icon.getHeight()) * 0.42f;
    const float start = direction == Direction::Undo ? 0.22f * juce::MathConstants<float>::pi
                                                     : 0.78f * juce::MathConstants<float>::pi;
    const float end = direction == Direction::Undo ? 1.72f * juce::MathConstants<float>::pi
                                                   : -0.72f * juce::MathConstants<float>::pi;

    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, r, r, 0.0f, start, end, true);
    g.setColour(ats_ui::colour::text().withAlpha(alpha));
    g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path head;
    if (direction == Direction::Undo)
    {
        const auto p = juce::Point<float>(centre.x - r * 0.88f, centre.y - r * 0.18f);
        head.startNewSubPath(p.x, p.y);
        head.lineTo(p.x + 6.0f, p.y - 4.0f);
        head.lineTo(p.x + 5.5f, p.y + 4.5f);
    }
    else
    {
        const auto p = juce::Point<float>(centre.x + r * 0.88f, centre.y - r * 0.18f);
        head.startNewSubPath(p.x, p.y);
        head.lineTo(p.x - 6.0f, p.y - 4.0f);
        head.lineTo(p.x - 5.5f, p.y + 4.5f);
    }
    head.closeSubPath();
    g.fillPath(head);
}

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

    titleLabel.setText("ATS Resynth", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, ats_ui::colour::text());
    addAndMakeVisible(titleLabel);

    contextLabel.setText("Spectral model editor", juce::dontSendNotification);
    contextLabel.setFont(juce::Font(11.0f));
    contextLabel.setJustificationType(juce::Justification::centredLeft);
    contextLabel.setMinimumHorizontalScale(0.68f);
    contextLabel.setColour(juce::Label::textColourId, ats_ui::colour::textMuted());
    addAndMakeVisible(contextLabel);

    tabs.setColour(juce::TabbedComponent::backgroundColourId, ats_ui::colour::background());
    tabs.setColour(juce::TabbedComponent::outlineColourId, ats_ui::colour::border());
    tabs.setTabBarDepth(scaled(34));

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
    setResizeLimits(scaled(640), scaled(460), scaled(1800), scaled(1200));
    setSize(scaled(940), scaled(640));

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
    g.fillAll(ats_ui::colour::background());

    auto area = getLocalBounds();
    auto header = area.removeFromTop(juce::jmax(44, scaled(48))).toFloat();
    g.setColour(ats_ui::colour::header());
    g.fillRect(header);
    g.setColour(ats_ui::colour::border());
    g.drawHorizontalLine(juce::roundToInt(header.getBottom()) - 1, 0.0f, (float) getWidth());
}

void AtsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    const int headerH = juce::jmax(44, scaled(48));
    const int pad = juce::jmax(8, scaled(10));
    auto header = area.removeFromTop(headerH).reduced(pad, scaled(6));
    const int buttonSize = juce::jmax(30, scaled(32));

    redoButton.setBounds(header.removeFromRight(buttonSize));
    header.removeFromRight(scaled(6));
    undoButton.setBounds(header.removeFromRight(buttonSize));
    header.removeFromRight(scaled(12));

    auto titleBlock = header.removeFromLeft(juce::jmin(header.getWidth(), scaled(260)));
    titleLabel.setBounds(titleBlock.removeFromTop(titleBlock.getHeight() / 2 + scaled(2)));
    contextLabel.setBounds(titleBlock);

    area.reduce(scaled(8), scaled(8));
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

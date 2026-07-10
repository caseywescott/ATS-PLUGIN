#include "MathExprPanel.h"

#include "PluginProcessor.h"
#include "UiConstants.h"

MathExprPanel::MathExprPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    help.setText("One assignment per line: a = <expr>, f = <expr>, or h = <expr> (phase only if model has phase). "
                 "Variables P T p t a f h; functions sin cos tan asin acos atan atan2 log log10 exp sqrt abs min max pow; "
                 "grid reads A(p,t) F(p,t) H(p,t). Runs off the audio thread.",
                 juce::dontSendNotification);
    help.setJustificationType(juce::Justification::topLeft);
    help.setFont(ats_ui::font::ui(12.5f));
    help.setColour(juce::Label::textColourId, ats_ui::colour::textSecondary());
    addAndMakeVisible(help);

    scriptEditor.setMultiLine(true, true);
    scriptEditor.setReturnKeyStartsNewLine(true);
    scriptEditor.setFont(ats_ui::font::mono(12.0f));
    scriptEditor.setText("a = a * 0.95\nf = f");
    scriptEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    scriptEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    scriptEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    scriptEditor.setColour(juce::TextEditor::textColourId, ats_ui::colour::textPrimary());
    scriptEditor.setColour(juce::TextEditor::highlightColourId, ats_ui::colour::accent().withAlpha(0.30f));
    scriptEditor.setColour(juce::CaretComponent::caretColourId, ats_ui::colour::accent());
    addAndMakeVisible(scriptEditor);

    applyButton.setButtonText("APPLY SCRIPT");
    applyButton.onClick = [this] {
        status.setText("Starting…", juce::dontSendNotification);
        audioProcessor.startMathExpr(scriptEditor.getText());
    };
    addAndMakeVisible(applyButton);

    status.setJustificationType(juce::Justification::centredLeft);
    status.setFont(ats_ui::font::mono(10.5f));
    status.setColour(juce::Label::textColourId, ats_ui::colour::textMuted());
    addAndMakeVisible(status);
}

void MathExprPanel::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().reduced(12).toFloat();
    r.removeFromTop(72.0f + 8.0f);          // below help text
    r.removeFromBottom(36.0f);              // above bottom action row
    ats_ui::drawWell(g, r, 8.0f);
}

void MathExprPanel::tick()
{
    const auto s = audioProcessor.getMathStatus();
    if (s != lastStatus)
    {
        lastStatus = s;
        const auto lower = s.toLowerCase();
        const bool success = lower.contains("done") || lower.contains("applied") || lower.contains("ok");
        if (success)
            flashUntilMs = juce::Time::getMillisecondCounter() + 600;
    }
    status.setText(s, juce::dontSendNotification);

    const bool flashing = juce::Time::getMillisecondCounter() < flashUntilMs;
    status.setColour(juce::Label::textColourId,
                     flashing ? ats_ui::colour::accent() : ats_ui::colour::textMuted());
}

void MathExprPanel::resized()
{
    auto r = getLocalBounds().reduced(12);
    help.setBounds(r.removeFromTop(72));
    r.removeFromTop(8);

    auto bot = r.removeFromBottom(36);
    bot.removeFromTop(6);                       // 10px gap above panel edge (approx)
    applyButton.setBounds(bot.removeFromRight(110).withHeight(30));
    bot.removeFromRight(12);
    status.setBounds(bot);

    scriptEditor.setBounds(r.reduced(10));      // inset inside the well frame
}

#include "MathExprPanel.h"

#include "PluginProcessor.h"

MathExprPanel::MathExprPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    help.setText("One assignment per line: a = <expr>, f = <expr>, or h = <expr> (phase only if model has phase). "
                 "Variables P T p t a f h; functions sin cos tan asin acos atan atan2 log log10 exp sqrt abs min max pow; "
                 "grid reads A(p,t) F(p,t) H(p,t). Runs off the audio thread.",
                 juce::dontSendNotification);
    help.setJustificationType(juce::Justification::topLeft);
    help.setFont(juce::Font(12.5f));
    addAndMakeVisible(help);

    scriptEditor.setMultiLine(true, true);
    scriptEditor.setReturnKeyStartsNewLine(true);
    scriptEditor.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    scriptEditor.setText("a = a * 0.95\nf = f");
    addAndMakeVisible(scriptEditor);

    applyButton.onClick = [this] {
        status.setText("Starting…", juce::dontSendNotification);
        audioProcessor.startMathExpr(scriptEditor.getText());
    };
    addAndMakeVisible(applyButton);

    status.setJustificationType(juce::Justification::centredLeft);
    status.setFont(juce::Font(12.0f));
    addAndMakeVisible(status);
}

void MathExprPanel::tick()
{
    status.setText(audioProcessor.getMathStatus(), juce::dontSendNotification);
}

void MathExprPanel::resized()
{
    auto r = getLocalBounds().reduced(12);
    help.setBounds(r.removeFromTop(72));
    r.removeFromTop(8);

    auto bot = r.removeFromBottom(36);
    applyButton.setBounds(bot.removeFromLeft(140));
    bot.removeFromLeft(12);
    status.setBounds(bot);

    scriptEditor.setBounds(r);
}

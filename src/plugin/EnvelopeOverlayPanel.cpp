#include "EnvelopeOverlayPanel.h"

#include "PluginProcessor.h"

#include <utility>

namespace
{
constexpr int kGridSize = 192;

bool curveHasSamples(const ats::EnvelopeOverlayCurve& curve)
{
    return !curve.samples.empty();
}

} // namespace

EnvelopeOverlayPanel::EnvelopeOverlayPanel(AtsAudioProcessor& p)
    : processor(p)
{
    setName("Envelope");
    setTitle("Envelope");
    startTimerHz(8);
}

EnvelopeOverlayPanel::~EnvelopeOverlayPanel()
{
    stopTimer();
}

void EnvelopeOverlayPanel::timerCallback()
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
        rebuildOverlay();
    }
}

void EnvelopeOverlayPanel::rebuildOverlay()
{
    ats::EnvelopeOverlayData next;
    juce::String nextStatus;
    hasOverlay = processor.buildEnvelopeOverlayData(kGridSize, next, nextStatus);
    overlay = std::move(next);
    status = nextStatus;
    repaint();
}

void EnvelopeOverlayPanel::drawCurve(juce::Graphics& g,
                                     const ats::EnvelopeOverlayCurve& curve,
                                     juce::Rectangle<int> plot,
                                     juce::Colour colour,
                                     const float minDb,
                                     const float maxDb)
{
    if (!curveHasSamples(curve))
        return;

    juce::Path path;
    bool started = false;
    const auto n = curve.samples.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& sample = curve.samples[i];
        if (!sample.ok)
            continue;

        const float x = plot.getX() + (n <= 1 ? 0.0f : (float) i * (float) plot.getWidth() / (float) (n - 1));
        const float t = juce::jlimit(0.0f, 1.0f, ((float) sample.valueDb - minDb) / (maxDb - minDb));
        const float y = (float) plot.getBottom() - t * (float) plot.getHeight();
        if (!started)
        {
            path.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            path.lineTo(x, y);
        }
    }

    if (started)
    {
        g.setColour(curve.reliable ? colour : colour.withAlpha(0.38f));
        g.strokePath(path, juce::PathStrokeType(curve.reliable ? 2.0f : 1.25f));
    }
}

void EnvelopeOverlayPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121218));

    auto area = getLocalBounds().reduced(8);
    auto legend = area.removeFromTop(24);
    auto footer = area.removeFromBottom(22);
    auto plot = area.reduced(2);

    g.setColour(juce::Colour(0xff242431));
    g.drawRect(plot, 1);
    g.setColour(juce::Colour(0xff1a1a24));
    for (int i = 1; i < 4; ++i)
    {
        const int y = plot.getY() + i * plot.getHeight() / 4;
        g.drawHorizontalLine(y, (float) plot.getX(), (float) plot.getRight());
    }

    if (!hasOverlay)
    {
        g.setColour(juce::Colour(0xffb0b0c0));
        g.setFont(juce::Font(13.0f));
        g.drawText(status.isNotEmpty() ? status : "No model", plot, juce::Justification::centred);
        return;
    }

    drawCurve(g, overlay.source, plot, juce::Colour(0xff69c8ff), -80.0f, 12.0f);
    drawCurve(g, overlay.target, plot, juce::Colour(0xffffcc66), -80.0f, 12.0f);
    drawCurve(g, overlay.transferGain, plot, juce::Colour(0xff83e08a), -36.0f, 36.0f);

    g.setFont(juce::Font(12.0f));
    int x = legend.getX();
    const auto drawLegend = [&](const juce::String& text, juce::Colour colour) {
        g.setColour(colour);
        g.fillEllipse((float) x, (float) legend.getY() + 8.0f, 7.0f, 7.0f);
        x += 11;
        g.setColour(juce::Colour(0xffd8d8e4));
        g.drawText(text, x, legend.getY(), 72, legend.getHeight(), juce::Justification::centredLeft);
        x += 78;
    };
    drawLegend("Source", juce::Colour(0xff69c8ff));
    drawLegend("Target", juce::Colour(0xffffcc66).withAlpha(curveHasSamples(overlay.target) ? 1.0f : 0.38f));
    drawLegend("Transfer", juce::Colour(0xff83e08a).withAlpha(curveHasSamples(overlay.transferGain) ? 1.0f : 0.38f));

    juce::String footerText = status;
    const auto appendStatus = [&footerText](const std::string& next) {
        if (next.empty())
            return;
        const juce::String text(next.c_str());
        footerText = footerText.isEmpty() ? text : footerText + " | " + text;
    };
    appendStatus(overlay.source.status);
    appendStatus(overlay.target.status);
    appendStatus(overlay.transferGain.status);

    g.setColour(juce::Colour(0xffb0b0c0));
    g.drawText(footerText, footer, juce::Justification::centredLeft);
}

void EnvelopeOverlayPanel::resized()
{
    rebuildPending = true;
}

#include "SpectralEnvelope3DPanel.h"

#include "PluginProcessor.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr juce::uint32 kColourSource = 0xff4090d0; // blue
static constexpr juce::uint32 kColourTarget = 0xffd08030; // amber
static constexpr juce::uint32 kColourAxis   = 0xff303048;
static constexpr juce::uint32 kColourBg     = 0xff0e0e16;
static constexpr juce::uint32 kColourLabel  = 0xffa0a0c0;

// ---------------------------------------------------------------------------
SpectralEnvelope3DPanel::SpectralEnvelope3DPanel(AtsAudioProcessor& p)
    : processor(p)
{
    setName("3D Envelope");
    setTitle("3D Envelope");

    showSourceBtn.setToggleState(true, juce::dontSendNotification);
    showTargetBtn.setToggleState(true, juce::dontSendNotification);
    showSourceBtn.setColour(juce::ToggleButton::textColourId, juce::Colour(kColourSource));
    showTargetBtn.setColour(juce::ToggleButton::textColourId, juce::Colour(kColourTarget));
    showSourceBtn.onStateChange = [this] { repaint(); };
    showTargetBtn.onStateChange = [this] { repaint(); };

    frameCountLabel.setFont(juce::Font(11.0f));
    frameCountLabel.setColour(juce::Label::textColourId, juce::Colour(kColourLabel));
    frameCountLabel.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(showSourceBtn);
    addAndMakeVisible(showTargetBtn);
    addAndMakeVisible(frameCountLabel);

    startTimerHz(4);
}

SpectralEnvelope3DPanel::~SpectralEnvelope3DPanel()
{
    stopTimer();
}

// ---------------------------------------------------------------------------
void SpectralEnvelope3DPanel::timerCallback()
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
        rebuild();
    }
}

void SpectralEnvelope3DPanel::rebuild()
{
    juce::String nextStatus;
    processor.buildAllFrameEnvelopes(kGridSize, kMaxFrames,
                                     sourceFrames, targetFrames,
                                     nextStatus);
    status = nextStatus;
    const int total = (int) sourceFrames.size() + (int) targetFrames.size();
    frameCountLabel.setText("Frames: " + juce::String(total), juce::dontSendNotification);
    repaint();
}

// ---------------------------------------------------------------------------
// Isometric projection
// ---------------------------------------------------------------------------
SpectralEnvelope3DPanel::IsoParams
SpectralEnvelope3DPanel::computeIsoParams(juce::Rectangle<int> plotArea) const
{
    IsoParams p;
    const float w = (float) plotArea.getWidth();
    const float h = (float) plotArea.getHeight();
    p.cx     = (float) plotArea.getX() + w * 0.50f;
    p.cy     = (float) plotArea.getY() + h * 0.72f;
    p.scaleX = w * 0.38f;
    p.scaleY = h * 0.55f;
    p.scaleD = w * 0.18f;
    return p;
}

juce::Point<float> SpectralEnvelope3DPanel::project(float freqN, float frameN, float ampN,
                                                     const IsoParams& p)
{
    // Classic oblique-isometric:
    //   freq axis  → right + slight depth
    //   frame axis → left  + slight depth   (mirror of freq in X)
    //   amp axis   → straight up
    const float sx = p.cx + (freqN - frameN) * p.scaleX;
    const float sy = p.cy + (freqN + frameN) * p.scaleD - ampN * p.scaleY;
    return { sx, sy };
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void SpectralEnvelope3DPanel::drawEnvelopeSurface(
    juce::Graphics& g,
    const std::vector<ats::EnvelopeOverlayCurve>& curves,
    const int totalSets,
    const int setOffset,
    juce::Colour baseColour,
    juce::Rectangle<int> plotArea,
    const IsoParams& iso) const
{
    if (curves.empty())
        return;

    const int n = (int) curves.size();

    // Paint back-to-front (painter's algorithm): highest frameN first.
    for (int ci = n - 1; ci >= 0; --ci)
    {
        const auto& curve = curves[(std::size_t) ci];
        if (curve.samples.empty())
            continue;

        // Normalised position along the full time range [0,1].
        const float frameN = totalSets > 1
            ? (float) (setOffset + ci) / (float) (totalSets - 1)
            : 0.5f;

        // Colour: alpha/brightness ramp (farther = dimmer).
        const float brightness = 0.45f + 0.55f * frameN;
        const float alpha      = 0.35f + 0.55f * frameN;
        const juce::Colour lineCol  = baseColour.withBrightness(brightness).withAlpha(alpha);
        const juce::Colour fillCol  = baseColour.withAlpha(alpha * 0.22f);

        const int nSamples = (int) curve.samples.size();

        // Build outline path (top edge of the ribbon).
        juce::Path outline;
        bool started = false;
        for (int si = 0; si < nSamples; ++si)
        {
            const auto& sample = curve.samples[(std::size_t) si];
            if (!sample.ok)
                continue;
            const float freqN = (nSamples > 1) ? (float) si / (float) (nSamples - 1) : 0.0f;
            const float ampN  = juce::jlimit(0.0f, 1.0f,
                ((float) sample.valueDb - kMinDb) / (kMaxDb - kMinDb));
            const auto pt = project(freqN, frameN, ampN, iso);
            if (!started) { outline.startNewSubPath(pt); started = true; }
            else          { outline.lineTo(pt); }
        }

        if (!started)
            continue;

        // Build filled ribbon: top edge forward, then floor backward.
        juce::Path ribbon = outline;
        // Walk back along floor (amp = 0).
        for (int si = nSamples - 1; si >= 0; --si)
        {
            const auto& sample = curve.samples[(std::size_t) si];
            if (!sample.ok)
                continue;
            const float freqN = (nSamples > 1) ? (float) si / (float) (nSamples - 1) : 0.0f;
            const auto pt = project(freqN, frameN, 0.0f, iso);
            ribbon.lineTo(pt);
        }
        ribbon.closeSubPath();

        g.setColour(fillCol);
        g.fillPath(ribbon);

        g.setColour(lineCol);
        const float strokeW = curve.reliable ? 1.2f : 0.7f;
        g.strokePath(outline, juce::PathStrokeType(strokeW));
    }
}

void SpectralEnvelope3DPanel::drawIsoAxes(juce::Graphics& g,
                                          juce::Rectangle<int> plotArea,
                                          const IsoParams& iso) const
{
    g.setColour(juce::Colour(kColourAxis));

    // Freq axis (frame=0, amp=0): from freqN=0 to freqN=1
    const auto freqStart = project(0.0f, 0.0f, 0.0f, iso);
    const auto freqEnd   = project(1.0f, 0.0f, 0.0f, iso);
    g.drawLine(freqStart.x, freqStart.y, freqEnd.x, freqEnd.y, 1.0f);

    // Time axis (freq=0, amp=0): from frameN=0 to frameN=1
    const auto timeEnd = project(0.0f, 1.0f, 0.0f, iso);
    g.drawLine(freqStart.x, freqStart.y, timeEnd.x, timeEnd.y, 1.0f);

    // Amp axis (freq=0, frame=0): from amp=0 to amp=1
    const auto ampTop = project(0.0f, 0.0f, 1.0f, iso);
    g.drawLine(freqStart.x, freqStart.y, ampTop.x, ampTop.y, 1.0f);

    // Axis labels
    g.setFont(juce::Font(10.0f));
    g.setColour(juce::Colour(kColourLabel));
    g.drawText("freq",
               (int) freqEnd.x - 20, (int) freqEnd.y - 8, 40, 16,
               juce::Justification::centred);
    g.drawText("time",
               (int) timeEnd.x - 20, (int) timeEnd.y - 8, 40, 16,
               juce::Justification::centred);
    g.drawText("+12 dB",
               (int) ampTop.x - 28, (int) ampTop.y - 12, 56, 14,
               juce::Justification::centred);
}

// ---------------------------------------------------------------------------
void SpectralEnvelope3DPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kColourBg));

    auto area    = getLocalBounds().reduced(6);
    auto toolbar = area.removeFromTop(28);
    auto plotArea = area;

    // ---- Toolbar already laid out by resized() ----

    // ---- Empty state ----
    const bool hasData = !sourceFrames.empty() || !targetFrames.empty();
    if (!hasData)
    {
        g.setColour(juce::Colour(kColourLabel));
        g.setFont(juce::Font(13.0f));
        g.drawText(status.isNotEmpty() ? status : "No model loaded",
                   plotArea, juce::Justification::centred);
        return;
    }

    const IsoParams iso = computeIsoParams(plotArea);
    const int totalSets = (int) sourceFrames.size() + (int) targetFrames.size();

    drawIsoAxes(g, plotArea, iso);

    if (showTargetBtn.getToggleState())
        drawEnvelopeSurface(g, targetFrames, totalSets,
                            (int) sourceFrames.size(),
                            juce::Colour(kColourTarget), plotArea, iso);

    if (showSourceBtn.getToggleState())
        drawEnvelopeSurface(g, sourceFrames, totalSets,
                            0,
                            juce::Colour(kColourSource), plotArea, iso);

    // Footer status
    g.setColour(juce::Colour(kColourLabel));
    g.setFont(juce::Font(10.0f));
    g.drawText(status, plotArea.getX(), plotArea.getBottom() - 14,
               plotArea.getWidth() - 4, 14, juce::Justification::centredRight);
}

void SpectralEnvelope3DPanel::resized()
{
    auto area    = getLocalBounds().reduced(6);
    auto toolbar = area.removeFromTop(28);

    showSourceBtn.setBounds(toolbar.removeFromLeft(80));
    toolbar.removeFromLeft(4);
    showTargetBtn.setBounds(toolbar.removeFromLeft(80));
    frameCountLabel.setBounds(toolbar);

    rebuildPending = true;
}

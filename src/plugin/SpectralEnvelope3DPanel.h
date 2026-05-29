#pragma once

#include <JuceHeader.h>

#include "EnvelopeOverlay.h"

class AtsAudioProcessor;

/**
 * Isometric 3D waterfall view of the spectral envelope over time.
 *
 * Each analysis frame is rendered as a frequency-vs-amplitude curve placed at
 * its position along the time (depth) axis, producing a classic "waterfall"
 * display.  The first half of frames is labelled "Source" (blue); the second
 * half "Target" (amber).  Two ToggleButtons control which set is visible.
 */
class SpectralEnvelope3DPanel : public juce::Component, private juce::Timer
{
public:
    explicit SpectralEnvelope3DPanel(AtsAudioProcessor&);
    ~SpectralEnvelope3DPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // --- Timer / data ---
    void timerCallback() override;
    void rebuild();

    // --- Rendering helpers ---
    struct IsoParams
    {
        float cx, cy;     // screen origin
        float scaleX;     // freq/time lateral scale
        float scaleY;     // amplitude vertical scale
        float scaleD;     // depth (time) foreshortening
    };
    IsoParams computeIsoParams(juce::Rectangle<int> plotArea) const;

    // Convert normalised world coords → screen point.
    static juce::Point<float> project(float freqN, float frameN, float ampN,
                                      const IsoParams& p);

    void drawEnvelopeSurface(juce::Graphics& g,
                             const std::vector<ats::EnvelopeOverlayCurve>& curves,
                             int totalSets,
                             int setOffset,
                             juce::Colour baseColour,
                             juce::Rectangle<int> plotArea,
                             const IsoParams& iso) const;

    void drawIsoAxes(juce::Graphics& g,
                     juce::Rectangle<int> plotArea,
                     const IsoParams& iso) const;

    // --- State ---
    AtsAudioProcessor& processor;
    std::vector<ats::EnvelopeOverlayCurve> sourceFrames;
    std::vector<ats::EnvelopeOverlayCurve> targetFrames;
    int lastModelRevision = -1;
    bool rebuildPending = true;
    juce::String status;

    // --- Controls ---
    juce::ToggleButton showSourceBtn { "Source" };
    juce::ToggleButton showTargetBtn { "Target" };
    juce::Label frameCountLabel;

    static constexpr int kGridSize  = 128;
    static constexpr int kMaxFrames = 48;
    static constexpr float kMinDb   = -80.0f;
    static constexpr float kMaxDb   =  12.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralEnvelope3DPanel)
};

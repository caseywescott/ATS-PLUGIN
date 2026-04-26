#include "PresetLibrary.h"

#include "PluginProcessor.h"

namespace ats_preset
{

juce::File getUserPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("ATS Resynth")
                   .getChildFile("Presets");
    dir.createDirectory();
    return dir;
}

juce::StringArray factoryPresetNames()
{
    return juce::StringArray("Default",
                             "Bright partials",
                             "Residual emphasis",
                             "Residuals only",
                             "Modern Voice",
                             "Modern Sustained",
                             "Modern Plucked/Percussive",
                             "Modern Noisy Texture",
                             "Modern Polyphonic");
}

juce::String factoryPresetMetadata(int factoryIndex)
{
    switch (factoryIndex)
    {
    case 0: return "Legacy-compatible baseline. Analysis: Classic ATS. Tracker: Classic recursive freq/SMR.";
    case 1: return "Brighter sinusoidal focus. Analysis: Classic ATS. Tracker: Classic recursive freq/SMR.";
    case 2: return "Residual-forward texture. Analysis: Classic ATS. Tracker: Classic recursive freq/SMR.";
    case 3: return "Residual-only audition mode. Analysis: Classic ATS. Tracker: Classic recursive freq/SMR.";
    case 4: return "Voice material. Analysis: Modern ATS. Tracker: Classic recursive freq/SMR for stable vocal continuity.";
    case 5: return "Sustained instruments. Analysis: Modern ATS. Tracker: Modern assignment.";
    case 6: return "Plucked/percussive material. Analysis: Modern ATS. Tracker: Modern assignment; higher residual emphasis.";
    case 7: return "Noisy textures. Analysis: Modern ATS. Tracker: Classic recursive freq/SMR; strong residual emphasis.";
    case 8: return "Polyphonic sources. Analysis: Modern ATS. Tracker: Modern assignment with all partials enabled.";
    default: return {};
    }
}

static void setNorm(juce::AudioProcessorValueTreeState& apvts, const char* id, float norm01)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(norm01);
}

static void applyDefaults(AtsAudioProcessor& proc)
{
    auto& apvts = proc.apvts;
    setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(1.0f));
    setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(1.0f));
    setNorm(apvts, "freq", apvts.getParameterRange("freq").convertTo0to1(1.0f));
    setNorm(apvts, "timeStretch", apvts.getParameterRange("timeStretch").convertTo0to1(1.0f));
    setNorm(apvts, "synStart", apvts.getParameterRange("synStart").convertTo0to1(0.0f));
    setNorm(apvts, "synDur", apvts.getParameterRange("synDur").convertTo0to1(0.0f));
    if (auto* p = apvts.getParameter("usePhase"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter("useAllPartials"))
        p->setValueNotifyingHost(1.0f);
    if (auto* p = apvts.getParameter("residualOnly"))
        p->setValueNotifyingHost(0.0f);
    setNorm(apvts, "outputTrim", apvts.getParameterRange("outputTrim").convertTo0to1(1.0f));
    if (auto* p = apvts.getParameter("analysisMode"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter("trackerMode"))
        p->setValueNotifyingHost(0.0f);
}

void applyFactoryPreset(AtsAudioProcessor& proc, int factoryIndex)
{
    auto& apvts = proc.apvts;

    switch (factoryIndex)
    {
    case 0:
        applyDefaults(proc);
        break;
    case 1:
        applyDefaults(proc);
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(1.6f));
        setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(0.45f));
        setNorm(apvts, "freq", apvts.getParameterRange("freq").convertTo0to1(1.05f));
        break;
    case 2:
        applyDefaults(proc);
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(0.55f));
        setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(1.75f));
        break;
    case 3:
        applyDefaults(proc);
        if (auto* p = apvts.getParameter("residualOnly"))
            p->setValueNotifyingHost(1.0f);
        setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(1.0f));
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(0.0f));
        break;
    case 4: // voice
        applyDefaults(proc);
        if (auto* p = apvts.getParameter("analysisMode"))
            p->setValueNotifyingHost(1.0f);
        if (auto* p = apvts.getParameter("trackerMode"))
            p->setValueNotifyingHost(0.0f);
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(1.2f));
        setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(0.8f));
        break;
    case 5: // sustained
        applyDefaults(proc);
        if (auto* p = apvts.getParameter("analysisMode"))
            p->setValueNotifyingHost(1.0f);
        if (auto* p = apvts.getParameter("trackerMode"))
            p->setValueNotifyingHost(1.0f);
        setNorm(apvts, "usePhase", 1.0f);
        break;
    case 6: // plucked/percussive
        applyDefaults(proc);
        if (auto* p = apvts.getParameter("analysisMode"))
            p->setValueNotifyingHost(1.0f);
        if (auto* p = apvts.getParameter("trackerMode"))
            p->setValueNotifyingHost(1.0f);
        setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(1.35f));
        break;
    case 7: // noisy texture
        applyDefaults(proc);
        if (auto* p = apvts.getParameter("analysisMode"))
            p->setValueNotifyingHost(1.0f);
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(0.5f));
        setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(1.8f));
        break;
    case 8: // polyphonic
        applyDefaults(proc);
        if (auto* p = apvts.getParameter("analysisMode"))
            p->setValueNotifyingHost(1.0f);
        if (auto* p = apvts.getParameter("trackerMode"))
            p->setValueNotifyingHost(1.0f);
        setNorm(apvts, "useAllPartials", 1.0f);
        break;
    default:
        break;
    }
}

} // namespace ats_preset

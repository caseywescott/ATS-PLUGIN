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
    return juce::StringArray("Default", "Bright partials", "Residual emphasis");
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
    setNorm(apvts, "outputTrim", apvts.getParameterRange("outputTrim").convertTo0to1(1.0f));
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
    default:
        break;
    }
}

} // namespace ats_preset

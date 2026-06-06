#include "PresetLibrary.h"

#include "PluginProcessor.h"

#include <array>

namespace ats_preset
{

namespace
{
struct FactoryPresetInfo
{
    const char* name;
    const char* description;
    bool morphPreset = false;
};

constexpr std::array<FactoryPresetInfo, 12> kFactoryPresets {{
    { "Default", "Classic neutral ATS playback defaults.", false },
    { "Bright partials", "Classic playback with stronger sinusoidal partials and reduced residual energy.", false },
    { "Residual emphasis", "Classic playback with a quieter partial layer and stronger residual layer.", false },
    { "Residuals only", "Classic playback focused entirely on the residual/noise layer.", false },
    { "Inharmonic spread", "Stretch mode opens partial spacing around the auto pivot for clangorous inharmonic color.", false },
    { "Clustered partials", "Compress mode pulls partial spacing inward around the auto pivot.", false },
    { "Harmonic bloom", "Harmonic mode gently exaggerates rank-based harmonic spacing from the auto F0 estimate.", false },
    { "Morph stable default", "Bark grid, frequency-rank matching, log-frequency/power interpolation, oscillator phase, and conservative smoothing for broad source material.", true },
    { "Morph tonal", "Bark grid with greedy nearest matching, Bark-coordinate frequency motion, relative-cluster phase metadata, and lower residual gain for sustained harmonic material.", true },
    { "Morph vocal/formant", "Bark grid with greedy nearest matching, dB amplitude interpolation, source-only merge-to-residual handling, and sustained-after-attack protection for vowel/formant material.", true },
    { "Morph percussive", "Bark grid with quick smoothing, crossfaded attack metadata, and hold/fade unmatched handling for plucked or struck sources.", true },
    { "Morph noisy texture", "Mel grid with greedy nearest matching, dB amplitude interpolation, merge-to-residual unmatched handling, and a residual-heavy lane balance for breathy or noisy material.", true },
}};

} // namespace

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
    juce::StringArray names;
    for (const auto& preset : kFactoryPresets)
        names.add(preset.name);
    return names;
}

juce::String factoryPresetDescription(int factoryIndex)
{
    if (juce::isPositiveAndBelow(factoryIndex, (int) kFactoryPresets.size()))
        return kFactoryPresets[(size_t) factoryIndex].description;
    return {};
}

static void setNorm(juce::AudioProcessorValueTreeState& apvts, const char* id, float norm01)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(norm01);
}

static void setPlain(juce::AudioProcessorValueTreeState& apvts, const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(apvts.getParameterRange(id).convertTo0to1(plainValue));
}

static void setBool(juce::AudioProcessorValueTreeState& apvts, const char* id, bool value)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(value ? 1.0f : 0.0f);
}

static void setChoice(juce::AudioProcessorValueTreeState& apvts, const char* id, int index)
{
    setPlain(apvts, id, (float) index);
}

static void applyDefaults(AtsAudioProcessor& proc)
{
    auto& apvts = proc.apvts;
    setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(1.0f));
    setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(1.0f));
    setNorm(apvts, "freq", apvts.getParameterRange("freq").convertTo0to1(1.0f));
    setChoice(apvts, ats_param::kFreqShapeMode, 0);
    setPlain(apvts, ats_param::kFreqShapeAmount, 1.0f);
    setPlain(apvts, ats_param::kFreqShapePivotHz, 0.0f);
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
}

static void setMorphLane(juce::AudioProcessorValueTreeState& apvts,
                         size_t laneIndex,
                         float amount,
                         bool linked)
{
    if (laneIndex >= ats_param::kMorphLaneCount)
        return;

    setPlain(apvts, ats_param::kMorphLaneAmountIds[laneIndex], amount);
    setBool(apvts, ats_param::kMorphLaneLinkedIds[laneIndex], linked);
}

static void applyMorphPresetBase(AtsAudioProcessor& proc,
                                 float performanceMacro,
                                 float smoothingMs)
{
    auto& apvts = proc.apvts;
    applyDefaults(proc);
    setPlain(apvts, ats_param::kMorphAmount, 0.5f);
    setPlain(apvts, ats_param::kMorphPerformanceMacro, performanceMacro);
    setPlain(apvts, ats_param::kMorphSmoothingMs, smoothingMs);
    setChoice(apvts, ats_param::kMorphGridType, 0);
    setChoice(apvts, ats_param::kMorphCorrespondenceStrategy, 3); // optimal transport
    setChoice(apvts, ats_param::kMorphUnmatchedSourcePolicy, 0);
    setChoice(apvts, ats_param::kMorphUnmatchedTargetPolicy, 1);
    setBool(apvts, ats_param::kMorphDiscardOptIn, false);
    setChoice(apvts, ats_param::kMorphFrequencyInterpolationDomain, 0);
    setChoice(apvts, ats_param::kMorphAmplitudeInterpolationDomain, 0);
    setChoice(apvts, ats_param::kMorphPhaseStrategy, 0);
    setChoice(apvts, ats_param::kMorphTransientPolicy, 0);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
        setMorphLane(apvts, i, 0.5f, true);
}

static void applyMorphStableDefault(AtsAudioProcessor& proc)
{
    applyMorphPresetBase(proc, 0.35f, 60.0f);
}

static void applyMorphTonal(AtsAudioProcessor& proc)
{
    auto& apvts = proc.apvts;
    applyMorphPresetBase(proc, 0.25f, 80.0f);
    setPlain(apvts, "partGain", 1.25f);
    setPlain(apvts, "resGain", 0.70f);
    setChoice(apvts, ats_param::kMorphCorrespondenceStrategy, 1);
    setChoice(apvts, ats_param::kMorphFrequencyInterpolationDomain, 1);
    setChoice(apvts, ats_param::kMorphPhaseStrategy, 3);
    setChoice(apvts, ats_param::kMorphTransientPolicy, 3);
}

static void applyMorphVocalFormant(AtsAudioProcessor& proc)
{
    auto& apvts = proc.apvts;
    applyMorphPresetBase(proc, 0.45f, 90.0f);
    setPlain(apvts, "partGain", 0.95f);
    setPlain(apvts, "resGain", 1.15f);
    setChoice(apvts, ats_param::kMorphCorrespondenceStrategy, 1);
    setChoice(apvts, ats_param::kMorphUnmatchedSourcePolicy, 3);
    setChoice(apvts, ats_param::kMorphAmplitudeInterpolationDomain, 1);
    setChoice(apvts, ats_param::kMorphTransientPolicy, 3);
    setMorphLane(apvts, (size_t) ats::MorphLane::ResidualNoise, 0.65f, false);
}

static void applyMorphPercussive(AtsAudioProcessor& proc)
{
    auto& apvts = proc.apvts;
    applyMorphPresetBase(proc, 0.80f, 18.0f);
    setPlain(apvts, "partGain", 1.05f);
    setPlain(apvts, "resGain", 1.25f);
    setChoice(apvts, ats_param::kMorphUnmatchedSourcePolicy, 2);
    setChoice(apvts, ats_param::kMorphCorrespondenceStrategy, 1);
    setChoice(apvts, ats_param::kMorphTransientPolicy, 2);
}

static void applyMorphNoisyTexture(AtsAudioProcessor& proc)
{
    auto& apvts = proc.apvts;
    applyMorphPresetBase(proc, 0.75f, 120.0f);
    setPlain(apvts, "partGain", 0.70f);
    setPlain(apvts, "resGain", 1.45f);
    setChoice(apvts, ats_param::kMorphGridType, 1);
    setChoice(apvts, ats_param::kMorphCorrespondenceStrategy, 1);
    setChoice(apvts, ats_param::kMorphUnmatchedSourcePolicy, 3);
    setChoice(apvts, ats_param::kMorphUnmatchedTargetPolicy, 3);
    setChoice(apvts, ats_param::kMorphAmplitudeInterpolationDomain, 1);
    setChoice(apvts, ats_param::kMorphTransientPolicy, 2);
    setMorphLane(apvts, (size_t) ats::MorphLane::Frequency, 0.35f, false);
    setMorphLane(apvts, (size_t) ats::MorphLane::ResidualNoise, 0.85f, false);
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
    case 4:
        applyDefaults(proc);
        setChoice(apvts, ats_param::kFreqShapeMode, 1);
        setPlain(apvts, ats_param::kFreqShapeAmount, 1.35f);
        setPlain(apvts, ats_param::kFreqShapePivotHz, 0.0f);
        break;
    case 5:
        applyDefaults(proc);
        setChoice(apvts, ats_param::kFreqShapeMode, 2);
        setPlain(apvts, ats_param::kFreqShapeAmount, 0.72f);
        setPlain(apvts, ats_param::kFreqShapePivotHz, 0.0f);
        break;
    case 6:
        applyDefaults(proc);
        setChoice(apvts, ats_param::kFreqShapeMode, 3);
        setPlain(apvts, ats_param::kFreqShapeAmount, 1.15f);
        setPlain(apvts, ats_param::kFreqShapePivotHz, 0.0f);
        break;
    case 7:
        applyMorphStableDefault(proc);
        break;
    case 8:
        applyMorphTonal(proc);
        break;
    case 9:
        applyMorphVocalFormant(proc);
        break;
    case 10:
        applyMorphPercussive(proc);
        break;
    case 11:
        applyMorphNoisyTexture(proc);
        break;
    default:
        break;
    }

    if (juce::isPositiveAndBelow(factoryIndex, (int) kFactoryPresets.size())
        && kFactoryPresets[(size_t) factoryIndex].morphPreset)
    {
        proc.setMorphPresetMetadata(kFactoryPresets[(size_t) factoryIndex].name,
                                    kFactoryPresets[(size_t) factoryIndex].description);
    }
}

} // namespace ats_preset

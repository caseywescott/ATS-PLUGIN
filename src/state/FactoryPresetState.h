#pragma once

#include "../core/ParameterRegistry.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace ats::state
{

struct FactoryPresetInfo
{
    std::string_view name;
    std::string_view description;
    bool morphPreset = false;
};

// Names and descriptions exactly as the legacy PresetLibrary shipped them.
inline constexpr std::array<FactoryPresetInfo, 12> kFactoryPresets {{
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

// JUCE-free port of the twelve documented factory presets.  This is kept next
// to the neutral preset service so hosts and the editor share exactly one
// parameter recipe rather than reproducing legacy UI-side mutations.
//
// Like the legacy PresetLibrary, a recipe only writes the parameters it names:
// analysis, timbre, Smearnov and (for the classic presets) morph settings the
// user has made survive loading a factory preset.
inline bool applyFactoryPreset(std::int32_t index, ParameterStore& parameters) noexcept
{
    if (index < 0 || index >= 12)
        return false;

    const auto set = [&parameters](std::string_view id, float value) {
        (void) parameters.write(id, value);
    };
    const auto defaults = [&set] {
        set("partGain", 1.0f); set("resGain", 1.0f); set("freq", 1.0f);
        set("freqShapeMode", 0.0f); set("freqShapeAmount", 1.0f);
        set("freqShapePivotHz", 0.0f); set("timeStretch", 1.0f);
        set("synStart", 0.0f); set("synDur", 0.0f); set("usePhase", 0.0f);
        set("useAllPartials", 1.0f); set("residualOnly", 0.0f); set("outputTrim", 1.0f);
    };
    const auto morphBase = [&set, &defaults](float macro, float smoothing) {
        defaults();
        set("morphAmount", 0.5f); set("morphPerformanceMacro", macro);
        set("morphSmoothingMs", smoothing); set("morphGridType", 0.0f);
        set("morphCorrespondenceStrategy", 3.0f); set("morphQuality", 1.0f);
        set("morphUnmatchedSourcePolicy", 0.0f); set("morphUnmatchedTargetPolicy", 1.0f);
        set("morphDiscardOptIn", 0.0f); set("morphFrequencyInterpolationDomain", 0.0f);
        set("morphAmplitudeInterpolationDomain", 0.0f); set("morphPhaseStrategy", 0.0f);
        set("morphTransientPolicy", 0.0f);
        constexpr std::array<std::string_view, 6> amounts { "morphLaneFrequencyAmount", "morphLaneAmplitudeAmount", "morphLaneResidualAmount", "morphLaneTimeAmount", "morphLaneTransientAmount", "morphLanePhaseAmount" };
        constexpr std::array<std::string_view, 6> links { "morphLaneFrequencyLinked", "morphLaneAmplitudeLinked", "morphLaneResidualLinked", "morphLaneTimeLinked", "morphLaneTransientLinked", "morphLanePhaseLinked" };
        for (std::size_t i = 0; i < amounts.size(); ++i) { set(amounts[i], 0.5f); set(links[i], 1.0f); }
    };

    switch (index)
    {
        case 0: defaults(); break;
        case 1: defaults(); set("partGain", 1.6f); set("resGain", 0.45f); set("freq", 1.05f); break;
        case 2: defaults(); set("partGain", 0.55f); set("resGain", 1.75f); break;
        case 3: defaults(); set("residualOnly", 1.0f); set("partGain", 0.0f); break;
        case 4: defaults(); set("freqShapeMode", 1.0f); set("freqShapeAmount", 1.35f); break;
        case 5: defaults(); set("freqShapeMode", 2.0f); set("freqShapeAmount", 0.72f); break;
        case 6: defaults(); set("freqShapeMode", 3.0f); set("freqShapeAmount", 1.15f); break;
        case 7: morphBase(0.35f, 60.0f); break;
        case 8: morphBase(0.25f, 80.0f); set("partGain", 1.25f); set("resGain", 0.70f); set("morphCorrespondenceStrategy", 1.0f); set("morphFrequencyInterpolationDomain", 1.0f); set("morphPhaseStrategy", 3.0f); set("morphTransientPolicy", 3.0f); break;
        case 9: morphBase(0.45f, 90.0f); set("partGain", 0.95f); set("resGain", 1.15f); set("morphCorrespondenceStrategy", 1.0f); set("morphUnmatchedSourcePolicy", 3.0f); set("morphAmplitudeInterpolationDomain", 1.0f); set("morphTransientPolicy", 3.0f); set("morphLaneResidualAmount", 0.65f); set("morphLaneResidualLinked", 0.0f); break;
        case 10: morphBase(0.80f, 18.0f); set("partGain", 1.05f); set("resGain", 1.25f); set("morphUnmatchedSourcePolicy", 2.0f); set("morphCorrespondenceStrategy", 1.0f); set("morphTransientPolicy", 2.0f); break;
        case 11: morphBase(0.75f, 120.0f); set("partGain", 0.70f); set("resGain", 1.45f); set("morphGridType", 1.0f); set("morphCorrespondenceStrategy", 1.0f); set("morphUnmatchedSourcePolicy", 3.0f); set("morphUnmatchedTargetPolicy", 3.0f); set("morphAmplitudeInterpolationDomain", 1.0f); set("morphTransientPolicy", 2.0f); set("morphLaneFrequencyAmount", 0.35f); set("morphLaneFrequencyLinked", 0.0f); set("morphLaneResidualAmount", 0.85f); set("morphLaneResidualLinked", 0.0f); break;
    }
    return true;
}

} // namespace ats::state

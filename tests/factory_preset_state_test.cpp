#include "../src/state/FactoryPresetState.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <string>

namespace
{
bool close(float a, float b) { return std::abs(a - b) < 1.0e-3f; }

using Recipe = std::map<std::string, float>;

// Independent transcription of the legacy JUCE PresetLibrary.cpp recipes
// (src/plugin/PresetLibrary.cpp at 1a64f030^), not derived from the port.
Recipe legacyDefaults()
{
    return { { "partGain", 1.0f }, { "resGain", 1.0f }, { "freq", 1.0f },
             { "freqShapeMode", 0.0f }, { "freqShapeAmount", 1.0f }, { "freqShapePivotHz", 0.0f },
             { "timeStretch", 1.0f }, { "synStart", 0.0f }, { "synDur", 0.0f },
             { "usePhase", 0.0f }, { "useAllPartials", 1.0f }, { "residualOnly", 0.0f },
             { "outputTrim", 1.0f } };
}

Recipe legacyMorphBase(float macro, float smoothing)
{
    auto r = legacyDefaults();
    r["morphAmount"] = 0.5f;
    r["morphPerformanceMacro"] = macro;
    r["morphSmoothingMs"] = smoothing;
    r["morphGridType"] = 0.0f;
    r["morphCorrespondenceStrategy"] = 3.0f;
    r["morphQuality"] = 1.0f;
    r["morphUnmatchedSourcePolicy"] = 0.0f;
    r["morphUnmatchedTargetPolicy"] = 1.0f;
    r["morphDiscardOptIn"] = 0.0f;
    r["morphFrequencyInterpolationDomain"] = 0.0f;
    r["morphAmplitudeInterpolationDomain"] = 0.0f;
    r["morphPhaseStrategy"] = 0.0f;
    r["morphTransientPolicy"] = 0.0f;
    // Legacy MorphLane order: Frequency, Amplitude, ResidualNoise, Time, Transient, Phase.
    for (const char* lane : { "Frequency", "Amplitude", "Residual", "Time", "Transient", "Phase" })
    {
        r[std::string("morphLane") + lane + "Amount"] = 0.5f;
        r[std::string("morphLane") + lane + "Linked"] = 1.0f;
    }
    return r;
}

Recipe legacyRecipe(int index)
{
    Recipe r;
    switch (index)
    {
    case 0: r = legacyDefaults(); break;
    case 1: r = legacyDefaults(); r["partGain"] = 1.6f; r["resGain"] = 0.45f; r["freq"] = 1.05f; break;
    case 2: r = legacyDefaults(); r["partGain"] = 0.55f; r["resGain"] = 1.75f; break;
    case 3: r = legacyDefaults(); r["residualOnly"] = 1.0f; r["resGain"] = 1.0f; r["partGain"] = 0.0f; break;
    case 4: r = legacyDefaults(); r["freqShapeMode"] = 1.0f; r["freqShapeAmount"] = 1.35f; break;
    case 5: r = legacyDefaults(); r["freqShapeMode"] = 2.0f; r["freqShapeAmount"] = 0.72f; break;
    case 6: r = legacyDefaults(); r["freqShapeMode"] = 3.0f; r["freqShapeAmount"] = 1.15f; break;
    case 7: r = legacyMorphBase(0.35f, 60.0f); break;
    case 8:
        r = legacyMorphBase(0.25f, 80.0f);
        r["partGain"] = 1.25f; r["resGain"] = 0.70f;
        r["morphCorrespondenceStrategy"] = 1.0f; r["morphFrequencyInterpolationDomain"] = 1.0f;
        r["morphPhaseStrategy"] = 3.0f; r["morphTransientPolicy"] = 3.0f;
        break;
    case 9:
        r = legacyMorphBase(0.45f, 90.0f);
        r["partGain"] = 0.95f; r["resGain"] = 1.15f;
        r["morphCorrespondenceStrategy"] = 1.0f; r["morphUnmatchedSourcePolicy"] = 3.0f;
        r["morphAmplitudeInterpolationDomain"] = 1.0f; r["morphTransientPolicy"] = 3.0f;
        r["morphLaneResidualAmount"] = 0.65f; r["morphLaneResidualLinked"] = 0.0f;
        break;
    case 10:
        r = legacyMorphBase(0.80f, 18.0f);
        r["partGain"] = 1.05f; r["resGain"] = 1.25f;
        r["morphUnmatchedSourcePolicy"] = 2.0f; r["morphCorrespondenceStrategy"] = 1.0f;
        r["morphTransientPolicy"] = 2.0f;
        break;
    case 11:
        r = legacyMorphBase(0.75f, 120.0f);
        r["partGain"] = 0.70f; r["resGain"] = 1.45f;
        r["morphGridType"] = 1.0f; r["morphCorrespondenceStrategy"] = 1.0f;
        r["morphUnmatchedSourcePolicy"] = 3.0f; r["morphUnmatchedTargetPolicy"] = 3.0f;
        r["morphAmplitudeInterpolationDomain"] = 1.0f; r["morphTransientPolicy"] = 2.0f;
        r["morphLaneFrequencyAmount"] = 0.35f; r["morphLaneFrequencyLinked"] = 0.0f;
        r["morphLaneResidualAmount"] = 0.85f; r["morphLaneResidualLinked"] = 0.0f;
        break;
    default: break;
    }
    return r;
}

// A "user" value for every parameter that differs from its default wherever
// the range allows, so a preset that resets untouched parameters is caught.
float userValue(const ats::ParameterDescriptor& d)
{
    if (d.type == ats::ParameterType::boolean)
        return d.defaultValue >= 0.5f ? 0.0f : 1.0f;
    if (d.type == ats::ParameterType::choice || d.type == ats::ParameterType::integer)
        return d.snap(d.defaultValue + 1.0f <= d.maximum ? d.defaultValue + 1.0f : d.minimum);
    return d.snap(d.minimum + 0.37f * (d.maximum - d.minimum));
}
}

int main()
{
    ats::ParameterStore parameters;
    assert(!ats::state::applyFactoryPreset(-1, parameters));
    assert(!ats::state::applyFactoryPreset(12, parameters));

    // Names and descriptions as the legacy library shipped them.
    assert(ats::state::kFactoryPresets[0].name == "Default");
    assert(ats::state::kFactoryPresets[3].name == "Residuals only");
    assert(ats::state::kFactoryPresets[9].name == "Morph vocal/formant");
    assert(ats::state::kFactoryPresets[11].name == "Morph noisy texture");
    for (std::size_t i = 0; i < ats::state::kFactoryPresets.size(); ++i)
    {
        assert(!ats::state::kFactoryPresets[i].description.empty());
        assert(ats::state::kFactoryPresets[i].morphPreset == (i >= 7));
    }

    int checked = 0;
    for (int index = 0; index < 12; ++index)
    {
        ats::ParameterStore store;
        for (const auto& d : ats::ParameterRegistry::descriptors())
            (void) store.write(d.denseIndex, userValue(d));
        assert(ats::state::applyFactoryPreset(index, store));

        const auto recipe = legacyRecipe(index);
        for (const auto& [id, value] : recipe)
            assert(ats::ParameterRegistry::find(id) != nullptr);
        for (const auto& d : ats::ParameterRegistry::descriptors())
        {
            const auto actual = store.read(d.denseIndex);
            const auto it = recipe.find(std::string(d.id));
            const auto expected = it != recipe.end() ? d.snap(it->second) : userValue(d);
            if (!close(actual, expected))
            {
                std::cerr << "preset " << index << " (" << ats::state::kFactoryPresets[
                                 static_cast<std::size_t>(index)].name
                          << "): " << d.id << " = " << actual << ", legacy " << expected
                          << (it != recipe.end() ? " (recipe)" : " (preserved user value)") << '\n';
                assert(false);
            }
            ++checked;
        }
    }
    assert(checked == 12 * static_cast<int>(ats::ParameterRegistry::descriptors().size()));
    std::cout << "factory_preset_state_test: 12 presets x 81 parameters match the legacy recipes\n";
    return 0;
}

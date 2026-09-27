#include <string>
#include "ParameterRegistry.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    const auto& descriptors = ats::ParameterRegistry::descriptors();
    assert(descriptors.size() == 81);
    for (std::size_t i = 0; i < descriptors.size(); ++i)
    {
        assert(!descriptors[i].id.empty());
        assert(ats::ParameterRegistry::indexOf(descriptors[i].id) == i);
        assert(descriptors[i].denseIndex == i);
        assert(descriptors[i].hostNumericId == i + 1);
        assert(!descriptors[i].group.empty());
        assert(descriptors[i].participatesInState && descriptors[i].participatesInPreset);
        assert(descriptors[i].defaultValue >= descriptors[i].minimum);
        assert(descriptors[i].defaultValue <= descriptors[i].maximum);
    }

    const auto* gain = ats::ParameterRegistry::find("partGain");
    assert(gain != nullptr);
    assert(std::fabs(gain->denormalize(0.5f) - (4.0f * std::pow(0.5f, 0.35f))) < 1.0e-5f);
    assert(std::fabs(gain->normalize(gain->denormalize(0.5f)) - 0.5f) < 1.0e-5f);

    const auto* choice = ats::ParameterRegistry::find("morphQuality");
    assert(choice != nullptr && choice->text(1.0f) == "Default");
    float parsed = -1.0f;
    assert(choice->fromText("Economy", parsed) && parsed == 2.0f);

    const auto* integer = ats::ParameterRegistry::find("analysisWinCycles");
    assert(integer != nullptr && integer->snap(3.6f) == 4.0f);

    const auto* trim = ats::ParameterRegistry::find("outputTrim");
    assert(trim != nullptr);
    assert(trim->smoothing == ats::ParameterSmoothing::onePole20Ms);
    assert(trim->updatePolicy == ats::ParameterUpdatePolicy::realtime);
    assert(ats::ParameterRegistry::find("analysisType")->group == "Analysis");
    assert(ats::ParameterRegistry::find("freq")->group == "Synthesis");
    assert(ats::ParameterRegistry::find("smearnovMix")->group == "Smearnov");

    ats::ParameterStore store;
    assert(store.size() == 81);
    assert(store.read("partGain") == 1.0f);
    assert(store.write("partGain", 2.12341f));
    assert(std::fabs(store.read("partGain") - 2.1234f) < 1.0e-5f);
    assert(store.write("morphQuality", 2.8f) && store.read("morphQuality") == 2.0f);
    store.resetToDefaults();
    assert(store.read("partGain") == 1.0f && store.read("morphQuality") == 1.0f);
    std::cout << "parameter_registry_test: 81 descriptors ok\n";

    // Snapping must land exactly on interval multiples on every architecture;
    // arm64 FMA used to leave -12 + 1200 * 0.01f at 2.2e-7 ("2.2209e-07").
    for (const auto* id : { "timbreBrightnessDb", "timbreTiltDbPerOct" })
    {
        const auto* descriptor = ats::ParameterRegistry::find(id);
        assert(descriptor != nullptr);
        assert(descriptor->snap(0.0f) == 0.0f);
        assert(descriptor->snap(1.0e-7f) == 0.0f);
        assert(descriptor->text(0.0f) == "0");
        assert(descriptor->text(descriptor->defaultValue) == "0");
        assert(descriptor->snap(1.234f) == 1.23f);
    }
    for (const auto& descriptor : ats::ParameterRegistry::descriptors())
        if (descriptor.type != ats::ParameterType::choice &&
            descriptor.type != ats::ParameterType::boolean)
            assert(descriptor.text(descriptor.defaultValue).find('e') == std::string::npos);
}

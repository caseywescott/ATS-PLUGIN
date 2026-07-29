#include "../src/state/FactoryPresetState.h"

#include <cassert>
#include <cmath>

namespace
{
bool close(float a, float b) { return std::abs(a - b) < 1.0e-3f; }
}

int main()
{
    ats::ParameterStore parameters;
    assert(!ats::state::applyFactoryPreset(-1, parameters));
    assert(!ats::state::applyFactoryPreset(12, parameters));

    assert(ats::state::applyFactoryPreset(1, parameters));
    assert(close(parameters.read("partGain"), 1.6f));
    assert(close(parameters.read("resGain"), 0.45f));
    assert(close(parameters.read("freq"), 1.05f));

    assert(ats::state::applyFactoryPreset(9, parameters));
    assert(parameters.read("morphCorrespondenceStrategy") == 1.0f);
    assert(parameters.read("morphUnmatchedSourcePolicy") == 3.0f);
    assert(close(parameters.read("morphLaneResidualAmount"), 0.65f));
    assert(parameters.read("morphLaneResidualLinked") == 0.0f);

    assert(ats::state::applyFactoryPreset(11, parameters));
    assert(parameters.read("morphGridType") == 1.0f);
    assert(close(parameters.read("morphLaneFrequencyAmount"), 0.35f));
    assert(close(parameters.read("morphLaneResidualAmount"), 0.85f));
    return 0;
}

#pragma once

#include <cstdint>
#include <vector>

namespace ats
{

struct ResidualBandNoiseState
{
    double lowPassedAtUpperCutoff = 0.0;
    double lowPassedAtLowerCutoff = 0.0;
};

double residualBandCenterHz(int bandIndex, int sampleRate);

float renderResidualBandNoiseSample(double bandEnergy,
                                    int bandIndex,
                                    int sampleIndex,
                                    std::uint32_t seed,
                                    int sampleRate,
                                    double gain,
                                    ResidualBandNoiseState& state);

std::vector<float> renderResidualBandNoiseFrames(
    const std::vector<std::vector<double>>& bandEnergyByFrame,
    int hopSizeSamples,
    double gain,
    std::uint32_t seed,
    int sampleRate = 48000);

} // namespace ats

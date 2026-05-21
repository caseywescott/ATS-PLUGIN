#include "AtsResidualBandNoise.h"

#include "AtsModernModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ats
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

constexpr std::array<double, kModernAtsDefaultResidualBands + 1> kBarkBandEdgesHz = {
    0.0,   100.0, 200.0, 300.0, 400.0, 510.0, 630.0, 770.0, 920.0,
    1080.0, 1270.0, 1480.0, 1720.0, 2000.0, 2320.0, 2700.0, 3150.0,
    3700.0, 4400.0, 5300.0, 6400.0, 7700.0, 9500.0, 12000.0, 15500.0,
    20500.0
};

std::uint32_t mix32(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float deterministicNoise(std::uint32_t seed, int sampleIndex, int streamIndex)
{
    const auto x = mix32(seed ^ (0x9e3779b9u * static_cast<std::uint32_t>(streamIndex + 1))
                         ^ static_cast<std::uint32_t>(sampleIndex + 0x85ebca6bu));
    return (static_cast<float>(x & 0x00ffffffu) / 8388607.5f) - 1.0f;
}

double positiveFinite(double value)
{
    return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}

double onePoleAlpha(double cutoffHz, int sampleRate)
{
    if (sampleRate <= 0)
        return 1.0;

    const double nyquist = 0.5 * static_cast<double>(sampleRate);
    const double cutoff = std::max(0.0, std::min(cutoffHz, nyquist));
    if (cutoff <= 0.0)
        return 0.0;
    if (cutoff >= nyquist)
        return 1.0;

    return 1.0 - std::exp(-2.0 * kPi * cutoff / static_cast<double>(sampleRate));
}

void residualBandBoundsHz(int bandIndex, int sampleRate, double& lowHz, double& highHz)
{
    const int band = std::max(0, std::min(kModernAtsDefaultResidualBands - 1, bandIndex));
    const double nyquist = sampleRate > 0 ? 0.5 * static_cast<double>(sampleRate) : 24000.0;
    lowHz = std::min(kBarkBandEdgesHz[static_cast<std::size_t>(band)], nyquist);
    highHz = std::min(kBarkBandEdgesHz[static_cast<std::size_t>(band + 1)], nyquist);
    if (highHz <= lowHz)
        highHz = std::min(nyquist, lowHz + 1.0);
}

double bandEnergyAt(const std::vector<std::vector<double>>& frames, int frameIndex, int bandIndex)
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(frames.size()))
        return 0.0;

    const auto& frame = frames[static_cast<std::size_t>(frameIndex)];
    if (bandIndex < 0 || bandIndex >= static_cast<int>(frame.size()))
        return 0.0;

    return positiveFinite(frame[static_cast<std::size_t>(bandIndex)]);
}

} // namespace

double residualBandCenterHz(int bandIndex, int sampleRate)
{
    double lowHz = 0.0;
    double highHz = 0.0;
    residualBandBoundsHz(bandIndex, sampleRate, lowHz, highHz);
    if (lowHz <= 0.0)
        return highHz * 0.5;
    return std::sqrt(lowHz * highHz);
}

float renderResidualBandNoiseSample(double bandEnergy,
                                    int bandIndex,
                                    int sampleIndex,
                                    std::uint32_t seed,
                                    int sampleRate,
                                    double gain,
                                    ResidualBandNoiseState& state)
{
    if (gain <= 0.0 || !std::isfinite(gain))
        return 0.0f;

    double lowHz = 0.0;
    double highHz = 0.0;
    residualBandBoundsHz(bandIndex, sampleRate, lowHz, highHz);
    const double highAlpha = onePoleAlpha(highHz, sampleRate);
    const double lowAlpha = onePoleAlpha(lowHz, sampleRate);
    const double input = static_cast<double>(deterministicNoise(seed, sampleIndex, bandIndex));

    state.lowPassedAtUpperCutoff += highAlpha * (input - state.lowPassedAtUpperCutoff);
    state.lowPassedAtLowerCutoff += lowAlpha * (input - state.lowPassedAtLowerCutoff);

    const double shapedNoise = state.lowPassedAtUpperCutoff - state.lowPassedAtLowerCutoff;
    const double energy = positiveFinite(bandEnergy);
    if (energy <= 0.0)
        return 0.0f;

    return static_cast<float>(std::sqrt(energy) * gain * shapedNoise);
}

std::vector<float> renderResidualBandNoiseFrames(
    const std::vector<std::vector<double>>& bandEnergyByFrame,
    int hopSizeSamples,
    double gain,
    std::uint32_t seed,
    int sampleRate)
{
    if (hopSizeSamples <= 0 || bandEnergyByFrame.empty() || gain <= 0.0 || !std::isfinite(gain))
        return {};

    std::vector<float> output(bandEnergyByFrame.size() * static_cast<std::size_t>(hopSizeSamples), 0.0f);
    std::array<ResidualBandNoiseState, kModernAtsDefaultResidualBands> states {};

    for (std::size_t sample = 0; sample < output.size(); ++sample)
    {
        const int frameIndex = static_cast<int>(sample / static_cast<std::size_t>(hopSizeSamples));
        const int nextFrameIndex =
            std::min(frameIndex + 1, static_cast<int>(bandEnergyByFrame.size()) - 1);
        const double localT =
            static_cast<double>(sample % static_cast<std::size_t>(hopSizeSamples))
            / static_cast<double>(hopSizeSamples);

        double mixed = 0.0;
        for (int band = 0; band < kModernAtsDefaultResidualBands; ++band)
        {
            const double energy = bandEnergyAt(bandEnergyByFrame, frameIndex, band) * (1.0 - localT)
                                  + bandEnergyAt(bandEnergyByFrame, nextFrameIndex, band) * localT;
            mixed += renderResidualBandNoiseSample(energy,
                                                   band,
                                                   static_cast<int>(sample),
                                                   seed,
                                                   sampleRate,
                                                   gain,
                                                   states[static_cast<std::size_t>(band)]);
        }

        output[sample] = static_cast<float>(mixed);
    }

    return output;
}

} // namespace ats

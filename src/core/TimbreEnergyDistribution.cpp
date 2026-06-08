#include "TimbreEnergyDistribution.h"

#include "FrequencyWarp.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ats
{

namespace
{

double safeNormalizedPosition(double frequencyHz, const EnvelopeAxisSpec& axis)
{
    if (!isValidFrequencyAxis(axis))
        return 0.0;
    bool ok = false;
    const double position = normalizedPositionForFrequency(frequencyHz, axis, &ok);
    if (!ok || !std::isfinite(position))
        return 0.0;
    return std::clamp(position, 0.0, 1.0);
}

double frequencyAtBinCenter(int binIndex, int binCount, const EnvelopeAxisSpec& axis)
{
    if (binCount <= 1 || !isValidFrequencyAxis(axis))
        return 0.0;
    const double position = (static_cast<double>(binIndex) + 0.5)
                            / static_cast<double>(binCount);
    bool ok = false;
    const double frequency = frequencyForNormalizedPosition(position, axis, &ok);
    return ok && std::isfinite(frequency) ? frequency : 0.0;
}

void finalizeStatus(EnergyDistribution& distribution)
{
    if (distribution.samples.empty())
    {
        distribution.status = TimbreDescriptorStatus::EmptyInput;
        return;
    }
    if (distribution.totalEnergy <= 0.0)
    {
        distribution.status = TimbreDescriptorStatus::EmptyInput;
        return;
    }
    distribution.status = TimbreDescriptorStatus::Available;
}

} // namespace

EnergyDistribution buildPartialEnergyDistribution(
    const std::vector<PartialEnergySample>& partials,
    const EnvelopeAxisSpec& axis)
{
    EnergyDistribution out;
    out.samples.reserve(partials.size());

    double minHz = std::numeric_limits<double>::infinity();
    double maxHz = -std::numeric_limits<double>::infinity();

    for (const auto& partial : partials)
    {
        EnergySample sample;
        sample.frequencyHz = partial.frequencyHz;
        sample.axisPosition = safeNormalizedPosition(partial.frequencyHz, axis);
        sample.energy = std::max(partial.energy, 0.0);
        out.samples.push_back(sample);
        out.totalEnergy += sample.energy;
        if (sample.energy > 0.0)
        {
            ++out.activePointCount;
            minHz = std::min(minHz, sample.frequencyHz);
            maxHz = std::max(maxHz, sample.frequencyHz);
        }
    }

    std::sort(out.samples.begin(), out.samples.end(),
              [](const EnergySample& a, const EnergySample& b) {
                  return a.frequencyHz < b.frequencyHz;
              });

    if (out.activePointCount == 0)
    {
        out.minFrequencyHz = 0.0;
        out.maxFrequencyHz = 0.0;
    }
    else
    {
        out.minFrequencyHz = minHz;
        out.maxFrequencyHz = maxHz;
    }
    out.representedEnergyRatio = 1.0;
    finalizeStatus(out);
    return out;
}

EnergyDistribution buildResidualEnergyDistribution(
    const std::vector<ResidualEnergySample>& bands,
    const EnvelopeAxisSpec& axis)
{
    EnergyDistribution out;
    out.samples.reserve(bands.size());

    double minHz = std::numeric_limits<double>::infinity();
    double maxHz = -std::numeric_limits<double>::infinity();

    for (const auto& band : bands)
    {
        EnergySample sample;
        sample.frequencyHz = band.frequencyHz;
        sample.axisPosition = safeNormalizedPosition(band.frequencyHz, axis);
        sample.energy = std::max(band.energy, 0.0);
        out.samples.push_back(sample);
        out.totalEnergy += sample.energy;
        if (sample.energy > 0.0)
        {
            ++out.activePointCount;
            minHz = std::min(minHz, sample.frequencyHz);
            maxHz = std::max(maxHz, sample.frequencyHz);
        }
    }

    std::sort(out.samples.begin(), out.samples.end(),
              [](const EnergySample& a, const EnergySample& b) {
                  return a.frequencyHz < b.frequencyHz;
              });

    if (out.activePointCount == 0)
    {
        out.minFrequencyHz = 0.0;
        out.maxFrequencyHz = 0.0;
    }
    else
    {
        out.minFrequencyHz = minHz;
        out.maxFrequencyHz = maxHz;
    }
    out.representedEnergyRatio = 1.0;
    finalizeStatus(out);
    return out;
}

EnergyDistribution buildCombinedEnergyDistribution(
    const std::vector<PartialEnergySample>& partials,
    const std::vector<ResidualEnergySample>& bands,
    const EnvelopeAxisSpec& axis)
{
    EnergyDistribution out;
    out.samples.reserve(partials.size() + bands.size());

    double minHz = std::numeric_limits<double>::infinity();
    double maxHz = -std::numeric_limits<double>::infinity();

    for (const auto& partial : partials)
    {
        EnergySample sample;
        sample.frequencyHz = partial.frequencyHz;
        sample.axisPosition = safeNormalizedPosition(partial.frequencyHz, axis);
        sample.energy = std::max(partial.energy, 0.0);
        out.samples.push_back(sample);
        out.totalEnergy += sample.energy;
        if (sample.energy > 0.0)
        {
            ++out.activePointCount;
            minHz = std::min(minHz, sample.frequencyHz);
            maxHz = std::max(maxHz, sample.frequencyHz);
        }
    }
    for (const auto& band : bands)
    {
        EnergySample sample;
        sample.frequencyHz = band.frequencyHz;
        sample.axisPosition = safeNormalizedPosition(band.frequencyHz, axis);
        sample.energy = std::max(band.energy, 0.0);
        out.samples.push_back(sample);
        out.totalEnergy += sample.energy;
        if (sample.energy > 0.0)
        {
            ++out.activePointCount;
            minHz = std::min(minHz, sample.frequencyHz);
            maxHz = std::max(maxHz, sample.frequencyHz);
        }
    }

    std::sort(out.samples.begin(), out.samples.end(),
              [](const EnergySample& a, const EnergySample& b) {
                  return a.frequencyHz < b.frequencyHz;
              });

    if (out.activePointCount == 0)
    {
        out.minFrequencyHz = 0.0;
        out.maxFrequencyHz = 0.0;
    }
    else
    {
        out.minFrequencyHz = minHz;
        out.maxFrequencyHz = maxHz;
    }
    out.representedEnergyRatio = 1.0;
    finalizeStatus(out);
    return out;
}

ProjectedGrid projectEnergyOntoUniformGrid(
    const EnergyDistribution& distribution,
    const EnvelopeAxisSpec& axis,
    int binCount)
{
    ProjectedGrid grid;
    grid.totalEnergyBefore = distribution.totalEnergy;

    if (binCount <= 1)
    {
        grid.status = TimbreDescriptorStatus::Unsupported;
        return grid;
    }
    if (!isValidFrequencyAxis(axis))
    {
        grid.status = TimbreDescriptorStatus::MissingEnvelope;
        return grid;
    }
    if (distribution.samples.empty() || distribution.totalEnergy <= 0.0)
    {
        grid.status = TimbreDescriptorStatus::EmptyInput;
        return grid;
    }

    grid.bins.assign(static_cast<std::size_t>(binCount), 0.0);
    grid.binCenterHz.assign(static_cast<std::size_t>(binCount), 0.0);
    for (int binIndex = 0; binIndex < binCount; ++binIndex)
    {
        grid.binCenterHz[static_cast<std::size_t>(binIndex)] =
            frequencyAtBinCenter(binIndex, binCount, axis);
    }

    double included = 0.0;
    for (const auto& sample : distribution.samples)
    {
        if (sample.energy <= 0.0)
            continue;

        // Convert axis position [0, 1] to a continuous bin coordinate so that
        // the projection splits the sample energy between its two neighbors.
        const double continuousBin =
            sample.axisPosition * static_cast<double>(binCount) - 0.5;
        if (continuousBin <= -0.5)
        {
            // Below the lowest bin centre; assign fully to bin 0.
            grid.bins.front() += sample.energy;
            included += sample.energy;
            continue;
        }
        if (continuousBin >= static_cast<double>(binCount) - 0.5)
        {
            grid.bins.back() += sample.energy;
            included += sample.energy;
            continue;
        }

        const int lower = static_cast<int>(std::floor(continuousBin));
        const int upper = lower + 1;
        const double weightUpper = continuousBin - static_cast<double>(lower);
        const double weightLower = 1.0 - weightUpper;

        if (lower >= 0 && lower < binCount)
            grid.bins[static_cast<std::size_t>(lower)] += sample.energy * weightLower;
        if (upper >= 0 && upper < binCount)
            grid.bins[static_cast<std::size_t>(upper)] += sample.energy * weightUpper;
        included += sample.energy;
    }

    grid.totalEnergyAfter = 0.0;
    grid.activeBinCount = 0;
    for (const auto value : grid.bins)
    {
        grid.totalEnergyAfter += value;
        if (value > 0.0)
            ++grid.activeBinCount;
    }
    grid.excludedEnergy = std::max(grid.totalEnergyBefore - included, 0.0);
    grid.representedEnergyRatio = grid.totalEnergyBefore > 0.0
        ? std::clamp(included / grid.totalEnergyBefore, 0.0, 1.0)
        : 0.0;
    grid.status = grid.totalEnergyAfter > 0.0
        ? TimbreDescriptorStatus::Available
        : TimbreDescriptorStatus::EmptyInput;
    return grid;
}

EnvelopeGridSamples sampleFittedEnvelopeOntoGrid(
    const FrameEnvelope& envelope,
    const EnvelopeAxisSpec& axis,
    int binCount,
    double amplitudeFloor,
    const EnvelopeSamplerFn& sampler)
{
    EnvelopeGridSamples out;

    if (binCount <= 1 || !isValidFrequencyAxis(axis))
    {
        out.status = TimbreDescriptorStatus::MissingEnvelope;
        return out;
    }
    if (envelope.values.empty() || !sampler)
    {
        out.status = TimbreDescriptorStatus::MissingEnvelope;
        return out;
    }

    out.values.assign(static_cast<std::size_t>(binCount), 0.0);
    out.energy.assign(static_cast<std::size_t>(binCount), 0.0);
    out.binCenterHz.assign(static_cast<std::size_t>(binCount), 0.0);

    const double floor = std::max(amplitudeFloor, 0.0);
    double minHz = std::numeric_limits<double>::infinity();
    double maxHz = -std::numeric_limits<double>::infinity();

    for (int binIndex = 0; binIndex < binCount; ++binIndex)
    {
        const double centerHz = frequencyAtBinCenter(binIndex, binCount, axis);
        out.binCenterHz[static_cast<std::size_t>(binIndex)] = centerHz;
        if (!std::isfinite(centerHz) || centerHz <= 0.0)
            continue;

        bool ok = false;
        const double amplitude = sampler(envelope, centerHz, &ok);
        if (!ok || !std::isfinite(amplitude))
            continue;

        const double value = amplitude < floor ? floor : amplitude;
        out.values[static_cast<std::size_t>(binIndex)] = value;
        out.energy[static_cast<std::size_t>(binIndex)] = value * value;
        if (value > floor)
            ++out.activeBinCount;
        minHz = std::min(minHz, centerHz);
        maxHz = std::max(maxHz, centerHz);
    }

    if (out.activeBinCount == 0)
    {
        out.minBinHz = 0.0;
        out.maxBinHz = 0.0;
        out.status = TimbreDescriptorStatus::EmptyInput;
        return out;
    }
    out.minBinHz = minHz;
    out.maxBinHz = maxHz;
    out.status = TimbreDescriptorStatus::Available;
    return out;
}

} // namespace ats

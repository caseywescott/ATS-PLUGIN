#include "TimbreFrameAnalysis.h"

#include <algorithm>
#include <cmath>

namespace ats
{

namespace
{

double weightedConfidence(
    const std::vector<PartialEnergySample>& partials,
    const double partialEnergy)
{
    if (partialEnergy <= 0.0)
        return 0.0;
    double sum = 0.0;
    for (const auto& sample : partials)
        sum += sample.energy * sample.confidence;
    return std::clamp(sum / partialEnergy, 0.0, 1.0);
}

double weightedResidualConfidence(
    const std::vector<ResidualEnergySample>& bands,
    const double residualEnergy)
{
    if (residualEnergy <= 0.0)
        return 0.0;
    double sum = 0.0;
    for (const auto& band : bands)
        sum += band.energy * band.confidence;
    return std::clamp(sum / residualEnergy, 0.0, 1.0);
}

double combinedConfidence(
    const std::vector<PartialEnergySample>& partials,
    const std::vector<ResidualEnergySample>& bands,
    const double combinedEnergy)
{
    if (combinedEnergy <= 0.0)
        return 0.0;
    double sum = 0.0;
    for (const auto& sample : partials)
        sum += sample.energy * sample.confidence;
    for (const auto& band : bands)
        sum += band.energy * band.confidence;
    return std::clamp(sum / combinedEnergy, 0.0, 1.0);
}

} // namespace

EnergyAnalysisResult analyzeEnergyAndNoisinessDescriptors(
    const ValidatedFrameSamples& samples,
    const TimbreDescriptorOptions& options,
    TimbreDescriptorFrame& frame)
{
    EnergyAnalysisResult result;

    const bool hasPartials = !samples.partials.empty()
                              && samples.partialEnergyTotal > 0.0;
    const bool hasResidual = samples.residualBandsPresent;
    const bool hasResidualEnergy = hasResidual && samples.residualEnergyTotal > 0.0;

    // partial_energy
    {
        auto& d = frame.partialEnergy;
        d.domain = TimbreDescriptorDomain::Partials;
        if (hasPartials)
        {
            d.value = samples.partialEnergyTotal;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = weightedConfidence(samples.partials, samples.partialEnergyTotal);
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::EmptyInput;
            d.confidence = 0.0;
        }
        d.coverage.activePointCount = static_cast<int>(samples.partials.size());
        d.coverage.supportedFrequencySpanHz =
            samples.maxPartialFrequencyHz - samples.minPartialFrequencyHz;
        d.coverage.representedEnergyRatio = hasPartials ? 1.0 : 0.0;
    }

    // residual_energy
    {
        auto& d = frame.residualEnergy;
        d.domain = TimbreDescriptorDomain::ResidualBands;
        if (hasResidual)
        {
            d.value = samples.residualEnergyTotal;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = weightedResidualConfidence(
                samples.residualBands, samples.residualEnergyTotal);
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::MissingResidual;
            d.confidence = 0.0;
        }
        d.coverage.activePointCount = static_cast<int>(samples.residualBands.size());
        d.coverage.supportedFrequencySpanHz =
            samples.maxResidualFrequencyHz - samples.minResidualFrequencyHz;
        d.coverage.representedEnergyRatio = hasResidual ? 1.0 : 0.0;
    }

    const double combined = samples.partialEnergyTotal
                          + (hasResidual ? samples.residualEnergyTotal : 0.0);

    // combined_energy
    {
        auto& d = frame.combinedEnergy;
        d.domain = TimbreDescriptorDomain::Combined;
        if (hasPartials || hasResidual)
        {
            d.value = combined;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = combinedConfidence(samples.partials,
                                              samples.residualBands,
                                              combined);
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::EmptyInput;
            d.confidence = 0.0;
        }
        d.coverage.activePointCount = static_cast<int>(samples.partials.size()
                                                       + samples.residualBands.size());
        d.coverage.supportedFrequencySpanHz =
            std::max(samples.maxPartialFrequencyHz, samples.maxResidualFrequencyHz)
            - std::min(samples.minPartialFrequencyHz > 0.0
                       ? samples.minPartialFrequencyHz
                       : samples.minResidualFrequencyHz,
                       samples.minResidualFrequencyHz > 0.0
                       ? samples.minResidualFrequencyHz
                       : samples.minPartialFrequencyHz);
        if (!std::isfinite(d.coverage.supportedFrequencySpanHz)
            || d.coverage.supportedFrequencySpanHz < 0.0)
        {
            d.coverage.supportedFrequencySpanHz = 0.0;
        }
        d.coverage.representedEnergyRatio = (hasPartials || hasResidual) ? 1.0 : 0.0;
    }

    // noisiness
    {
        auto& d = frame.noisiness;
        d.domain = TimbreDescriptorDomain::Combined;
        if (!hasResidual)
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::MissingResidual;
            d.confidence = 0.0;
        }
        else if (combined <= 0.0)
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::EmptyInput;
            d.confidence = 0.0;
        }
        else
        {
            d.value = samples.residualEnergyTotal / combined;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = combinedConfidence(samples.partials,
                                              samples.residualBands,
                                              combined);
        }
        d.coverage.activePointCount = static_cast<int>(samples.partials.size()
                                                       + samples.residualBands.size());
        d.coverage.representedEnergyRatio = (hasPartials || hasResidual) ? 1.0 : 0.0;
    }

    // orphan_residual_ratio
    {
        auto& d = frame.orphanResidualRatio;
        d.domain = TimbreDescriptorDomain::ResidualBands;
        if (!hasResidual)
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::MissingResidual;
            d.confidence = 0.0;
        }
        else
        {
            double orphan = 0.0;
            for (const auto& band : samples.residualBands)
            {
                if (band.orphanBand)
                    orphan += band.energy;
            }
            if (samples.residualEnergyTotal <= 0.0)
            {
                d.value = 0.0;
                d.status = TimbreDescriptorStatus::EmptyInput;
                d.confidence = 0.0;
            }
            else
            {
                d.value = orphan / samples.residualEnergyTotal;
                d.status = TimbreDescriptorStatus::Available;
                d.confidence = weightedResidualConfidence(
                    samples.residualBands, samples.residualEnergyTotal);
            }
        }
        d.coverage.activePointCount = static_cast<int>(samples.residualBands.size());
    }

    // bandwise_noisiness on the configured comparison grid
    {
        auto& d = frame.bandwiseNoisiness;
        d.domain = TimbreDescriptorDomain::Combined;
        if (!hasResidual)
        {
            d.values.clear();
            d.status = TimbreDescriptorStatus::MissingResidual;
            d.confidence = 0.0;
        }
        else
        {
            const int gridSize = std::max(options.comparisonGridSize, 2);
            const auto partialDistribution = buildPartialEnergyDistribution(
                samples.partials, options.comparisonAxis);
            const auto residualDistribution = buildResidualEnergyDistribution(
                samples.residualBands, options.comparisonAxis);
            const auto partialGrid = projectEnergyOntoUniformGrid(
                partialDistribution, options.comparisonAxis, gridSize);
            const auto residualGrid = projectEnergyOntoUniformGrid(
                residualDistribution, options.comparisonAxis, gridSize);

            if (residualGrid.status != TimbreDescriptorStatus::Available
                && partialGrid.status != TimbreDescriptorStatus::Available)
            {
                d.values.assign(static_cast<std::size_t>(gridSize), 0.0);
                d.status = TimbreDescriptorStatus::EmptyInput;
                d.confidence = 0.0;
            }
            else
            {
                d.values.assign(static_cast<std::size_t>(gridSize), 0.0);
                double activeBins = 0.0;
                for (int bin = 0; bin < gridSize; ++bin)
                {
                    const double partialBin =
                        bin < static_cast<int>(partialGrid.bins.size())
                            ? partialGrid.bins[static_cast<std::size_t>(bin)] : 0.0;
                    const double residualBin =
                        bin < static_cast<int>(residualGrid.bins.size())
                            ? residualGrid.bins[static_cast<std::size_t>(bin)] : 0.0;
                    const double total = partialBin + residualBin;
                    if (total <= 0.0)
                    {
                        d.values[static_cast<std::size_t>(bin)] = 0.0;
                        continue;
                    }
                    d.values[static_cast<std::size_t>(bin)] = residualBin / total;
                    activeBins += 1.0;
                }
                d.status = TimbreDescriptorStatus::Available;
                d.confidence = combinedConfidence(samples.partials,
                                                  samples.residualBands,
                                                  combined);
                d.coverage.activePointCount = static_cast<int>(activeBins);
                d.coverage.supportedFrequencySpanHz =
                    samples.maxResidualFrequencyHz - samples.minResidualFrequencyHz;
                d.coverage.representedEnergyRatio = 1.0;
            }
        }
    }

    // Harmonic-to-noise ratio compatibility view (returned to caller).
    if (hasResidualEnergy && hasPartials)
    {
        result.harmonicToNoiseRatioDb = 10.0
            * std::log10(samples.partialEnergyTotal / samples.residualEnergyTotal);
        result.harmonicToNoiseStatus = TimbreDescriptorStatus::Available;
    }
    else if (!hasResidual)
    {
        result.harmonicToNoiseStatus = TimbreDescriptorStatus::MissingResidual;
    }
    else
    {
        result.harmonicToNoiseStatus = TimbreDescriptorStatus::EmptyInput;
    }

    frame.sourceModelId = samples.sourceModelId;
    frame.frameIndex = samples.frameIndex;

    return result;
}

} // namespace ats

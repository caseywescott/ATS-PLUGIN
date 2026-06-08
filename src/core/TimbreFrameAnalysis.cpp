#include "TimbreFrameAnalysis.h"

#include "FrequencyWarp.h"

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

namespace
{

struct MomentResult
{
    double centroidHz = 0.0;
    double spreadHz = 0.0;
    double skewness = 0.0;
    double kurtosis = 0.0;
    int activeCount = 0;
    double totalEnergy = 0.0;
    bool centroidAvailable = false;
    bool spreadAvailable = false;
    bool higherOrderAvailable = false;
};

MomentResult computeMoments(const EnergyDistribution& distribution)
{
    MomentResult out;
    if (distribution.samples.empty() || distribution.totalEnergy <= 0.0)
        return out;

    double weightedFreq = 0.0;
    int active = 0;
    for (const auto& sample : distribution.samples)
    {
        if (sample.energy <= 0.0)
            continue;
        weightedFreq += sample.frequencyHz * sample.energy;
        ++active;
    }
    if (active == 0)
        return out;

    const double total = distribution.totalEnergy;
    out.totalEnergy = total;
    out.centroidHz = weightedFreq / total;
    out.activeCount = active;
    out.centroidAvailable = true;

    if (active < 2)
        return out;

    double secondMoment = 0.0;
    double thirdMoment = 0.0;
    double fourthMoment = 0.0;
    for (const auto& sample : distribution.samples)
    {
        if (sample.energy <= 0.0)
            continue;
        const double delta = sample.frequencyHz - out.centroidHz;
        const double d2 = delta * delta;
        const double weight = sample.energy / total;
        secondMoment += weight * d2;
        thirdMoment += weight * d2 * delta;
        fourthMoment += weight * d2 * d2;
    }

    if (secondMoment <= 0.0)
    {
        out.spreadHz = 0.0;
        return out;
    }
    out.spreadHz = std::sqrt(secondMoment);
    out.spreadAvailable = true;

    const double sigma3 = out.spreadHz * out.spreadHz * out.spreadHz;
    const double sigma4 = sigma3 * out.spreadHz;
    if (sigma3 > 0.0 && sigma4 > 0.0)
    {
        out.skewness = thirdMoment / sigma3;
        out.kurtosis = fourthMoment / sigma4;
        out.higherOrderAvailable = true;
    }
    return out;
}

double computeAxisCentroid(const EnergyDistribution& distribution)
{
    if (distribution.totalEnergy <= 0.0)
        return 0.0;
    double weighted = 0.0;
    for (const auto& sample : distribution.samples)
    {
        if (sample.energy <= 0.0)
            continue;
        weighted += sample.axisPosition * sample.energy;
    }
    return weighted / distribution.totalEnergy;
}

double computeRolloff(const EnergyDistribution& distribution, double fraction)
{
    if (distribution.samples.empty() || distribution.totalEnergy <= 0.0)
        return 0.0;
    if (fraction <= 0.0)
        return distribution.samples.front().frequencyHz;
    if (fraction >= 1.0)
        return distribution.samples.back().frequencyHz;

    const double target = fraction * distribution.totalEnergy;
    double cumulative = 0.0;
    // samples are sorted by frequency ascending.
    for (const auto& sample : distribution.samples)
    {
        if (sample.energy <= 0.0)
            continue;
        cumulative += sample.energy;
        if (cumulative >= target)
            return sample.frequencyHz;
    }
    return distribution.samples.back().frequencyHz;
}

void writeCentroidHz(
    TimbreDescriptorValue& value,
    const TimbreDescriptorDomain domain,
    const EnergyDistribution& distribution,
    const ValidatedFrameSamples& samples,
    const std::vector<PartialEnergySample>& partialContrib,
    const std::vector<ResidualEnergySample>& residualContrib,
    const TimbreDescriptorStatus emptyStatus)
{
    value.domain = domain;
    if (distribution.totalEnergy <= 0.0)
    {
        value.value = 0.0;
        value.status = emptyStatus;
        value.confidence = 0.0;
        return;
    }
    const auto moments = computeMoments(distribution);
    value.value = moments.centroidHz;
    value.status = TimbreDescriptorStatus::Available;

    double confidenceSum = 0.0;
    for (const auto& sample : partialContrib)
        confidenceSum += sample.energy * sample.confidence;
    for (const auto& band : residualContrib)
        confidenceSum += band.energy * band.confidence;
    value.confidence = std::clamp(confidenceSum / distribution.totalEnergy, 0.0, 1.0);
    value.coverage.activePointCount = moments.activeCount;
    value.coverage.supportedFrequencySpanHz =
        distribution.maxFrequencyHz - distribution.minFrequencyHz;
    value.coverage.representedEnergyRatio = distribution.representedEnergyRatio;
    (void) samples;
}

} // namespace

void analyzeCentroidMomentsDescriptors(
    const ValidatedFrameSamples& samples,
    const TimbreDescriptorOptions& options,
    TimbreDescriptorFrame& frame)
{
    const auto partialDistribution =
        buildPartialEnergyDistribution(samples.partials, options.comparisonAxis);
    const auto residualDistribution =
        buildResidualEnergyDistribution(samples.residualBands, options.comparisonAxis);
    const auto combinedDistribution = buildCombinedEnergyDistribution(
        samples.partials, samples.residualBands, options.comparisonAxis);

    writeCentroidHz(frame.partialCentroidHz, TimbreDescriptorDomain::Partials,
                    partialDistribution, samples, samples.partials, {},
                    TimbreDescriptorStatus::EmptyInput);
    writeCentroidHz(frame.residualCentroidHz, TimbreDescriptorDomain::ResidualBands,
                    residualDistribution, samples, {}, samples.residualBands,
                    samples.residualBandsPresent
                        ? TimbreDescriptorStatus::EmptyInput
                        : TimbreDescriptorStatus::MissingResidual);
    writeCentroidHz(frame.combinedCentroidHz, TimbreDescriptorDomain::Combined,
                    combinedDistribution, samples, samples.partials,
                    samples.residualBands, TimbreDescriptorStatus::EmptyInput);

    // Axis-coordinate centroid (Bark/Mel/ERB/Linear depending on options).
    {
        auto& d = frame.combinedCentroidAxis;
        d.domain = TimbreDescriptorDomain::Combined;
        if (combinedDistribution.totalEnergy <= 0.0)
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::EmptyInput;
            d.confidence = 0.0;
        }
        else
        {
            d.value = computeAxisCentroid(combinedDistribution);
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = frame.combinedCentroidHz.confidence;
            d.coverage.activePointCount = frame.combinedCentroidHz.coverage.activePointCount;
            d.coverage.representedEnergyRatio = combinedDistribution.representedEnergyRatio;
            d.coverage.supportedFrequencySpanHz =
                combinedDistribution.maxFrequencyHz - combinedDistribution.minFrequencyHz;
        }
    }

    // Spread / skewness / kurtosis on the combined distribution. Sparse
    // distributions report TooSparse rather than emitting a 0/0 moment.
    {
        const auto moments = computeMoments(combinedDistribution);
        auto& spread = frame.combinedSpreadHz;
        auto& skew = frame.combinedSkewness;
        auto& kurt = frame.combinedKurtosis;
        spread.domain = TimbreDescriptorDomain::Combined;
        skew.domain = TimbreDescriptorDomain::Combined;
        kurt.domain = TimbreDescriptorDomain::Combined;

        if (combinedDistribution.totalEnergy <= 0.0)
        {
            spread.value = 0.0;
            spread.status = TimbreDescriptorStatus::EmptyInput;
            skew = spread;
            kurt = spread;
        }
        else if (!moments.spreadAvailable)
        {
            spread.value = 0.0;
            spread.status = TimbreDescriptorStatus::TooSparse;
            spread.confidence = frame.combinedCentroidHz.confidence;
            skew = spread;
            kurt = spread;
        }
        else
        {
            spread.value = moments.spreadHz;
            spread.status = TimbreDescriptorStatus::Available;
            spread.confidence = frame.combinedCentroidHz.confidence;
            spread.coverage = frame.combinedCentroidHz.coverage;

            if (moments.higherOrderAvailable)
            {
                skew.value = moments.skewness;
                skew.status = TimbreDescriptorStatus::Available;
                skew.confidence = spread.confidence;
                skew.coverage = spread.coverage;

                kurt.value = moments.kurtosis;
                kurt.status = TimbreDescriptorStatus::Available;
                kurt.confidence = spread.confidence;
                kurt.coverage = spread.coverage;
            }
            else
            {
                skew.value = 0.0;
                skew.status = TimbreDescriptorStatus::TooSparse;
                kurt = skew;
            }
        }
    }

    // Configurable rolloff. Default fractions are 0.85 and 0.95.
    {
        auto& low = frame.rolloff85Hz;
        auto& high = frame.rolloff95Hz;
        low.domain = TimbreDescriptorDomain::Combined;
        high.domain = TimbreDescriptorDomain::Combined;
        if (combinedDistribution.totalEnergy <= 0.0)
        {
            low.value = 0.0;
            low.status = TimbreDescriptorStatus::EmptyInput;
            low.confidence = 0.0;
            high = low;
        }
        else
        {
            low.value = computeRolloff(combinedDistribution, options.rolloffLowFraction);
            low.status = TimbreDescriptorStatus::Available;
            low.confidence = frame.combinedCentroidHz.confidence;
            low.coverage = frame.combinedCentroidHz.coverage;

            high.value = computeRolloff(combinedDistribution, options.rolloffHighFraction);
            high.status = TimbreDescriptorStatus::Available;
            high.confidence = low.confidence;
            high.coverage = low.coverage;
        }
    }
}

namespace
{

// Geometric mean of positive bins only. Skipping zero bins keeps flatness
// invariant to uniform gain (an additive floor would dominate small bins and
// break invariance) while still producing a finite value when at least one
// bin carries energy. Bins == 0 do not contribute to either mean.
double geometricMean(const std::vector<double>& bins, double /*floor*/)
{
    if (bins.empty())
        return 0.0;
    double logSum = 0.0;
    int count = 0;
    for (const auto value : bins)
    {
        if (value <= 0.0)
            continue;
        logSum += std::log(value);
        ++count;
    }
    if (count == 0)
        return 0.0;
    return std::exp(logSum / static_cast<double>(count));
}

double arithmeticMean(const std::vector<double>& bins, double /*floor*/)
{
    if (bins.empty())
        return 0.0;
    double sum = 0.0;
    int count = 0;
    for (const auto value : bins)
    {
        if (value <= 0.0)
            continue;
        sum += value;
        ++count;
    }
    if (count == 0)
        return 0.0;
    return sum / static_cast<double>(count);
}

double normalizedEntropy(const std::vector<double>& bins, double total)
{
    if (bins.empty() || total <= 0.0)
        return 0.0;
    double entropy = 0.0;
    for (const auto value : bins)
    {
        if (value <= 0.0)
            continue;
        const double probability = value / total;
        entropy -= probability * std::log(probability);
    }
    const double normalizer = std::log(static_cast<double>(bins.size()));
    if (normalizer <= 0.0)
        return 0.0;
    return std::clamp(entropy / normalizer, 0.0, 1.0);
}

double spectralDecreaseStatistic(const std::vector<double>& bins)
{
    if (bins.size() < 2)
        return 0.0;
    // Anchor on the first non-zero bin so sparse leading bins from a Bark
    // projection cannot zero-out an otherwise informative decrease.
    std::size_t anchor = 0;
    while (anchor < bins.size() && bins[anchor] <= 0.0)
        ++anchor;
    if (anchor + 1 >= bins.size())
        return 0.0;
    const double first = bins[anchor];
    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t k = anchor + 1; k < bins.size(); ++k)
    {
        const double diff = bins[k] - first;
        const double step = static_cast<double>(k - anchor);
        numerator += diff / step;
        denominator += bins[k];
    }
    if (denominator <= 0.0)
        return 0.0;
    return numerator / denominator;
}

double strongPeakRatioStatistic(const std::vector<double>& bins, double total)
{
    if (bins.empty() || total <= 0.0)
        return 0.0;
    double peak = 0.0;
    for (const auto value : bins)
        peak = std::max(peak, value);
    if (peak <= 0.0)
        return 0.0;
    return std::clamp(peak / total, 0.0, 1.0);
}

int countDominantBins(const std::vector<double>& bins, double thresholdRatio)
{
    if (bins.empty())
        return 0;
    double peak = 0.0;
    for (const auto value : bins)
        peak = std::max(peak, value);
    if (peak <= 0.0)
        return 0;
    const double threshold = peak * thresholdRatio;
    int count = 0;
    for (const auto value : bins)
    {
        if (value >= threshold && value > 0.0)
            ++count;
    }
    return count;
}

std::vector<double> spectrumFlatnessVector(
    const std::vector<double>& bins,
    int bandCount,
    double floor)
{
    std::vector<double> out;
    if (bins.empty() || bandCount <= 0)
        return out;
    const int effectiveBands = std::min<int>(bandCount, static_cast<int>(bins.size()));
    out.assign(static_cast<std::size_t>(effectiveBands), 0.0);
    const double total = static_cast<double>(bins.size());
    for (int band = 0; band < effectiveBands; ++band)
    {
        const std::size_t start = static_cast<std::size_t>(
            (static_cast<double>(band) * total) / static_cast<double>(effectiveBands));
        std::size_t end = static_cast<std::size_t>(
            (static_cast<double>(band + 1) * total) / static_cast<double>(effectiveBands));
        if (end <= start)
            end = start + 1;
        end = std::min(end, bins.size());

        std::vector<double> slice(bins.begin() + static_cast<std::ptrdiff_t>(start),
                                  bins.begin() + static_cast<std::ptrdiff_t>(end));
        const double gm = geometricMean(slice, floor);
        const double am = arithmeticMean(slice, floor);
        out[static_cast<std::size_t>(band)] = am > 0.0 ? std::clamp(gm / am, 0.0, 1.0) : 0.0;
    }
    return out;
}

} // namespace

void analyzeGridShapeDescriptors(
    const ValidatedFrameSamples& samples,
    const TimbreDescriptorOptions& options,
    const GridShapeOptions& shapeOptions,
    TimbreDescriptorFrame& frame)
{
    const int gridSize = std::max(options.comparisonGridSize, 2);
    const auto distribution = buildCombinedEnergyDistribution(
        samples.partials, samples.residualBands, options.comparisonAxis);
    const auto grid = projectEnergyOntoUniformGrid(
        distribution, options.comparisonAxis, gridSize);

    const auto setEmpty = [](TimbreDescriptorValue& value,
                              TimbreDescriptorDomain domain) {
        value.value = 0.0;
        value.domain = domain;
        value.status = TimbreDescriptorStatus::EmptyInput;
        value.confidence = 0.0;
    };

    const auto setTooSparse = [](TimbreDescriptorValue& value,
                                  TimbreDescriptorDomain domain) {
        value.value = 0.0;
        value.domain = domain;
        value.status = TimbreDescriptorStatus::TooSparse;
        value.confidence = 0.0;
    };

    if (grid.status != TimbreDescriptorStatus::Available
        || grid.totalEnergyAfter <= 0.0)
    {
        setEmpty(frame.flatness, TimbreDescriptorDomain::Combined);
        setEmpty(frame.crest, TimbreDescriptorDomain::Combined);
        setEmpty(frame.entropyNormalized, TimbreDescriptorDomain::Combined);
        setEmpty(frame.spectralDecrease, TimbreDescriptorDomain::Combined);
        setEmpty(frame.strongPeakRatio, TimbreDescriptorDomain::Combined);
        setEmpty(frame.spectralComplexity, TimbreDescriptorDomain::Combined);

        frame.spectrumFlatnessVector.values.clear();
        frame.spectrumFlatnessVector.domain = TimbreDescriptorDomain::Combined;
        frame.spectrumFlatnessVector.status = TimbreDescriptorStatus::EmptyInput;
        frame.spectrumFlatnessVector.confidence = 0.0;
        return;
    }

    const auto& bins = grid.bins;
    const double floor = std::max(shapeOptions.binFloor, 0.0);
    const double total = grid.totalEnergyAfter;
    const double am = arithmeticMean(bins, floor);
    const double gm = geometricMean(bins, floor);

    // flatness = geometric_mean / arithmetic_mean (in [0, 1] post-clamp).
    {
        auto& d = frame.flatness;
        d.domain = TimbreDescriptorDomain::Combined;
        if (am > 0.0)
        {
            d.value = std::clamp(gm / am, 0.0, 1.0);
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
            d.coverage.activePointCount = grid.activeBinCount;
            d.coverage.representedEnergyRatio = grid.representedEnergyRatio;
            d.coverage.supportedFrequencySpanHz =
                distribution.maxFrequencyHz - distribution.minFrequencyHz;
        }
        else
        {
            setTooSparse(d, TimbreDescriptorDomain::Combined);
        }
    }

    // crest = max / arithmetic_mean (>= 1 except when bins are all floor).
    {
        auto& d = frame.crest;
        d.domain = TimbreDescriptorDomain::Combined;
        if (am > 0.0)
        {
            double peak = 0.0;
            for (const auto value : bins)
                peak = std::max(peak, value);
            d.value = std::max(peak / am, 1.0);
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
            d.coverage = frame.flatness.coverage;
        }
        else
        {
            setTooSparse(d, TimbreDescriptorDomain::Combined);
        }
    }

    // entropy = -sum(p log p) / log(K)
    {
        auto& d = frame.entropyNormalized;
        d.domain = TimbreDescriptorDomain::Combined;
        d.value = normalizedEntropy(bins, total);
        d.status = TimbreDescriptorStatus::Available;
        d.confidence = 1.0;
        d.coverage = frame.flatness.coverage;
    }

    // spectral_decrease (Peeters)
    {
        auto& d = frame.spectralDecrease;
        d.domain = TimbreDescriptorDomain::Combined;
        if (grid.activeBinCount >= 2)
        {
            d.value = spectralDecreaseStatistic(bins);
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
            d.coverage = frame.flatness.coverage;
        }
        else
        {
            setTooSparse(d, TimbreDescriptorDomain::Combined);
        }
    }

    // strong_peak_ratio = max(bin) / total
    {
        auto& d = frame.strongPeakRatio;
        d.domain = TimbreDescriptorDomain::Combined;
        d.value = strongPeakRatioStatistic(bins, total);
        d.status = TimbreDescriptorStatus::Available;
        d.confidence = 1.0;
        d.coverage = frame.flatness.coverage;
    }

    // spectral_complexity = number of bins above relative threshold
    {
        auto& d = frame.spectralComplexity;
        d.domain = TimbreDescriptorDomain::Combined;
        d.value = static_cast<double>(
            countDominantBins(bins, shapeOptions.complexityThresholdRatio));
        d.status = TimbreDescriptorStatus::Available;
        d.confidence = 1.0;
        d.coverage = frame.flatness.coverage;
    }

    // spectrum_flatness_vector (MPEG-7-style per-band flatness)
    {
        auto& d = frame.spectrumFlatnessVector;
        d.domain = TimbreDescriptorDomain::Combined;
        d.values = spectrumFlatnessVector(bins, shapeOptions.flatnessVectorBands, floor);
        d.status = d.values.empty()
            ? TimbreDescriptorStatus::EmptyInput
            : TimbreDescriptorStatus::Available;
        d.confidence = d.values.empty() ? 0.0 : 1.0;
        d.coverage = frame.flatness.coverage;
    }
}

} // namespace ats

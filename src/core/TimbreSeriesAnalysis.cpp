#include "TimbreSeriesAnalysis.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace ats
{

namespace
{

bool sourceModelIdsMatch(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty())
        return true; // empty source id treated as wildcard for offline tools
    return a == b;
}

int resolveGridSize(const TimbreDescriptorOptions& options,
                     const TimbreSeriesAlignmentOptions& alignmentOptions)
{
    const int override = alignmentOptions.comparisonGridSizeOverride;
    return std::max(override > 0 ? override : options.comparisonGridSize, 2);
}

void normalizeGrid(std::vector<double>& grid,
                    SeriesFrameNormalization policy,
                    double& outRawEnergy)
{
    outRawEnergy = 0.0;
    for (const double bin : grid)
        outRawEnergy += bin;
    if (policy == SeriesFrameNormalization::None)
        return;
    if (outRawEnergy <= 0.0)
        return;
    double scale = 1.0;
    if (policy == SeriesFrameNormalization::L1)
    {
        scale = 1.0 / outRawEnergy;
    }
    else if (policy == SeriesFrameNormalization::L2)
    {
        double l2 = 0.0;
        for (const double bin : grid)
            l2 += bin * bin;
        l2 = std::sqrt(l2);
        if (l2 <= 0.0)
            return;
        scale = 1.0 / l2;
    }
    for (double& bin : grid)
        bin *= scale;
}

} // namespace

SeriesAlignmentReport validateSeriesAlignment(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreDescriptorSeriesOptions& seriesOptions,
    const TimbreSeriesAlignmentOptions& alignmentOptions)
{
    SeriesAlignmentReport report;
    report.frameCount = static_cast<int>(frames.size());
    report.frameHopSeconds = seriesOptions.frameHopSeconds;

    if (frames.empty())
    {
        report.status = TimbreDescriptorStatus::EmptyInput;
        return report;
    }

    if (static_cast<int>(frames.size()) < alignmentOptions.minimumFrameCount)
    {
        report.status = TimbreDescriptorStatus::TooSparse;
        return report;
    }

    const std::string firstSourceId = frames.front().samples.sourceModelId;
    const bool firstHasResidual = frames.front().samples.residualBandsPresent;
    const bool firstHasWaveform = frames.front().samples.waveformAvailable;
    (void) firstHasResidual;
    (void) firstHasWaveform;

    double previousTime = frames.front().timeSeconds;
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const auto& frame = frames[i];
        if (!sourceModelIdsMatch(firstSourceId, frame.samples.sourceModelId))
        {
            report.status = TimbreDescriptorStatus::Undefined;
            std::ostringstream oss;
            oss << "frame " << i
                << " sourceModelId '" << frame.samples.sourceModelId
                << "' does not match series source '" << firstSourceId << "'";
            report.errorMessage = oss.str();
            return report;
        }
        if (i > 0)
        {
            if (!(frame.timeSeconds > previousTime))
            {
                report.status = TimbreDescriptorStatus::Undefined;
                std::ostringstream oss;
                oss << "frame " << i << " time " << frame.timeSeconds
                    << " is not greater than previous time " << previousTime;
                report.errorMessage = oss.str();
                return report;
            }
            if (seriesOptions.frameHopSeconds > 0.0
                && alignmentOptions.frameHopToleranceFraction >= 0.0)
            {
                const double hop = frame.timeSeconds - previousTime;
                const double expected = seriesOptions.frameHopSeconds;
                const double tolerance = expected
                    * std::max(alignmentOptions.frameHopToleranceFraction, 0.0);
                if (std::abs(hop - expected) > tolerance)
                {
                    report.status = TimbreDescriptorStatus::Undefined;
                    std::ostringstream oss;
                    oss << "frame " << i << " hop " << hop
                        << " drifts beyond tolerance " << tolerance
                        << " around expected " << expected;
                    report.errorMessage = oss.str();
                    return report;
                }
            }
        }
        previousTime = frame.timeSeconds;
        if (frame.transientBoundary)
            ++report.transientBoundaryCount;
    }

    // axis sanity: comparisonAxis must be valid; reject ill-defined axis.
    if (!(options.comparisonAxis.maxFrequencyHz > options.comparisonAxis.minFrequencyHz))
    {
        report.status = TimbreDescriptorStatus::Undefined;
        report.errorMessage = "comparisonAxis range is empty or inverted";
        return report;
    }

    report.durationSeconds =
        frames.back().timeSeconds - frames.front().timeSeconds;
    report.status = TimbreDescriptorStatus::Available;
    return report;
}

SeriesGridStack buildSeriesGridStack(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreSeriesAlignmentOptions& alignmentOptions,
    TimbreDescriptorDomain domain)
{
    SeriesGridStack stack;
    stack.domain = domain;
    stack.normalization = alignmentOptions.frameNormalization;
    stack.gridSize = resolveGridSize(options, alignmentOptions);

    stack.grids.reserve(frames.size());
    stack.frameEnergyBeforeNormalization.reserve(frames.size());

    for (const auto& frame : frames)
    {
        EnergyDistribution distribution;
        switch (domain)
        {
            case TimbreDescriptorDomain::Partials:
                distribution = buildPartialEnergyDistribution(
                    frame.samples.partials, options.comparisonAxis);
                break;
            case TimbreDescriptorDomain::ResidualBands:
                distribution = buildResidualEnergyDistribution(
                    frame.samples.residualBands, options.comparisonAxis);
                break;
            case TimbreDescriptorDomain::Combined:
                distribution = buildCombinedEnergyDistribution(
                    frame.samples.partials,
                    frame.samples.residualBands,
                    options.comparisonAxis);
                break;
            default:
                stack.grids.clear();
                stack.frameEnergyBeforeNormalization.clear();
                return stack;
        }
        const auto projection = projectEnergyOntoUniformGrid(
            distribution, options.comparisonAxis, stack.gridSize);
        std::vector<double> bins = projection.bins;
        if (bins.empty())
            bins.assign(static_cast<std::size_t>(stack.gridSize), 0.0);
        double rawEnergy = 0.0;
        normalizeGrid(bins, alignmentOptions.frameNormalization, rawEnergy);
        stack.frameEnergyBeforeNormalization.push_back(rawEnergy);
        stack.grids.push_back(std::move(bins));
    }
    return stack;
}

std::vector<SeriesFrameContext> buildSeriesFrameContexts(
    const TimbreSeriesInput& series,
    const TimbreDescriptorOptions& options,
    const TimbreDescriptorSeriesOptions& seriesOptions,
    const std::vector<bool>& transientBoundaries)
{
    (void) options;
    std::vector<SeriesFrameContext> contexts;
    contexts.reserve(series.frames.size());
    const double hop = seriesOptions.frameHopSeconds > 0.0
        ? seriesOptions.frameHopSeconds
        : (series.frameHopSeconds > 0.0 ? series.frameHopSeconds : 0.0);

    for (std::size_t i = 0; i < series.frames.size(); ++i)
    {
        SeriesFrameContext context;
        context.samples = validateEnvelopeFrameInput(
            series.frames[i], /*amplitudeFloor=*/1.0e-12);
        context.timeSeconds = hop > 0.0
            ? static_cast<double>(i) * hop
            : static_cast<double>(i);
        context.transientBoundary = i < transientBoundaries.size()
            && transientBoundaries[i];
        contexts.push_back(std::move(context));
    }
    return contexts;
}

SeriesAlignmentReport analyzeTimbreDescriptorSeries(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreDescriptorSeriesOptions& seriesOptions,
    const TimbreSeriesAlignmentOptions& alignmentOptions,
    TimbreDescriptorSeries& output)
{
    output = {};
    if (!frames.empty())
        output.sourceModelId = frames.front().samples.sourceModelId;
    output.frameHopSeconds = seriesOptions.frameHopSeconds;

    const auto alignment = validateSeriesAlignment(
        frames, options, seriesOptions, alignmentOptions);

    if (alignment.status != TimbreDescriptorStatus::Available)
        return alignment;

    // Commit 15 wires only validation + populates the series scaffold so that
    // subsequent commits (flux, variation, temporal shape, partial tracks,
    // residual stationarity) fill in trajectories without re-running
    // alignment checks. The frames vector mirrors the input count so future
    // analyzers can write per-frame descriptor frames in place.
    output.frames.assign(frames.size(), {});
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        output.frames[i].sourceModelId = frames[i].samples.sourceModelId;
        output.frames[i].frameIndex = static_cast<std::uint64_t>(i);
    }
    return alignment;
}

namespace
{

double interpolatedCrossing(
    double targetEnergy,
    double leftEnergy,
    double rightEnergy,
    double leftTime,
    double rightTime)
{
    if (rightEnergy == leftEnergy)
        return rightTime;
    const double fraction = (targetEnergy - leftEnergy) / (rightEnergy - leftEnergy);
    return leftTime + std::clamp(fraction, 0.0, 1.0) * (rightTime - leftTime);
}

double findRisingCrossing(
    const std::vector<double>& energies,
    const std::vector<double>& times,
    std::size_t startIndex,
    std::size_t endIndex,
    double targetEnergy)
{
    if (endIndex >= energies.size())
        endIndex = energies.size() - 1;
    if (startIndex > endIndex)
        return times[startIndex];

    // Walk forward from startIndex; report the first sample at or above
    // targetEnergy. Interpolate from its predecessor when one exists.
    for (std::size_t i = startIndex; i <= endIndex; ++i)
    {
        if (energies[i] >= targetEnergy)
        {
            if (i == startIndex || energies[i - 1] >= targetEnergy)
                return times[i];
            return interpolatedCrossing(
                targetEnergy, energies[i - 1], energies[i],
                times[i - 1], times[i]);
        }
    }
    return times[endIndex];
}

double findFallingCrossing(
    const std::vector<double>& energies,
    const std::vector<double>& times,
    std::size_t startIndex,
    double targetEnergy)
{
    for (std::size_t i = startIndex + 1; i < energies.size(); ++i)
    {
        if (energies[i] <= targetEnergy)
        {
            return interpolatedCrossing(
                targetEnergy, energies[i - 1], energies[i],
                times[i - 1], times[i]);
        }
    }
    return times.back();
}

int countIndependentPeaks(
    const std::vector<double>& energies,
    double thresholdEnergy)
{
    int count = 0;
    bool inside = false;
    for (const double value : energies)
    {
        if (value > thresholdEnergy)
        {
            if (!inside)
            {
                ++count;
                inside = true;
            }
        }
        else
        {
            inside = false;
        }
    }
    return count;
}

double dbDifference(double currentEnergy, double referenceEnergy, double floor)
{
    const double safeCurrent = std::max(currentEnergy, floor);
    const double safeReference = std::max(referenceEnergy, floor);
    return 10.0 * std::log10(safeCurrent / safeReference);
}

} // namespace

TemporalEnvelopeDiagnostics analyzeTemporalEnvelopeShape(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TemporalEnvelopeOptions& temporalOptions,
    TimbreDescriptorSeries& series)
{
    (void) options;
    TemporalEnvelopeDiagnostics diagnostics;

    auto markStatus = [&](TimbreDescriptorStatus status) {
        const auto reset = [status](TimbreDescriptorValue& value) {
            value.value = 0.0;
            value.domain = TimbreDescriptorDomain::TemporalSeries;
            value.status = status;
            value.confidence = 0.0;
        };
        reset(series.temporalCentroidSeconds);
        reset(series.logAttackTime);
        reset(series.attackSlopeDbPerSecond);
        reset(series.decaySlopeDbPerSecond);
        reset(series.effectiveDurationSeconds);
    };

    if (static_cast<int>(frames.size()) < temporalOptions.minimumFrameCount)
    {
        markStatus(TimbreDescriptorStatus::TooSparse);
        return diagnostics;
    }

    std::vector<double> times;
    std::vector<double> energies;
    times.reserve(frames.size());
    energies.reserve(frames.size());
    for (const auto& frame : frames)
    {
        times.push_back(frame.timeSeconds);
        const double e = frame.samples.partialEnergyTotal
                       + frame.samples.residualEnergyTotal;
        energies.push_back(std::max(e, 0.0));
    }

    double peakEnergy = 0.0;
    std::size_t peakIndex = 0;
    for (std::size_t i = 0; i < energies.size(); ++i)
    {
        if (energies[i] > peakEnergy)
        {
            peakEnergy = energies[i];
            peakIndex = i;
        }
    }
    if (peakEnergy <= 0.0)
    {
        markStatus(TimbreDescriptorStatus::EmptyInput);
        return diagnostics;
    }

    diagnostics.peakEnergy = peakEnergy;
    diagnostics.peakTimeSeconds = times[peakIndex];

    const double floor = std::max(temporalOptions.energyFloor, 1.0e-30);
    auto thresholdEnergy = [peakEnergy, floor](double db) {
        const double linear = std::pow(10.0, db / 10.0);
        return std::max(peakEnergy * linear, floor);
    };

    const double effectiveThresholdEnergy =
        thresholdEnergy(temporalOptions.effectiveDurationThresholdDb);
    diagnostics.independentPeakCount =
        countIndependentPeaks(energies, effectiveThresholdEnergy);

    if (diagnostics.independentPeakCount > temporalOptions.maximumIndependentPeaks)
    {
        markStatus(TimbreDescriptorStatus::Undefined);
        return diagnostics;
    }

    // Effective duration boundaries.
    double effectiveStart = times.front();
    for (std::size_t i = 0; i <= peakIndex; ++i)
    {
        if (energies[i] >= effectiveThresholdEnergy)
        {
            effectiveStart = (i == 0)
                ? times[i]
                : interpolatedCrossing(effectiveThresholdEnergy,
                                        energies[i - 1], energies[i],
                                        times[i - 1], times[i]);
            break;
        }
    }
    const double effectiveEnd = findFallingCrossing(
        energies, times, peakIndex, effectiveThresholdEnergy);

    const double attackStartEnergy =
        thresholdEnergy(temporalOptions.attackStartThresholdDb);
    const double attackEndEnergy =
        thresholdEnergy(temporalOptions.attackEndThresholdDb);

    const double attackStart = findRisingCrossing(
        energies, times, 0, peakIndex, attackStartEnergy);
    const double attackEnd = findRisingCrossing(
        energies, times, 0, peakIndex, attackEndEnergy);

    diagnostics.attackStartSeconds = attackStart;
    diagnostics.attackEndSeconds = attackEnd;
    diagnostics.decayStartSeconds = times[peakIndex];
    diagnostics.decayEndSeconds = effectiveEnd;

    // log_attack_time
    {
        auto& d = series.logAttackTime;
        d.domain = TimbreDescriptorDomain::TemporalSeries;
        const double attackDuration = attackEnd - attackStart;
        if (attackDuration > 0.0)
        {
            d.value = std::log10(attackDuration);
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::TooSparse;
            d.confidence = 0.0;
        }
    }

    // attack_slope_db_per_second
    {
        auto& d = series.attackSlopeDbPerSecond;
        d.domain = TimbreDescriptorDomain::TemporalSeries;
        const double attackDuration = attackEnd - attackStart;
        if (attackDuration > 0.0)
        {
            const double startDb = dbDifference(
                std::max(attackStartEnergy, floor),
                peakEnergy, floor);
            const double endDb = dbDifference(
                std::max(attackEndEnergy, floor),
                peakEnergy, floor);
            d.value = (endDb - startDb) / attackDuration;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::TooSparse;
            d.confidence = 0.0;
        }
    }

    // decay_slope_db_per_second
    {
        auto& d = series.decaySlopeDbPerSecond;
        d.domain = TimbreDescriptorDomain::TemporalSeries;
        const double decayDuration = effectiveEnd - times[peakIndex];
        if (decayDuration > 0.0)
        {
            const double startDb = 0.0; // peak relative to peak
            const double endDb = dbDifference(
                std::max(effectiveThresholdEnergy, floor),
                peakEnergy, floor);
            d.value = (endDb - startDb) / decayDuration;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::TooSparse;
            d.confidence = 0.0;
        }
    }

    // effective_duration_seconds
    {
        auto& d = series.effectiveDurationSeconds;
        d.domain = TimbreDescriptorDomain::TemporalSeries;
        const double duration = effectiveEnd - effectiveStart;
        if (duration > 0.0)
        {
            d.value = duration;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::TooSparse;
            d.confidence = 0.0;
        }
    }

    // temporal_centroid_seconds: energy-weighted time centroid relative to the
    // first frame.
    {
        auto& d = series.temporalCentroidSeconds;
        d.domain = TimbreDescriptorDomain::TemporalSeries;
        double weightedSum = 0.0;
        double weightTotal = 0.0;
        const double baseline = times.front();
        for (std::size_t i = 0; i < energies.size(); ++i)
        {
            weightedSum += (times[i] - baseline) * energies[i];
            weightTotal += energies[i];
        }
        if (weightTotal > 0.0)
        {
            d.value = weightedSum / weightTotal;
            d.status = TimbreDescriptorStatus::Available;
            d.confidence = 1.0;
        }
        else
        {
            d.value = 0.0;
            d.status = TimbreDescriptorStatus::EmptyInput;
            d.confidence = 0.0;
        }
    }

    return diagnostics;
}

namespace
{

struct TrackSummary
{
    std::uint64_t trackId = 0;
    double peakAmplitude = 0.0;
    double integratedAmplitude = 0.0;
    double onsetSeconds = 0.0;
    double releaseSeconds = 0.0;
    bool valid = false;
};

double interpolateAmplitudeCrossing(
    const std::vector<PartialTrackSample>& samples,
    std::size_t leftIndex,
    std::size_t rightIndex,
    double targetAmplitude)
{
    const auto& left = samples[leftIndex];
    const auto& right = samples[rightIndex];
    if (right.amplitude == left.amplitude)
        return right.timeSeconds;
    const double fraction = (targetAmplitude - left.amplitude)
        / (right.amplitude - left.amplitude);
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    return left.timeSeconds + clamped * (right.timeSeconds - left.timeSeconds);
}

TrackSummary summarizeTrack(
    const PartialTrack& track,
    const PartialLifecycleOptions& options)
{
    TrackSummary summary;
    summary.trackId = track.trackId;
    if (track.samples.size() < 2)
        return summary;

    std::size_t peakIndex = 0;
    for (std::size_t i = 0; i < track.samples.size(); ++i)
    {
        const auto& sample = track.samples[i];
        if (sample.present && sample.amplitude > summary.peakAmplitude)
        {
            summary.peakAmplitude = sample.amplitude;
            peakIndex = i;
        }
        if (sample.present && sample.amplitude > 0.0)
            summary.integratedAmplitude += sample.amplitude;
    }
    if (summary.peakAmplitude <= 0.0
        || summary.integratedAmplitude < options.minimumTrackAmplitudeIntegral)
    {
        return summary;
    }

    const double onsetThreshold = summary.peakAmplitude
        * std::clamp(options.trackOnsetThresholdRatio, 0.0, 1.0);
    const double releaseThreshold = summary.peakAmplitude
        * std::clamp(options.trackReleaseThresholdRatio, 0.0, 1.0);

    // Onset: walk from first sample to the peak; report the first crossing of
    // onsetThreshold. Interpolate from the predecessor when available.
    summary.onsetSeconds = track.samples.front().timeSeconds;
    for (std::size_t i = 0; i <= peakIndex; ++i)
    {
        if (track.samples[i].amplitude >= onsetThreshold)
        {
            if (i == 0)
            {
                summary.onsetSeconds = track.samples[i].timeSeconds;
            }
            else
            {
                summary.onsetSeconds = interpolateAmplitudeCrossing(
                    track.samples, i - 1, i, onsetThreshold);
            }
            break;
        }
    }

    // Release: walk after the peak; the first crossing below releaseThreshold.
    summary.releaseSeconds = track.samples.back().timeSeconds;
    for (std::size_t i = peakIndex + 1; i < track.samples.size(); ++i)
    {
        if (track.samples[i].amplitude <= releaseThreshold)
        {
            summary.releaseSeconds = interpolateAmplitudeCrossing(
                track.samples, i - 1, i, releaseThreshold);
            break;
        }
    }
    summary.valid = true;
    return summary;
}

double weightedStandardDeviation(
    const std::vector<std::pair<double, double>>& offsetsAndWeights)
{
    double weightTotal = 0.0;
    double mean = 0.0;
    for (const auto& [offset, weight] : offsetsAndWeights)
    {
        weightTotal += weight;
        mean += offset * weight;
    }
    if (weightTotal <= 0.0)
        return 0.0;
    mean /= weightTotal;
    double variance = 0.0;
    for (const auto& [offset, weight] : offsetsAndWeights)
    {
        const double delta = offset - mean;
        variance += weight * delta * delta;
    }
    return std::sqrt(variance / weightTotal);
}

} // namespace

PartialLifecycleDiagnostics analyzePartialTrackLifecycle(
    const std::vector<PartialTrack>& tracks,
    const PartialLifecycleOptions& lifecycleOptions,
    TimbreDescriptorSeries& series)
{
    PartialLifecycleDiagnostics diagnostics;

    auto markStatus = [&](TimbreDescriptorStatus status) {
        const auto reset = [status](TimbreDescriptorValue& value) {
            value.value = 0.0;
            value.domain = TimbreDescriptorDomain::TemporalSeries;
            value.status = status;
            value.confidence = 0.0;
        };
        reset(series.partialOnsetAsynchronySeconds);
        reset(series.partialReleaseAsynchronySeconds);
    };

    if (tracks.empty())
    {
        markStatus(TimbreDescriptorStatus::EmptyInput);
        return diagnostics;
    }

    std::vector<TrackSummary> summaries;
    summaries.reserve(tracks.size());
    for (const auto& track : tracks)
    {
        const auto summary = summarizeTrack(track, lifecycleOptions);
        if (summary.valid)
            summaries.push_back(summary);
    }
    diagnostics.qualifyingTrackCount = static_cast<int>(summaries.size());

    if (static_cast<int>(summaries.size()) < lifecycleOptions.minimumTrackCount)
    {
        markStatus(TimbreDescriptorStatus::TooSparse);
        return diagnostics;
    }

    diagnostics.eventOnsetSeconds = summaries.front().onsetSeconds;
    diagnostics.eventReleaseSeconds = summaries.front().releaseSeconds;
    diagnostics.onsetReferenceTrackId = static_cast<int>(summaries.front().trackId);
    diagnostics.releaseReferenceTrackId = diagnostics.onsetReferenceTrackId;
    for (const auto& summary : summaries)
    {
        if (summary.onsetSeconds < diagnostics.eventOnsetSeconds)
        {
            diagnostics.eventOnsetSeconds = summary.onsetSeconds;
            diagnostics.onsetReferenceTrackId = static_cast<int>(summary.trackId);
        }
        if (summary.releaseSeconds > diagnostics.eventReleaseSeconds)
        {
            diagnostics.eventReleaseSeconds = summary.releaseSeconds;
            diagnostics.releaseReferenceTrackId = static_cast<int>(summary.trackId);
        }
    }

    std::vector<std::pair<double, double>> onsetOffsets;
    std::vector<std::pair<double, double>> releaseOffsets;
    onsetOffsets.reserve(summaries.size());
    releaseOffsets.reserve(summaries.size());
    for (const auto& summary : summaries)
    {
        const double weight = summary.integratedAmplitude * summary.integratedAmplitude;
        onsetOffsets.emplace_back(
            summary.onsetSeconds - diagnostics.eventOnsetSeconds, weight);
        releaseOffsets.emplace_back(
            summary.releaseSeconds - diagnostics.eventReleaseSeconds, weight);
    }

    const double onsetStddev = weightedStandardDeviation(onsetOffsets);
    const double releaseStddev = weightedStandardDeviation(releaseOffsets);

    {
        auto& d = series.partialOnsetAsynchronySeconds;
        d.value = onsetStddev;
        d.domain = TimbreDescriptorDomain::TemporalSeries;
        d.status = TimbreDescriptorStatus::Available;
        d.confidence = 1.0;
        d.coverage.activePointCount = diagnostics.qualifyingTrackCount;
    }
    {
        auto& d = series.partialReleaseAsynchronySeconds;
        d.value = releaseStddev;
        d.domain = TimbreDescriptorDomain::TemporalSeries;
        d.status = TimbreDescriptorStatus::Available;
        d.confidence = 1.0;
        d.coverage.activePointCount = diagnostics.qualifyingTrackCount;
    }

    return diagnostics;
}

namespace
{

struct ModulationSummary
{
    double rateHz = 0.0;
    double depthOrExtent = 0.0;
    double weight = 0.0;
    bool valid = false;
};

ModulationSummary estimateRateFromSeries(
    const std::vector<double>& times,
    const std::vector<double>& values,
    double nyquistHz)
{
    ModulationSummary summary;
    if (values.size() < 4 || times.size() != values.size())
        return summary;

    double mean = 0.0;
    for (const double v : values)
        mean += v;
    mean /= static_cast<double>(values.size());

    std::vector<double> detrended(values.size());
    double minDetrended = 0.0;
    double maxDetrended = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        detrended[i] = values[i] - mean;
        if (detrended[i] < minDetrended) minDetrended = detrended[i];
        if (detrended[i] > maxDetrended) maxDetrended = detrended[i];
    }
    summary.depthOrExtent = maxDetrended - minDetrended;

    int zeroCrossings = 0;
    for (std::size_t i = 1; i < detrended.size(); ++i)
    {
        const double prev = detrended[i - 1];
        const double cur = detrended[i];
        if ((prev <= 0.0 && cur > 0.0) || (prev >= 0.0 && cur < 0.0))
            ++zeroCrossings;
    }
    const double duration = times.back() - times.front();
    if (duration <= 0.0 || zeroCrossings == 0)
        return summary;

    summary.rateHz = static_cast<double>(zeroCrossings) / (2.0 * duration);
    if (nyquistHz > 0.0)
        summary.rateHz = std::min(summary.rateHz, nyquistHz);
    summary.weight = mean > 0.0 ? mean * mean : 0.0;
    summary.valid = true;
    return summary;
}

} // namespace

PartialModulationDiagnostics analyzePartialModulation(
    const std::vector<PartialTrack>& tracks,
    const PartialModulationOptions& modulationOptions,
    TimbreDescriptorSeries& series)
{
    PartialModulationDiagnostics diagnostics;
    diagnostics.seriesNyquistHz = modulationOptions.trajectoryRateHz > 0.0
        ? modulationOptions.trajectoryRateHz * 0.5
        : 0.0;

    auto markStatus = [&](TimbreDescriptorStatus status) {
        const auto reset = [status](TimbreDescriptorValue& value) {
            value.value = 0.0;
            value.domain = TimbreDescriptorDomain::TemporalSeries;
            value.status = status;
            value.confidence = 0.0;
        };
        reset(series.partialAmRateHz);
        reset(series.partialFmRateHz);
    };

    if (tracks.empty())
    {
        markStatus(TimbreDescriptorStatus::EmptyInput);
        return diagnostics;
    }
    if (modulationOptions.trajectoryRateHz <= 0.0)
    {
        markStatus(TimbreDescriptorStatus::TooSparse);
        return diagnostics;
    }

    double amRateNumerator = 0.0;
    double amWeightTotal = 0.0;
    double amDepthAccum = 0.0;
    double fmRateNumerator = 0.0;
    double fmWeightTotal = 0.0;
    double fmExtentAccum = 0.0;

    for (const auto& track : tracks)
    {
        std::vector<double> times;
        std::vector<double> amplitudes;
        std::vector<double> frequencies;
        times.reserve(track.samples.size());
        amplitudes.reserve(track.samples.size());
        frequencies.reserve(track.samples.size());
        for (const auto& sample : track.samples)
        {
            if (!sample.present)
                continue;
            if (!std::isfinite(sample.amplitude) || !std::isfinite(sample.frequencyHz))
                continue;
            times.push_back(sample.timeSeconds);
            amplitudes.push_back(sample.amplitude);
            frequencies.push_back(sample.frequencyHz);
        }
        if (static_cast<int>(amplitudes.size()) < modulationOptions.minimumSampleCount)
            continue;

        const auto am = estimateRateFromSeries(times, amplitudes, diagnostics.seriesNyquistHz);
        if (am.valid)
        {
            double meanAmp = 0.0;
            for (const double v : amplitudes)
                meanAmp += v;
            meanAmp /= static_cast<double>(amplitudes.size());
            const double relativeDepth = meanAmp > 0.0
                ? (am.depthOrExtent * 0.5) / meanAmp
                : 0.0;
            if (relativeDepth >= modulationOptions.minimumAmDepth)
            {
                amRateNumerator += am.rateHz * am.weight;
                amWeightTotal += am.weight;
                amDepthAccum += relativeDepth * am.weight;
                ++diagnostics.qualifyingAmTrackCount;
            }
        }
        const auto fm = estimateRateFromSeries(times, frequencies, diagnostics.seriesNyquistHz);
        if (fm.valid && fm.depthOrExtent >= modulationOptions.minimumFmExtentHz)
        {
            fmRateNumerator += fm.rateHz * fm.weight;
            fmWeightTotal += fm.weight;
            fmExtentAccum += fm.depthOrExtent * fm.weight;
            ++diagnostics.qualifyingFmTrackCount;
        }
    }

    auto reportRate = [&](TimbreDescriptorValue& value, double numerator,
                            double weightTotal, int qualifying)
    {
        value.domain = TimbreDescriptorDomain::TemporalSeries;
        if (qualifying < modulationOptions.minimumTrackCount || weightTotal <= 0.0)
        {
            value.value = 0.0;
            value.status = TimbreDescriptorStatus::TooSparse;
            value.confidence = 0.0;
        }
        else
        {
            value.value = numerator / weightTotal;
            value.status = TimbreDescriptorStatus::Available;
            value.confidence = 1.0;
            value.coverage.activePointCount = qualifying;
        }
    };
    reportRate(series.partialAmRateHz, amRateNumerator, amWeightTotal,
                diagnostics.qualifyingAmTrackCount);
    reportRate(series.partialFmRateHz, fmRateNumerator, fmWeightTotal,
                diagnostics.qualifyingFmTrackCount);

    if (amWeightTotal > 0.0)
        diagnostics.weightedAmDepth = amDepthAccum / amWeightTotal;
    if (fmWeightTotal > 0.0)
        diagnostics.weightedFmExtentHz = fmExtentAccum / fmWeightTotal;

    return diagnostics;
}

ResidualMotionDiagnostics analyzeResidualMotion(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreSeriesAlignmentOptions& alignmentOptions,
    const ResidualMotionOptions& motionOptions,
    TimbreDescriptorSeries& series)
{
    ResidualMotionDiagnostics diagnostics;
    auto& variation = series.residualSpectralVariation;
    auto& stationarity = series.residualStationarity;
    variation.domain = TimbreDescriptorDomain::TemporalSeries;
    stationarity.domain = TimbreDescriptorDomain::TemporalSeries;

    auto markStatus = [&](TimbreDescriptorStatus status) {
        variation.value = 0.0;
        variation.status = status;
        variation.confidence = 0.0;
        stationarity.value = 0.0;
        stationarity.status = status;
        stationarity.confidence = 0.0;
    };

    if (static_cast<int>(frames.size()) < motionOptions.minimumFrameCount)
    {
        markStatus(TimbreDescriptorStatus::TooSparse);
        return diagnostics;
    }

    // Detect whether any frame has residual bands.
    bool anyResidual = false;
    for (const auto& frame : frames)
    {
        if (frame.samples.residualBandsPresent)
        {
            anyResidual = true;
            break;
        }
    }
    if (!anyResidual)
    {
        markStatus(TimbreDescriptorStatus::MissingResidual);
        return diagnostics;
    }

    const auto stack = buildSeriesGridStack(
        frames, options, alignmentOptions, TimbreDescriptorDomain::ResidualBands);
    if (stack.grids.empty())
    {
        markStatus(TimbreDescriptorStatus::EmptyInput);
        return diagnostics;
    }

    // Per-frame variation -> series-level mean.
    double variationAccum = 0.0;
    int variationCount = 0;
    for (std::size_t i = 1; i < stack.grids.size(); ++i)
    {
        if (alignmentOptions.resetAtTransientBoundaries
            && frames[i].transientBoundary)
            continue;
        const auto& current = stack.grids[i];
        const auto& previous = stack.grids[i - 1];
        double dot = 0.0;
        double normCur = 0.0;
        double normPrev = 0.0;
        for (std::size_t k = 0; k < current.size(); ++k)
        {
            dot += current[k] * previous[k];
            normCur += current[k] * current[k];
            normPrev += previous[k] * previous[k];
        }
        const double normProduct = std::sqrt(normCur) * std::sqrt(normPrev);
        if (normProduct <= 0.0)
            continue;
        const double similarity = dot / normProduct;
        variationAccum += 1.0 - std::clamp(similarity, -1.0, 1.0);
        ++variationCount;
    }

    if (variationCount > 0)
    {
        variation.value = variationAccum / static_cast<double>(variationCount);
        variation.status = TimbreDescriptorStatus::Available;
        variation.confidence = 1.0;
        variation.coverage.activePointCount = variationCount;
    }
    else
    {
        variation.status = TimbreDescriptorStatus::EmptyInput;
        variation.confidence = 0.0;
    }
    diagnostics.meanVariation = variation.value;

    // Stationarity: lagged cosine similarity.
    const int lag = std::max(motionOptions.stationarityLagFrames, 1);
    double similarityAccum = 0.0;
    int similarityCount = 0;
    int totalPairs = 0;
    for (std::size_t i = static_cast<std::size_t>(lag); i < stack.grids.size(); ++i)
    {
        ++totalPairs;
        if (motionOptions.resetAtTransientBoundaries)
        {
            bool boundaryCrossed = false;
            for (std::size_t j = i + 1 - static_cast<std::size_t>(lag); j <= i; ++j)
            {
                if (frames[j].transientBoundary)
                {
                    boundaryCrossed = true;
                    break;
                }
            }
            if (boundaryCrossed)
                continue;
        }
        const auto& current = stack.grids[i];
        const auto& lagged = stack.grids[i - static_cast<std::size_t>(lag)];
        double dot = 0.0;
        double normCur = 0.0;
        double normLagged = 0.0;
        for (std::size_t k = 0; k < current.size(); ++k)
        {
            dot += current[k] * lagged[k];
            normCur += current[k] * current[k];
            normLagged += lagged[k] * lagged[k];
        }
        const double normProduct = std::sqrt(normCur) * std::sqrt(normLagged);
        if (normProduct <= 0.0)
            continue;
        similarityAccum += std::clamp(dot / normProduct, -1.0, 1.0);
        ++similarityCount;
    }

    diagnostics.contributingPairs = similarityCount;
    diagnostics.totalPairs = totalPairs;

    if (similarityCount > 0)
    {
        stationarity.value = similarityAccum / static_cast<double>(similarityCount);
        stationarity.status = TimbreDescriptorStatus::Available;
        stationarity.confidence = 1.0;
        stationarity.coverage.activePointCount = similarityCount;
    }
    else
    {
        stationarity.status = TimbreDescriptorStatus::EmptyInput;
        stationarity.confidence = 0.0;
    }

    return diagnostics;
}

void computeFluxAndVariationTrajectories(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreSeriesAlignmentOptions& alignmentOptions,
    TimbreDescriptorDomain domain,
    std::vector<TimbreDescriptorValue>& outFlux,
    std::vector<TimbreDescriptorValue>& outVariation)
{
    outFlux.assign(frames.size(), {});
    outVariation.assign(frames.size(), {});
    if (frames.empty())
        return;

    const auto stack = buildSeriesGridStack(
        frames, options, alignmentOptions, domain);
    if (stack.grids.size() != frames.size() || stack.gridSize <= 0)
    {
        for (auto& v : outFlux) v.status = TimbreDescriptorStatus::EmptyInput;
        for (auto& v : outVariation) v.status = TimbreDescriptorStatus::EmptyInput;
        return;
    }

    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        auto& flux = outFlux[i];
        auto& variation = outVariation[i];
        flux.domain = TimbreDescriptorDomain::TemporalSeries;
        variation.domain = TimbreDescriptorDomain::TemporalSeries;
        flux.value = 0.0;
        variation.value = 0.0;

        if (i == 0)
        {
            flux.status = TimbreDescriptorStatus::TooSparse;
            variation.status = TimbreDescriptorStatus::TooSparse;
            continue;
        }
        if (alignmentOptions.resetAtTransientBoundaries
            && frames[i].transientBoundary)
        {
            flux.status = TimbreDescriptorStatus::TooSparse;
            variation.status = TimbreDescriptorStatus::TooSparse;
            continue;
        }

        const auto& current = stack.grids[i];
        const auto& previous = stack.grids[i - 1];
        if (current.size() != previous.size() || current.empty())
        {
            flux.status = TimbreDescriptorStatus::EmptyInput;
            variation.status = TimbreDescriptorStatus::EmptyInput;
            continue;
        }

        double positiveFluxAccum = 0.0;
        double dot = 0.0;
        double normCurrent = 0.0;
        double normPrevious = 0.0;
        for (std::size_t k = 0; k < current.size(); ++k)
        {
            const double diff = current[k] - previous[k];
            const double positive = diff > 0.0 ? diff : 0.0;
            positiveFluxAccum += positive * positive;
            dot += current[k] * previous[k];
            normCurrent += current[k] * current[k];
            normPrevious += previous[k] * previous[k];
        }
        flux.value = std::sqrt(positiveFluxAccum);
        flux.status = TimbreDescriptorStatus::Available;
        flux.confidence = 1.0;
        flux.coverage.activePointCount = static_cast<int>(current.size());

        const double normProduct = std::sqrt(normCurrent) * std::sqrt(normPrevious);
        if (normProduct > 0.0)
        {
            const double similarity = dot / normProduct;
            variation.value = 1.0 - std::clamp(similarity, -1.0, 1.0);
            variation.status = TimbreDescriptorStatus::Available;
            variation.confidence = 1.0;
            variation.coverage.activePointCount = static_cast<int>(current.size());
        }
        else
        {
            variation.status = TimbreDescriptorStatus::EmptyInput;
            variation.confidence = 0.0;
        }
    }
}

} // namespace ats

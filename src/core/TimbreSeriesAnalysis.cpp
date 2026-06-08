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

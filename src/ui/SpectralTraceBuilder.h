#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace ats::ui
{

enum class SpectralTraceSource : std::uint8_t { current, sourceA, sourceB };
enum class SpectralTraceMode : std::uint8_t
{
    combined,
    partials,
    residual,
    isometric,
    isometricResidual,
    isometricEnvelope,
    isometricEnvelopeRdc
};

struct SpectralPartialFrame
{
    double frequencyHz = 0.0;
    double amplitude = 0.0;
};

struct SpectralPartialInput
{
    int partialIndex = -1;
    int frequencyShapeRank = 0;
    std::vector<SpectralPartialFrame> frames;
};

struct SpectralResidualBandInput
{
    int bandIndex = -1;
    double lowHz = 0.0;
    double highHz = 0.0;
    std::vector<double> energy;
};

struct SpectralTraceInput
{
    SpectralTraceSource source = SpectralTraceSource::current;
    std::uint64_t sourceRevision = 0;
    std::uint64_t displayShapeRevision = 0;
    double durationSeconds = 0.0;
    double displayAmpPeak = 0.0;
    double displayResidualPeak = 0.0;
    std::vector<SpectralPartialInput> partials;
    std::vector<SpectralResidualBandInput> residualBands;
};

struct SpectralTraceLimits
{
    std::size_t maxPartials = 900;
    std::size_t maxPointsPerPartial = 720;
    std::size_t maxResidualColumns = 220;
};

struct SpectralTracePoint
{
    std::uint32_t frameIndex = 0;
    float timeNorm = 0.0f;
    float freqNorm = 0.0f;
    float ampNorm = 0.0f;
    double frequencyHz = 0.0;
    double sourceFrequencyHz = 0.0;
    double amplitude = 0.0;
    bool active = false;
};

struct SpectralTrace
{
    int partialIndex = -1;
    float peakAmpNorm = 0.0f;
    double meanFrequencyHz = 0.0;
    double sourceMeanFrequencyHz = 0.0;
    int frequencyShapeRank = 0;
    std::vector<SpectralTracePoint> points;
};

struct SpectralResidualBandTrace
{
    int bandIndex = -1;
    double lowHz = 0.0;
    double highHz = 0.0;
    std::vector<float> energyNorm;
};

struct SpectralTraceSnapshot
{
    SpectralTraceSource source = SpectralTraceSource::current;
    SpectralTraceMode mode = SpectralTraceMode::combined;
    std::uint64_t sourceRevision = 0;
    std::uint64_t displayShapeRevision = 0;
    std::uint32_t partialCount = 0;
    std::uint32_t visiblePartialCount = 0;
    std::uint32_t frameCount = 0;
    double durationSeconds = 0.0;
    double minFrequencyHz = 20.0;
    double maxFrequencyHz = 20000.0;
    double ampDisplayReferencePeak = 0.0;
    double residualDisplayReferencePeak = 0.0;
    std::vector<SpectralTrace> partials;
    std::vector<SpectralResidualBandTrace> residualBands;

    [[nodiscard]] bool hasResidual() const noexcept { return !residualBands.empty(); }
};

class SpectralTraceBuilder
{
public:
    [[nodiscard]] static SpectralTraceSnapshot build(const SpectralTraceInput& input,
                                                      SpectralTraceMode mode = SpectralTraceMode::combined,
                                                      SpectralTraceLimits limits = {})
    {
        SpectralTraceSnapshot output;
        output.source = input.source;
        output.mode = mode;
        output.sourceRevision = input.sourceRevision;
        output.displayShapeRevision = input.displayShapeRevision;
        output.durationSeconds = finitePositive(input.durationSeconds) ? input.durationSeconds : 0.0;
        output.partialCount = static_cast<std::uint32_t>(input.partials.size());
        output.frameCount = frameCount(input);

        const auto partialsEnabled = mode != SpectralTraceMode::residual
                                  && mode != SpectralTraceMode::isometricResidual;
        const auto residualEnabled = mode == SpectralTraceMode::combined
                                  || mode == SpectralTraceMode::residual
                                  || mode == SpectralTraceMode::isometricResidual;

        struct Candidate
        {
            const SpectralPartialInput* input = nullptr;
            double peak = 0.0;
            double weightedHz = 0.0;
            double ampSum = 0.0;
            std::size_t activeFrames = 0;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(input.partials.size());
        double minHz = std::numeric_limits<double>::max();
        double maxHz = 0.0;
        double maxAmp = 0.0;
        for (const auto& partial : input.partials)
        {
            Candidate candidate { &partial };
            for (const auto& frame : partial.frames)
            {
                if (!activeFrame(frame))
                    continue;
                candidate.peak = std::max(candidate.peak, frame.amplitude);
                candidate.weightedHz += frame.frequencyHz * frame.amplitude;
                candidate.ampSum += frame.amplitude;
                ++candidate.activeFrames;
                minHz = std::min(minHz, frame.frequencyHz);
                maxHz = std::max(maxHz, frame.frequencyHz);
                maxAmp = std::max(maxAmp, frame.amplitude);
            }
            if (candidate.activeFrames > 0)
                candidates.push_back(candidate);
        }

        output.ampDisplayReferencePeak = finitePositive(input.displayAmpPeak)
            ? input.displayAmpPeak : maxAmp;
        output.residualDisplayReferencePeak = finitePositive(input.displayResidualPeak)
            ? input.displayResidualPeak : measuredResidualPeak(input);
        setFrequencyBounds(output, minHz, maxHz);

        const auto ampPeak = std::max(1.0e-15, output.ampDisplayReferencePeak);
        const double maxAmpDb = 20.0 * std::log10(ampPeak);
        const double minAmpDb = maxAmpDb - 72.0;
        const double ampDbRange = 72.0;
        const double logRange = std::log(output.maxFrequencyHz / output.minFrequencyHz);

        if (partialsEnabled)
        {
            std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                return a.peak > b.peak;
            });
            if (limits.maxPartials < candidates.size())
                candidates.resize(limits.maxPartials);
            std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                return a.peak < b.peak;
            });

            output.partials.reserve(candidates.size());
            for (const auto& candidate : candidates)
            {
                const auto& partial = *candidate.input;
                SpectralTrace trace;
                trace.partialIndex = partial.partialIndex;
                trace.frequencyShapeRank = partial.frequencyShapeRank;
                trace.sourceMeanFrequencyHz = candidate.ampSum > 0.0
                    ? candidate.weightedHz / candidate.ampSum : 0.0;
                trace.meanFrequencyHz = trace.sourceMeanFrequencyHz;
                trace.peakAmpNorm = clampUnit((20.0 * std::log10(std::max(1.0e-15, candidate.peak))
                                               - minAmpDb) / ampDbRange);

                const auto step = pointStep(partial.frames.size(), limits.maxPointsPerPartial);
                trace.points.reserve(partial.frames.size() / step + 2);
                for (std::size_t frame = 0; frame < partial.frames.size(); frame += step)
                    appendPoint(trace.points, partial.frames[frame], frame,
                                partial.frames.size(), output, minAmpDb, ampDbRange, logRange);
                if (!partial.frames.empty() && (partial.frames.size() - 1) % step != 0)
                    appendPoint(trace.points, partial.frames.back(), partial.frames.size() - 1,
                                partial.frames.size(), output, minAmpDb, ampDbRange, logRange);
                output.partials.push_back(std::move(trace));
            }
            output.visiblePartialCount = static_cast<std::uint32_t>(output.partials.size());
        }

        if (residualEnabled)
        {
            const auto columns = std::min(limits.maxResidualColumns,
                                          std::max<std::size_t>(1, maxResidualFrames(input)));
            const auto reference = std::max(1.0e-20, output.residualDisplayReferencePeak);
            const auto floorDb = 10.0 * std::log10(reference) - 72.0;
            output.residualBands.reserve(input.residualBands.size());
            for (const auto& band : input.residualBands)
            {
                SpectralResidualBandTrace result;
                result.bandIndex = band.bandIndex;
                result.lowHz = finitePositive(band.lowHz) ? band.lowHz : 0.0;
                result.highHz = finitePositive(band.highHz) ? band.highHz : result.lowHz;
                result.energyNorm.reserve(columns);
                for (std::size_t column = 0; column < columns; ++column)
                {
                    const auto begin = column * band.energy.size() / columns;
                    const auto end = (column + 1) * band.energy.size() / columns;
                    double peak = 0.0;
                    for (auto i = begin; i < std::max(begin, end); ++i)
                        if (std::isfinite(band.energy[i]) && band.energy[i] > 0.0)
                            peak = std::max(peak, band.energy[i]);
                    const auto db = 10.0 * std::log10(std::max(1.0e-20, peak));
                    result.energyNorm.push_back(clampUnit((db - floorDb) / 72.0));
                }
                output.residualBands.push_back(std::move(result));
            }
        }
        return output;
    }

private:
    [[nodiscard]] static bool finitePositive(double value) noexcept
    { return std::isfinite(value) && value > 0.0; }

    [[nodiscard]] static bool activeFrame(const SpectralPartialFrame& frame) noexcept
    { return finitePositive(frame.frequencyHz) && finitePositive(frame.amplitude); }

    [[nodiscard]] static float clampUnit(double value) noexcept
    {
        if (!std::isfinite(value)) return 0.0f;
        return static_cast<float>(std::clamp(value, 0.0, 1.0));
    }

    [[nodiscard]] static std::size_t frameCount(const SpectralTraceInput& input) noexcept
    {
        std::size_t result = 0;
        for (const auto& partial : input.partials)
            result = std::max(result, partial.frames.size());
        for (const auto& band : input.residualBands)
            result = std::max(result, band.energy.size());
        return result;
    }

    [[nodiscard]] static std::size_t maxResidualFrames(const SpectralTraceInput& input) noexcept
    {
        std::size_t result = 0;
        for (const auto& band : input.residualBands)
            result = std::max(result, band.energy.size());
        return result;
    }

    [[nodiscard]] static double measuredResidualPeak(const SpectralTraceInput& input) noexcept
    {
        double result = 0.0;
        for (const auto& band : input.residualBands)
            for (const auto value : band.energy)
                if (std::isfinite(value) && value > 0.0) result = std::max(result, value);
        return result;
    }

    static void setFrequencyBounds(SpectralTraceSnapshot& output, double minHz, double maxHz) noexcept
    {
        if (!finitePositive(minHz) || !finitePositive(maxHz) || maxHz <= minHz)
            return;
        const auto logMin = std::log(std::max(1.0, minHz));
        const auto logMax = std::log(std::max(minHz * 1.01, maxHz));
        const auto pad = std::max(0.04, (logMax - logMin) * 0.06);
        output.minFrequencyHz = std::max(1.0, std::exp(logMin - pad));
        output.maxFrequencyHz = std::max(output.minFrequencyHz * 1.2, std::exp(logMax + pad));
    }

    [[nodiscard]] static std::size_t pointStep(std::size_t frameCount,
                                                std::size_t limit) noexcept
    { return std::max<std::size_t>(1, (frameCount + std::max<std::size_t>(1, limit) - 1)
                                      / std::max<std::size_t>(1, limit)); }

    static void appendPoint(std::vector<SpectralTracePoint>& points,
                            const SpectralPartialFrame& frame,
                            std::size_t index,
                            std::size_t count,
                            const SpectralTraceSnapshot& output,
                            double minAmpDb,
                            double ampDbRange,
                            double logRange)
    {
        SpectralTracePoint point;
        point.frameIndex = static_cast<std::uint32_t>(index);
        point.timeNorm = count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
        point.frequencyHz = std::isfinite(frame.frequencyHz) ? frame.frequencyHz : 0.0;
        point.sourceFrequencyHz = point.frequencyHz;
        point.amplitude = std::isfinite(frame.amplitude) ? std::max(0.0, frame.amplitude) : 0.0;
        point.active = activeFrame(frame);
        if (point.active)
        {
            const auto clampedHz = std::clamp(point.frequencyHz,
                                              output.minFrequencyHz, output.maxFrequencyHz);
            point.freqNorm = clampUnit(std::log(clampedHz / output.minFrequencyHz) / logRange);
            point.ampNorm = clampUnit((20.0 * std::log10(std::max(1.0e-15, point.amplitude))
                                       - minAmpDb) / ampDbRange);
        }
        points.push_back(point);
    }
};

} // namespace ats::ui

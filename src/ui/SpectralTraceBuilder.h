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

enum class FrequencyShapeMode : std::uint8_t
{
    Linear, Stretch, Compress, Harmonic,
    // Lower-case aliases preserve the page-model API while the spectrum
    // snapshot mirrors the core ATS spelling.
    linear = Linear, stretch = Stretch, compress = Compress, harmonic = Harmonic
};

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

struct SpectralOnsetInput
{
    double normalizedTime = 0.0;
    double strength = 1.0;
    double preNormalizedTime = -1.0;
    double peakNormalizedTime = -1.0;
};

struct SpectralTraceInput
{
    SpectralTraceSource source = SpectralTraceSource::current;
    std::uint64_t sourceRevision = 0;
    std::uint64_t displayShapeRevision = 0;
    double durationSeconds = 0.0;
    double displayAmpPeak = 0.0;
    double displayResidualPeak = 0.0;
    // These are the dimensions of the source ATS_SOUND.  They are distinct
    // from the lengths of the diagnostic/display vectors: the legacy builder
    // uses the model dimensions for frame alignment and residual grouping.
    std::size_t partialCount = 0;
    std::size_t frameCount = 0;
    std::vector<SpectralPartialInput> partials;
    std::vector<SpectralResidualBandInput> residualBands;
    std::vector<SpectralOnsetInput> onsets;
    std::vector<double> smearnovDetectionCurve;
    std::vector<double> smearnovThresholdCurve;
    FrequencyShapeMode frequencyShapeMode = FrequencyShapeMode::Linear;
    double frequencyShapeAmount = 1.0;
    double frequencyShapePivotHz = 0.0;
    double resolvedFrequencyShapePivotHz = 0.0;
    double nyquistHz = 22050.0;
    std::vector<double> envelope;
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

struct SpectralOnset
{
    float norm = 0.0f;
    float preNorm = 0.0f;
    float peakNorm = 0.0f;
    float strength = 0.0f;
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
    std::vector<SpectralOnset> onsets;
    // Kept as a compatibility projection for older editor snapshots. New code
    // should use `onsets`, which preserves the attack-window handles.
    std::vector<float> onsetNorms;
    std::vector<float> smearnovDetectionCurve;
    std::vector<float> smearnovThresholdCurve;
    FrequencyShapeMode frequencyShapeMode = FrequencyShapeMode::Linear;
    double frequencyShapeAmount = 1.0;
    double frequencyShapePivotHz = 0.0;
    double resolvedFrequencyShapePivotHz = 0.0;
    std::vector<float> envelopeNorm;

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
        output.partialCount = static_cast<std::uint32_t>(input.partialCount != 0
            ? input.partialCount : input.partials.size());
        output.frameCount = frameCount(input);
        output.frequencyShapeMode = input.frequencyShapeMode;
        output.frequencyShapeAmount = std::isfinite(input.frequencyShapeAmount) ? input.frequencyShapeAmount : 1.0;
        output.frequencyShapePivotHz = std::isfinite(input.frequencyShapePivotHz) ? input.frequencyShapePivotHz : 0.0;
        output.resolvedFrequencyShapePivotHz = std::isfinite(input.resolvedFrequencyShapePivotHz)
            ? input.resolvedFrequencyShapePivotHz : 0.0;
        for (const auto value : input.smearnovDetectionCurve)
            output.smearnovDetectionCurve.push_back(static_cast<float>(value));
        for (const auto value : input.smearnovThresholdCurve)
            output.smearnovThresholdCurve.push_back(static_cast<float>(value));

        // Detector vectors are frame-aligned legacy data.  Keep their order
        // and length verbatim; filtering NaNs or sorting records changes the
        // time associated with every later curve/handle.
        output.onsets.reserve(input.onsets.size());
        for (const auto& onset : input.onsets)
            output.onsets.push_back({ static_cast<float>(onset.normalizedTime),
                                      static_cast<float>(onset.preNormalizedTime >= 0.0
                                          ? onset.preNormalizedTime : onset.normalizedTime),
                                      static_cast<float>(onset.peakNormalizedTime >= 0.0
                                          ? onset.peakNormalizedTime : onset.normalizedTime),
                                      static_cast<float>(onset.strength) });
        output.onsetNorms.reserve(output.onsets.size());
        for (const auto& onset : output.onsets)
            output.onsetNorms.push_back(onset.norm);
        output.envelopeNorm.reserve(input.envelope.size());
        for (const auto value : input.envelope)
            output.envelopeNorm.push_back(clampUnit(value));

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
                const auto displayHz = shapedFrequency(frame.frequencyHz,
                                                       partial.frequencyShapeRank, input);
                minHz = std::min(minHz, displayHz);
                maxHz = std::max(maxHz, displayHz);
                maxAmp = std::max(maxAmp, frame.amplitude);
            }
            if (candidate.activeFrames > 0)
                candidates.push_back(candidate);
        }

        output.ampDisplayReferencePeak = finitePositive(input.displayAmpPeak)
            ? input.displayAmpPeak : maxAmp;
        output.residualDisplayReferencePeak = finitePositive(input.displayResidualPeak)
            ? input.displayResidualPeak : measuredResidualPeak(input);
        setFrequencyBounds(output, minHz, maxHz, input.nyquistHz);

        const auto ampPeak = std::max(1.0e-15, output.ampDisplayReferencePeak);
        const double maxAmpDb = 20.0 * std::log10(ampPeak);
        const double minAmpDb = maxAmpDb - 72.0;
        const double ampDbRange = 72.0;
        const double logRange = std::log(output.maxFrequencyHz / output.minFrequencyHz);

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
                double weightedDisplayHz = 0.0;
                for (const auto& frame : partial.frames)
                    if (activeFrame(frame))
                        weightedDisplayHz += shapedFrequency(frame.frequencyHz,
                                                              partial.frequencyShapeRank, input)
                            * frame.amplitude;
                trace.meanFrequencyHz = candidate.ampSum > 0.0
                    ? weightedDisplayHz / candidate.ampSum : trace.sourceMeanFrequencyHz;
                trace.peakAmpNorm = clampFloor((20.0 * std::log10(std::max(1.0e-15, candidate.peak))
                                                - minAmpDb) / ampDbRange);

                const auto step = pointStep(output.frameCount, limits.maxPointsPerPartial);
                trace.points.reserve(partial.frames.size() / step + 2);
                for (std::size_t frame = 0; frame < partial.frames.size(); frame += step)
                    appendPoint(trace.points, partial.frames[frame], frame,
                                output.frameCount, output, partial.frequencyShapeRank,
                                input, minAmpDb, ampDbRange, logRange);
                if (!partial.frames.empty() && (partial.frames.size() - 1) % step != 0)
                    appendPoint(trace.points, partial.frames.back(), partial.frames.size() - 1,
                                output.frameCount, output, partial.frequencyShapeRank,
                                input, minAmpDb, ampDbRange, logRange);
                output.partials.push_back(std::move(trace));
            }
            output.visiblePartialCount = static_cast<std::uint32_t>(output.partials.size());
        }

        {
            // Columns partition the model's frames proportionally, so every
            // column holds at least one frame and the residual spans the
            // whole sound.  (The legacy ceil(frames / columns) grouping left
            // the tail columns silent: a 100-frame file drew its residual
            // over the first 45% of the time axis only.)
            const auto requested = std::clamp(limits.maxResidualColumns,
                                              std::size_t { 16 }, std::size_t { 512 });
            const auto columns = input.residualBands.empty() || output.frameCount == 0
                ? 0 : std::min<std::size_t>(requested, output.frameCount);
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
                    const auto [begin, end] = residualColumnFrames(column, columns,
                                                                   output.frameCount);
                    double peak = 0.0;
                    for (auto i = begin; i < end && i < band.energy.size(); ++i)
                        if (std::isfinite(band.energy[i]) && band.energy[i] > 0.0)
                            peak = std::max(peak, band.energy[i]);
                    const auto db = 10.0 * std::log10(std::max(1.0e-20, peak));
                    result.energyNorm.push_back(clampFloor((db - floorDb) / 72.0));
                }
                output.residualBands.push_back(std::move(result));
            }
        }
        return output;
    }

    /** Half-open frame range [first, last) that residual column `column` covers. */
    [[nodiscard]] static std::pair<std::size_t, std::size_t> residualColumnFrames(
        std::size_t column, std::size_t columns, std::size_t frames) noexcept
    {
        if (columns == 0 || frames == 0)
            return { 0, 0 };
        return { column * frames / columns, (column + 1) * frames / columns };
    }

    /**
        Normalised time of a residual column: the centre of the frames it
        covers, on the same frame/(frames-1) axis the partial points use, so
        residual ridges line up with partials, the playhead and selections.
    */
    [[nodiscard]] static float residualColumnTimeNorm(std::size_t column, std::size_t columns,
                                                      std::size_t frames) noexcept
    {
        if (columns == 0 || frames <= 1)
            return columns <= 1 ? 0.0f
                : static_cast<float>(column) / static_cast<float>(columns - 1);
        const auto [first, last] = residualColumnFrames(column, columns, frames);
        const auto centre = 0.5 * (static_cast<double>(first) + static_cast<double>(last) - 1.0);
        return static_cast<float>(std::clamp(centre / static_cast<double>(frames - 1), 0.0, 1.0));
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

    [[nodiscard]] static float clampFloor(double value) noexcept
    {
        if (!std::isfinite(value)) return 0.0f;
        return static_cast<float>(std::max(0.0, value));
    }

    [[nodiscard]] static double shapedFrequency(double hz, int rank,
                                                const SpectralTraceInput& input) noexcept
    {
        if (!finitePositive(hz) || input.frequencyShapeMode == FrequencyShapeMode::Linear)
            return hz;
        const auto amount = std::isfinite(input.frequencyShapeAmount)
            && input.frequencyShapeAmount > 0.0 ? input.frequencyShapeAmount : 1.0;
        const auto pivot = std::isfinite(input.resolvedFrequencyShapePivotHz)
            && input.resolvedFrequencyShapePivotHz > 0.0
            ? input.resolvedFrequencyShapePivotHz : input.frequencyShapePivotHz;
        if (!finitePositive(pivot)) return hz;
        const auto anchor = input.frequencyShapeMode == FrequencyShapeMode::Harmonic && rank > 0
            ? pivot * static_cast<double>(rank) : pivot;
        if (!finitePositive(anchor)) return hz;
        const auto warped = anchor * std::pow(hz / anchor, amount);
        const auto top = finitePositive(input.nyquistHz) ? input.nyquistHz * 0.99 : 20000.0;
        return std::clamp(warped, 1.0, std::max(1.0, top));
    }

    [[nodiscard]] static std::size_t frameCount(const SpectralTraceInput& input) noexcept
    {
        if (input.frameCount != 0)
            return input.frameCount;
        std::size_t result = 0;
        for (const auto& partial : input.partials)
            result = std::max(result, partial.frames.size());
        for (const auto& band : input.residualBands)
            result = std::max(result, band.energy.size());
        result = std::max(result, input.envelope.size());
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

    static void setFrequencyBounds(SpectralTraceSnapshot& output, double minHz, double maxHz,
                                   double nyquistHz) noexcept
    {
        if (!finitePositive(minHz) || !finitePositive(maxHz) || maxHz <= minHz)
        {
            output.minFrequencyHz = 20.0;
            output.maxFrequencyHz = std::max(20000.0,
                                             finitePositive(nyquistHz) ? nyquistHz : 0.0);
            return;
        }
        const auto logMin = std::log(std::max(1.0, minHz));
        const auto logMax = std::log(std::max(minHz * 1.01, maxHz));
        const auto pad = std::max(0.04, (logMax - logMin) * 0.06);
        output.minFrequencyHz = std::max(1.0, std::exp(logMin - pad));
        output.maxFrequencyHz = std::max(output.minFrequencyHz * 1.2, std::exp(logMax + pad));
    }

    [[nodiscard]] static std::size_t pointStep(std::size_t frameCount,
                                                std::size_t limit) noexcept
    {
        const auto effectiveLimit = std::max<std::size_t>(32, limit);
        return std::max<std::size_t>(1, (frameCount + effectiveLimit - 1) / effectiveLimit);
    }

    static void appendPoint(std::vector<SpectralTracePoint>& points,
                            const SpectralPartialFrame& frame,
                            std::size_t index,
                            std::size_t count,
                            const SpectralTraceSnapshot& output,
                            int frequencyShapeRank,
                            const SpectralTraceInput& input,
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
            point.frequencyHz = shapedFrequency(point.frequencyHz, frequencyShapeRank, input);
            const auto clampedHz = std::clamp(point.frequencyHz,
                                              output.minFrequencyHz, output.maxFrequencyHz);
            point.freqNorm = clampUnit(std::log(clampedHz / output.minFrequencyHz) / logRange);
            point.ampNorm = clampFloor((20.0 * std::log10(std::max(1.0e-15, point.amplitude))
                                        - minAmpDb) / ampDbRange);
        }
        points.push_back(point);
    }
};

} // namespace ats::ui

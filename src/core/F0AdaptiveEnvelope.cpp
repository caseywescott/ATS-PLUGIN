#include "F0AdaptiveEnvelope.h"

#include "FrequencyWarp.h"
#include "RegularizedCepstrum.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ats
{
namespace
{

void setOk(bool* ok, const bool value)
{
    if (ok)
        *ok = value;
}

bool validOptions(const F0AdaptiveEnvelopeOptions& options)
{
    return isValidFrequencyAxis(options.frequencyAxis) &&
           options.sampleCount >= 2 &&
           options.amplitudeFloor > 0.0 &&
           options.minF0Hz > 0.0 &&
           options.maxF0Hz > options.minF0Hz &&
           options.minF0Confidence >= 0.0 &&
           options.minF0Confidence <= 1.0 &&
           options.minSpectrumPointCount > 0 &&
           options.smoothingBandwidthInF0 > 0.0 &&
           options.minimumBandwidthHz > 0.0;
}

std::vector<EnvelopeSpectrumPoint> usableSpectrumPoints(const std::vector<EnvelopeSpectrumPoint>& points,
                                                        const EnvelopeAxisSpec& axis)
{
    std::vector<EnvelopeSpectrumPoint> usable;
    usable.reserve(points.size());
    for (const auto& point : points)
    {
        if (!std::isfinite(point.frequencyHz) || point.frequencyHz <= 0.0 ||
            !std::isfinite(point.amplitude))
        {
            continue;
        }

        bool positionOk = false;
        (void)normalizedPositionForFrequency(point.frequencyHz, axis, &positionOk);
        if (positionOk)
            usable.push_back({ point.frequencyHz, std::abs(point.amplitude) });
    }

    std::sort(usable.begin(), usable.end(), [](const auto& a, const auto& b) {
        return a.frequencyHz < b.frequencyHz;
    });
    return usable;
}

bool checkGate(const F0AdaptiveEnvelopeInput& input,
               const F0AdaptiveEnvelopeOptions& options,
               EnvelopeDiagnostics& diagnostics,
               std::string& errorMessage)
{
    if (!input.waveformAvailable)
    {
        diagnostics.fallbackReason = EnvelopeFallbackReason::Unsupported;
        diagnostics.addWarning(EnvelopeWarningFallback,
                               "F0-adaptive envelope requires waveform-derived spectrum data");
        errorMessage = "waveform spectrum is not available";
        return false;
    }

    if (!std::isfinite(input.fundamentalFrequencyHz) ||
        input.fundamentalFrequencyHz < options.minF0Hz ||
        input.fundamentalFrequencyHz > options.maxF0Hz ||
        !std::isfinite(input.f0Confidence) ||
        input.f0Confidence < options.minF0Confidence)
    {
        diagnostics.fallbackReason = EnvelopeFallbackReason::Unsupported;
        diagnostics.addWarning(EnvelopeWarningFallback,
                               "F0-adaptive envelope requires reliable F0 metadata");
        errorMessage = "reliable F0 is not available";
        return false;
    }

    return true;
}

double evaluateSmoothedLogSpectrum(const std::vector<EnvelopeSpectrumPoint>& spectrum,
                                   const double frequencyHz,
                                   const double f0Hz,
                                   const F0AdaptiveEnvelopeOptions& options)
{
    const double bandwidthHz = std::max(options.minimumBandwidthHz,
                                        options.smoothingBandwidthInF0 * f0Hz);
    const double invTwoSigmaSquared = 1.0 / (2.0 * bandwidthHz * bandwidthHz);
    double weightedLogSum = 0.0;
    double weightSum = 0.0;
    double nearestLog = 0.0;
    double nearestDistance = std::numeric_limits<double>::infinity();

    for (const auto& point : spectrum)
    {
        const double distanceHz = point.frequencyHz - frequencyHz;
        const double weight = std::exp(-(distanceHz * distanceHz) * invTwoSigmaSquared);
        const double logAmplitudeDb = amplitudeToDb(point.amplitude, options.amplitudeFloor);
        weightedLogSum += weight * logAmplitudeDb;
        weightSum += weight;

        const double absDistance = std::abs(distanceHz);
        if (absDistance < nearestDistance)
        {
            nearestDistance = absDistance;
            nearestLog = logAmplitudeDb;
        }
    }

    if (weightSum <= 1.0e-12)
        return nearestLog;

    return weightedLogSum / weightSum;
}

double weightedSpectrumError(const std::vector<EnvelopeSpectrumPoint>& spectrum,
                             const FrameEnvelope& envelope,
                             const F0AdaptiveEnvelopeOptions& options)
{
    double squaredError = 0.0;
    int count = 0;
    for (const auto& point : spectrum)
    {
        bool ok = false;
        const double estimated = evaluateF0AdaptiveEnvelope(envelope, point.frequencyHz, &ok);
        if (!ok)
            continue;

        const double expected = amplitudeToDb(point.amplitude, options.amplitudeFloor);
        const double error = estimated - expected;
        squaredError += error * error;
        ++count;
    }
    return count > 0 ? std::sqrt(squaredError / static_cast<double>(count)) : 0.0;
}

double evaluateSampledValues(const FrameEnvelope& envelope, const double frequencyHz, bool* ok)
{
    if (envelope.storage != EnvelopeValueStorage::Samples || envelope.values.empty())
    {
        setOk(ok, false);
        return 0.0;
    }

    if (envelope.values.size() == 1u)
    {
        setOk(ok, true);
        return envelope.values.front();
    }

    bool positionOk = false;
    const double position = normalizedPositionForFrequency(frequencyHz, envelope.frequencyAxis, &positionOk);
    if (!positionOk)
    {
        setOk(ok, false);
        return 0.0;
    }

    const double scaled = position * static_cast<double>(envelope.values.size() - 1u);
    const auto lo = static_cast<std::size_t>(std::floor(scaled));
    const auto hi = std::min(lo + 1u, envelope.values.size() - 1u);
    const double t = scaled - static_cast<double>(lo);
    setOk(ok, true);
    return envelope.values[lo] + t * (envelope.values[hi] - envelope.values[lo]);
}

} // namespace

bool fitF0AdaptiveEnvelope(const F0AdaptiveEnvelopeInput& input,
                           const F0AdaptiveEnvelopeOptions& options,
                           F0AdaptiveEnvelopeFitResult& result)
{
    result = {};
    if (!validOptions(options))
    {
        result.errorMessage = "invalid F0-adaptive envelope options";
        return false;
    }

    if (!checkGate(input, options, result.diagnostics, result.errorMessage))
        return false;

    const auto usable = usableSpectrumPoints(input.spectrum, options.frequencyAxis);
    result.diagnostics.fitPointCount = static_cast<int>(usable.size());
    if (static_cast<int>(usable.size()) < options.minSpectrumPointCount)
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame,
                                      "F0-adaptive envelope requires a dense waveform spectrum");
        result.errorMessage = "not enough waveform spectrum points";
        return false;
    }

    std::vector<double> values(static_cast<std::size_t>(options.sampleCount), 0.0);
    for (int i = 0; i < options.sampleCount; ++i)
    {
        bool frequencyOk = false;
        const double position = static_cast<double>(i) / static_cast<double>(options.sampleCount - 1);
        const double frequencyHz = frequencyForNormalizedPosition(position, options.frequencyAxis, &frequencyOk);
        if (!frequencyOk)
        {
            result.diagnostics.fallbackReason = EnvelopeFallbackReason::OutOfRange;
            result.diagnostics.addWarning(EnvelopeWarningOutOfRange,
                                          "could not evaluate F0-adaptive frequency grid");
            result.errorMessage = "could not evaluate frequency grid";
            return false;
        }

        values[static_cast<std::size_t>(i)] =
            evaluateSmoothedLogSpectrum(usable, frequencyHz, input.fundamentalFrequencyHz, options);
    }

    result.envelope.estimatorId = F0AdaptiveEnvelopeEstimator::kId;
    result.envelope.estimatorVersion = F0AdaptiveEnvelopeEstimator::kVersion;
    result.envelope.sourceModelId = input.sourceModelId;
    result.envelope.frameIndex = input.frameIndex;
    result.envelope.sampleRate = input.sampleRate;
    result.envelope.frequencyAxis = options.frequencyAxis;
    result.envelope.envelopeOrder = 0;
    result.envelope.regularization = input.fundamentalFrequencyHz;
    result.envelope.penaltyType = "f0_adaptive_waveform";
    result.envelope.storage = EnvelopeValueStorage::Samples;
    result.envelope.values = std::move(values);
    result.diagnostics.weightedError = weightedSpectrumError(usable, result.envelope, options);
    result.envelope.diagnostics = result.diagnostics;
    result.ok = true;
    return true;
}

double evaluateF0AdaptiveEnvelope(const FrameEnvelope& envelope, const double frequencyHz, bool* ok)
{
    return evaluateSampledValues(envelope, frequencyHz, ok);
}

F0AdaptiveEnvelopeEstimator::F0AdaptiveEnvelopeEstimator(F0AdaptiveEnvelopeOptions nextOptions)
    : options(std::move(nextOptions))
{
}

std::string F0AdaptiveEnvelopeEstimator::id() const
{
    return kId;
}

std::uint32_t F0AdaptiveEnvelopeEstimator::version() const
{
    return kVersion;
}

bool F0AdaptiveEnvelopeEstimator::prepare(const EnvelopeEstimatorContext& nextContext,
                                          std::string* errorMessage)
{
    if (nextContext.sampleRate <= 0.0)
    {
        if (errorMessage)
            *errorMessage = "sample rate must be positive";
        prepared = false;
        return false;
    }

    context = nextContext;
    if (!isValidFrequencyAxis(options.frequencyAxis))
        options.frequencyAxis = nextContext.frequencyAxis;
    if (options.frequencyAxis.sampleRate <= 0.0)
        options.frequencyAxis.sampleRate = nextContext.sampleRate;

    if (!validOptions(options))
    {
        if (errorMessage)
            *errorMessage = "F0-adaptive envelope options are invalid";
        prepared = false;
        return false;
    }

    lastDiagnostics = {};
    prepared = true;
    return true;
}

bool F0AdaptiveEnvelopeEstimator::estimate(const EnvelopeFrameInput& frame,
                                           FrameEnvelope& out,
                                           std::string* errorMessage)
{
    if (!prepared)
    {
        if (errorMessage)
            *errorMessage = "estimator is not prepared";
        return false;
    }

    F0AdaptiveEnvelopeInput input;
    input.sourceModelId = frame.sourceModelId;
    input.frameIndex = frame.frameIndex;
    input.sampleRate = frame.sampleRate > 0.0 ? frame.sampleRate : context.sampleRate;
    input.waveformAvailable = frame.waveformAvailable;
    input.fundamentalFrequencyHz = frame.fundamentalFrequencyHz;
    input.f0Confidence = frame.f0Confidence;
    input.spectrum = frame.waveformSpectrum;

    F0AdaptiveEnvelopeFitResult fit;
    if (!fitF0AdaptiveEnvelope(input, options, fit))
    {
        if (errorMessage)
            *errorMessage = fit.errorMessage;
        lastDiagnostics = fit.diagnostics;
        return false;
    }

    out = std::move(fit.envelope);
    lastDiagnostics = out.diagnostics;
    return true;
}

double F0AdaptiveEnvelopeEstimator::evaluate(const FrameEnvelope& envelope,
                                             const double frequencyHz,
                                             bool* ok) const
{
    if (envelope.estimatorId != kId)
    {
        setOk(ok, false);
        return 0.0;
    }
    return evaluateF0AdaptiveEnvelope(envelope, frequencyHz, ok);
}

EnvelopeDiagnostics F0AdaptiveEnvelopeEstimator::diagnostics() const
{
    return lastDiagnostics;
}

} // namespace ats

#include "LpcEnvelope.h"

#include "FrequencyWarp.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ats
{
namespace
{

constexpr double kPi = 3.141592653589793238462643383279502884;

void setOk(bool* ok, const bool value)
{
    if (ok)
        *ok = value;
}

struct PositionedPoint
{
    double position = 0.0;
    double logAmplitudeDb = 0.0;
    double weight = 1.0;
};

bool validOptions(const LpcEnvelopeEstimatorOptions& options)
{
    return isValidFrequencyAxis(options.frequencyAxis) &&
           options.sampleCount >= 4 &&
           options.order >= 1 &&
           options.regularization >= 0.0 &&
           options.amplitudeFloor > 0.0 &&
           options.maxReflectionCoefficient > 0.0 &&
           options.maxReflectionCoefficient < 1.0;
}

std::vector<PositionedPoint> sortedUsablePoints(const std::vector<RdcFitPoint>& points,
                                                const EnvelopeAxisSpec& axis)
{
    std::vector<PositionedPoint> usable;
    usable.reserve(points.size());
    for (const auto& point : points)
    {
        if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.logAmplitudeDb) ||
            !std::isfinite(point.weight) || point.weight <= 0.0)
        {
            continue;
        }

        bool positionOk = false;
        const double position = normalizedPositionForFrequency(point.frequencyHz, axis, &positionOk);
        if (positionOk)
            usable.push_back({ position, point.logAmplitudeDb, point.weight });
    }

    std::sort(usable.begin(), usable.end(), [](const auto& a, const auto& b) {
        return a.position < b.position;
    });
    return usable;
}

std::vector<double> interpolateLogSpectrum(const std::vector<PositionedPoint>& points,
                                           const int sampleCount)
{
    std::vector<double> spectrum(static_cast<std::size_t>(sampleCount), 0.0);
    if (points.empty())
        return spectrum;

    std::size_t right = 0;
    for (int i = 0; i < sampleCount; ++i)
    {
        const double position = static_cast<double>(i) / static_cast<double>(sampleCount - 1);
        while (right < points.size() && points[right].position < position)
            ++right;

        if (right == 0)
        {
            spectrum[static_cast<std::size_t>(i)] = points.front().logAmplitudeDb;
        }
        else if (right >= points.size())
        {
            spectrum[static_cast<std::size_t>(i)] = points.back().logAmplitudeDb;
        }
        else
        {
            const auto& lo = points[right - 1u];
            const auto& hi = points[right];
            const double span = std::max(1.0e-12, hi.position - lo.position);
            const double t = std::clamp((position - lo.position) / span, 0.0, 1.0);
            spectrum[static_cast<std::size_t>(i)] =
                lo.logAmplitudeDb + t * (hi.logAmplitudeDb - lo.logAmplitudeDb);
        }
    }
    return spectrum;
}

std::vector<double> autocorrelationFromLogSpectrum(const std::vector<double>& logSpectrumDb,
                                                  const int order,
                                                  const double amplitudeFloor)
{
    std::vector<double> autocorrelation(static_cast<std::size_t>(order + 1), 0.0);
    const double floorPower = amplitudeFloor * amplitudeFloor;
    const double n = static_cast<double>(logSpectrumDb.size());
    for (int lag = 0; lag <= order; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < logSpectrumDb.size(); ++i)
        {
            const double amplitude = std::pow(10.0, logSpectrumDb[i] / 20.0);
            const double power = std::max(amplitude * amplitude, floorPower);
            const double omega = kPi * static_cast<double>(i) /
                                 static_cast<double>(logSpectrumDb.size() - 1u);
            sum += power * std::cos(omega * static_cast<double>(lag));
        }
        autocorrelation[static_cast<std::size_t>(lag)] = sum / n;
    }
    return autocorrelation;
}

bool levinsonDurbin(const std::vector<double>& autocorrelation,
                    const int requestedOrder,
                    const double maxReflectionCoefficient,
                    std::vector<double>& predictor,
                    double& predictionError,
                    int& effectiveOrder,
                    EnvelopeDiagnostics& diagnostics)
{
    if (autocorrelation.empty() || autocorrelation[0] <= 0.0 || !std::isfinite(autocorrelation[0]))
        return false;

    const int order = std::min(requestedOrder, static_cast<int>(autocorrelation.size()) - 1);
    predictor.assign(static_cast<std::size_t>(order + 1), 0.0);
    predictor[0] = 1.0;
    predictionError = autocorrelation[0];
    effectiveOrder = 0;

    for (int i = 1; i <= order; ++i)
    {
        double sum = autocorrelation[static_cast<std::size_t>(i)];
        for (int j = 1; j < i; ++j)
            sum += predictor[static_cast<std::size_t>(j)] *
                   autocorrelation[static_cast<std::size_t>(i - j)];

        if (predictionError <= std::numeric_limits<double>::epsilon())
            return false;

        double reflection = -sum / predictionError;
        if (!std::isfinite(reflection))
            return false;

        const double reflectionLimit = maxReflectionCoefficient;
        if (std::abs(reflection) > reflectionLimit)
        {
            reflection = std::copysign(reflectionLimit, reflection);
            diagnostics.addWarning(EnvelopeWarningFallback,
                                   "LPC reflection coefficient was clamped for stability");
        }

        auto next = predictor;
        next[static_cast<std::size_t>(i)] = reflection;
        for (int j = 1; j < i; ++j)
        {
            next[static_cast<std::size_t>(j)] =
                predictor[static_cast<std::size_t>(j)] +
                reflection * predictor[static_cast<std::size_t>(i - j)];
        }

        predictor = std::move(next);
        predictionError *= std::max(1.0e-9, 1.0 - reflection * reflection);
        effectiveOrder = i;
    }

    predictor.resize(static_cast<std::size_t>(effectiveOrder + 1));
    return effectiveOrder > 0 && predictionError > 0.0 && std::isfinite(predictionError);
}

double evaluateAllPoleDb(const std::vector<double>& predictor,
                         const double predictionError,
                         const double position,
                         const double amplitudeFloor)
{
    const double omega = kPi * std::clamp(position, 0.0, 1.0);
    double real = 1.0;
    double imag = 0.0;
    for (std::size_t k = 1; k < predictor.size(); ++k)
    {
        real += predictor[k] * std::cos(-omega * static_cast<double>(k));
        imag += predictor[k] * std::sin(-omega * static_cast<double>(k));
    }

    const double denom = std::max(real * real + imag * imag, 1.0e-24);
    const double power = std::max(predictionError / denom, amplitudeFloor * amplitudeFloor);
    return 10.0 * std::log10(power);
}

double weightedMeanPointError(const std::vector<RdcFitPoint>& points,
                              const FrameEnvelope& envelope,
                              const double amplitudeFloor)
{
    double weightedSquaredError = 0.0;
    double weightSum = 0.0;
    for (const auto& point : points)
    {
        bool ok = false;
        const double actual = evaluateLpcEnvelope(envelope, point.frequencyHz, &ok);
        if (!ok)
            continue;

        const double error = actual - point.logAmplitudeDb;
        weightedSquaredError += std::max(0.0, point.weight) * error * error;
        weightSum += std::max(0.0, point.weight);
    }

    (void)amplitudeFloor;
    return weightSum > 0.0 ? std::sqrt(weightedSquaredError / weightSum) : 0.0;
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

bool fitLpcEnvelopeFromPoints(const std::vector<RdcFitPoint>& points,
                              const LpcEnvelopeEstimatorOptions& options,
                              LpcEnvelopeFitResult& result)
{
    result = {};
    if (!validOptions(options))
    {
        result.errorMessage = "invalid LPC envelope options";
        return false;
    }

    const auto usable = sortedUsablePoints(points, options.frequencyAxis);
    if (usable.empty())
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame, "no usable in-range fit points");
        result.errorMessage = "no usable fit points";
        return false;
    }

    result.diagnostics.fitPointCount = static_cast<int>(usable.size());
    if (static_cast<int>(usable.size()) < options.minFitPointCount ||
        static_cast<int>(usable.size()) < options.order * 2)
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame,
                                      "LPC all-pole fit has sparse support and may overfit isolated partials");
    }

    const double density = static_cast<double>(usable.size()) / static_cast<double>(options.sampleCount);
    if (density < options.sparseDensityWarningThreshold)
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame,
                                      "LPC all-pole fit is based on sparse interpolated bins");
    }

    const auto logSpectrum = interpolateLogSpectrum(usable, options.sampleCount);
    auto autocorrelation = autocorrelationFromLogSpectrum(logSpectrum,
                                                          options.order,
                                                          options.amplitudeFloor);
    autocorrelation[0] += std::max(0.0, options.regularization) *
                          std::max(autocorrelation[0], options.amplitudeFloor * options.amplitudeFloor);

    std::vector<double> predictor;
    double predictionError = 0.0;
    int effectiveOrder = 0;
    if (!levinsonDurbin(autocorrelation,
                        options.order,
                        options.maxReflectionCoefficient,
                        predictor,
                        predictionError,
                        effectiveOrder,
                        result.diagnostics))
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::IllConditioned;
        result.diagnostics.addWarning(EnvelopeWarningFallback, "LPC Levinson-Durbin solve failed");
        result.errorMessage = "LPC solve failed";
        return false;
    }

    std::vector<double> values(static_cast<std::size_t>(options.sampleCount), 0.0);
    for (int i = 0; i < options.sampleCount; ++i)
    {
        const double position = static_cast<double>(i) / static_cast<double>(options.sampleCount - 1);
        values[static_cast<std::size_t>(i)] =
            evaluateAllPoleDb(predictor, predictionError, position, options.amplitudeFloor);
    }

    double inputMean = 0.0;
    double outputMean = 0.0;
    for (int i = 0; i < options.sampleCount; ++i)
    {
        inputMean += logSpectrum[static_cast<std::size_t>(i)];
        outputMean += values[static_cast<std::size_t>(i)];
    }
    const double offset = (inputMean - outputMean) / static_cast<double>(options.sampleCount);
    for (auto& value : values)
        value += offset;

    result.effectiveOrder = effectiveOrder;
    result.predictorCoefficients = predictor;
    result.predictionError = predictionError;
    result.envelope.frequencyAxis = options.frequencyAxis;
    result.envelope.envelopeOrder = effectiveOrder;
    result.envelope.regularization = options.regularization;
    result.envelope.penaltyType = "lpc_all_pole";
    result.envelope.storage = EnvelopeValueStorage::Samples;
    result.envelope.values = std::move(values);
    result.envelope.diagnostics = result.diagnostics;
    result.diagnostics.weightedError = weightedMeanPointError(points, result.envelope, options.amplitudeFloor);
    result.envelope.diagnostics = result.diagnostics;
    result.ok = true;
    return true;
}

double evaluateLpcEnvelope(const FrameEnvelope& envelope, const double frequencyHz, bool* ok)
{
    return evaluateSampledValues(envelope, frequencyHz, ok);
}

LpcEnvelopeEstimator::LpcEnvelopeEstimator(LpcEnvelopeEstimatorOptions nextOptions)
    : options(std::move(nextOptions))
{
}

std::string LpcEnvelopeEstimator::id() const
{
    return kId;
}

std::uint32_t LpcEnvelopeEstimator::version() const
{
    return kVersion;
}

bool LpcEnvelopeEstimator::prepare(const EnvelopeEstimatorContext& nextContext, std::string* errorMessage)
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
    if (context.maxOrder > 0)
        options.order = std::min(options.order, context.maxOrder);

    if (!validOptions(options))
    {
        if (errorMessage)
            *errorMessage = "LPC envelope options are invalid";
        prepared = false;
        return false;
    }

    lastDiagnostics = {};
    prepared = true;
    return true;
}

bool LpcEnvelopeEstimator::estimate(const EnvelopeFrameInput& frame,
                                    FrameEnvelope& out,
                                    std::string* errorMessage)
{
    if (!prepared)
    {
        if (errorMessage)
            *errorMessage = "estimator is not prepared";
        return false;
    }

    EnvelopeDiagnostics pointDiagnostics;
    std::vector<RdcFitPoint> points;
    if (!buildRdcFitPointsFromPartials(frame, options.weights, points, &pointDiagnostics))
    {
        if (errorMessage)
            *errorMessage = "no usable weighted partial points";
        lastDiagnostics = pointDiagnostics;
        lastDiagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        lastDiagnostics.addWarning(EnvelopeWarningSparseFrame, "no usable weighted partial points");
        return false;
    }

    LpcEnvelopeFitResult fit;
    if (!fitLpcEnvelopeFromPoints(points, options, fit))
    {
        if (errorMessage)
            *errorMessage = fit.errorMessage;
        lastDiagnostics = fit.diagnostics;
        return false;
    }

    out = std::move(fit.envelope);
    out.estimatorId = id();
    out.estimatorVersion = version();
    out.sourceModelId = frame.sourceModelId;
    out.frameIndex = frame.frameIndex;
    out.sampleRate = frame.sampleRate > 0.0 ? frame.sampleRate : context.sampleRate;

    out.diagnostics.clippedOrFlooredCount += pointDiagnostics.clippedOrFlooredCount;
    out.diagnostics.warningFlags |= pointDiagnostics.warningFlags;
    out.diagnostics.warnings.insert(out.diagnostics.warnings.end(),
                                    pointDiagnostics.warnings.begin(),
                                    pointDiagnostics.warnings.end());
    lastDiagnostics = out.diagnostics;
    return true;
}

double LpcEnvelopeEstimator::evaluate(const FrameEnvelope& envelope,
                                      const double frequencyHz,
                                      bool* ok) const
{
    if (envelope.estimatorId != kId)
    {
        setOk(ok, false);
        return 0.0;
    }
    return evaluateLpcEnvelope(envelope, frequencyHz, ok);
}

EnvelopeDiagnostics LpcEnvelopeEstimator::diagnostics() const
{
    return lastDiagnostics;
}

} // namespace ats

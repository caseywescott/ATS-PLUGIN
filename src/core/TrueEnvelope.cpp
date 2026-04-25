#include "TrueEnvelope.h"

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

struct RasterizedSpectrum
{
    std::vector<double> values;
    std::vector<bool> observed;
    int observedBinCount = 0;
};

bool validOptions(const TrueEnvelopeEstimatorOptions& options)
{
    return isValidFrequencyAxis(options.frequencyAxis) &&
           options.sampleCount >= 2 &&
           options.lifterOrder >= 0 &&
           options.iterations > 0 &&
           options.convergenceDb >= 0.0;
}

std::vector<RdcFitPoint> usableFitPoints(const std::vector<RdcFitPoint>& points,
                                         const EnvelopeAxisSpec& axis)
{
    std::vector<RdcFitPoint> usable;
    usable.reserve(points.size());
    for (const auto& point : points)
    {
        if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.logAmplitudeDb) ||
            !std::isfinite(point.weight) || point.weight <= 0.0)
        {
            continue;
        }

        bool positionOk = false;
        (void)normalizedPositionForFrequency(point.frequencyHz, axis, &positionOk);
        if (positionOk)
            usable.push_back(point);
    }
    return usable;
}

bool rasterizeUpperContour(const std::vector<RdcFitPoint>& points,
                           const TrueEnvelopeEstimatorOptions& options,
                           RasterizedSpectrum& out)
{
    out = {};
    const auto sampleCount = static_cast<std::size_t>(options.sampleCount);
    out.values.assign(sampleCount, std::numeric_limits<double>::quiet_NaN());
    out.observed.assign(sampleCount, false);

    for (const auto& point : points)
    {
        bool positionOk = false;
        const double position = normalizedPositionForFrequency(point.frequencyHz, options.frequencyAxis, &positionOk);
        if (!positionOk)
            continue;

        const double scaled = position * static_cast<double>(sampleCount - 1u);
        const auto bin = static_cast<std::size_t>(std::clamp(std::lround(scaled),
                                                            0l,
                                                            static_cast<long>(sampleCount - 1u)));
        if (!out.observed[bin])
        {
            out.values[bin] = point.logAmplitudeDb;
            out.observed[bin] = true;
            ++out.observedBinCount;
        }
        else
        {
            out.values[bin] = std::max(out.values[bin], point.logAmplitudeDb);
        }
    }

    if (out.observedBinCount == 0)
        return false;

    std::size_t first = 0;
    while (first < sampleCount && !out.observed[first])
        ++first;

    for (std::size_t i = 0; i < first; ++i)
        out.values[i] = out.values[first];

    std::size_t left = first;
    while (left < sampleCount)
    {
        std::size_t right = left + 1u;
        while (right < sampleCount && !out.observed[right])
            ++right;

        if (right >= sampleCount)
        {
            for (std::size_t i = left + 1u; i < sampleCount; ++i)
                out.values[i] = out.values[left];
            break;
        }

        const double span = static_cast<double>(right - left);
        for (std::size_t i = left + 1u; i < right; ++i)
        {
            const double t = static_cast<double>(i - left) / span;
            out.values[i] = out.values[left] + t * (out.values[right] - out.values[left]);
        }
        left = right;
    }

    return true;
}

double lifterWeight(const int index, const int order, const TrueEnvelopeLifterWindow window)
{
    if (index == 0)
        return 1.0;
    if (index > order)
        return 0.0;
    if (window == TrueEnvelopeLifterWindow::Rectangular || order <= 0)
        return 1.0;

    return 0.54 + 0.46 * std::cos(kPi * static_cast<double>(index) / static_cast<double>(order));
}

std::vector<double> smoothByDctLifter(const std::vector<double>& input,
                                      const int lifterOrder,
                                      const TrueEnvelopeLifterWindow window)
{
    const auto n = input.size();
    std::vector<double> coefficients(n, 0.0);
    const int order = std::min(lifterOrder, static_cast<int>(n) - 1);

    for (std::size_t k = 0; k < n; ++k)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double angle = kPi * (static_cast<double>(i) + 0.5) * static_cast<double>(k) /
                                 static_cast<double>(n);
            sum += input[i] * std::cos(angle);
        }
        coefficients[k] = (2.0 / static_cast<double>(n)) * sum *
                          lifterWeight(static_cast<int>(k), order, window);
    }

    std::vector<double> output(n, coefficients[0] * 0.5);
    for (std::size_t i = 0; i < n; ++i)
    {
        double value = coefficients[0] * 0.5;
        for (int k = 1; k <= order; ++k)
        {
            const double angle = kPi * (static_cast<double>(i) + 0.5) * static_cast<double>(k) /
                                 static_cast<double>(n);
            value += coefficients[static_cast<std::size_t>(k)] * std::cos(angle);
        }
        output[i] = value;
    }
    return output;
}

double maxObservedUndershoot(const std::vector<double>& envelope,
                             const RasterizedSpectrum& spectrum)
{
    double undershoot = 0.0;
    for (std::size_t i = 0; i < envelope.size(); ++i)
    {
        if (spectrum.observed[i])
            undershoot = std::max(undershoot, spectrum.values[i] - envelope[i]);
    }
    return undershoot;
}

double rmsObservedError(const std::vector<double>& envelope,
                        const RasterizedSpectrum& spectrum)
{
    double squaredError = 0.0;
    int count = 0;
    for (std::size_t i = 0; i < envelope.size(); ++i)
    {
        if (!spectrum.observed[i])
            continue;

        const double error = envelope[i] - spectrum.values[i];
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

const char* toString(const TrueEnvelopeLifterWindow window)
{
    switch (window)
    {
        case TrueEnvelopeLifterWindow::Rectangular:
            return "rectangular_lifter";
        case TrueEnvelopeLifterWindow::Hamming:
            return "hamming_lifter";
    }
    return "hamming_lifter";
}

bool parseTrueEnvelopeLifterWindow(const std::string& text, TrueEnvelopeLifterWindow& out)
{
    if (text == "rectangular_lifter")
    {
        out = TrueEnvelopeLifterWindow::Rectangular;
        return true;
    }
    if (text == "hamming_lifter")
    {
        out = TrueEnvelopeLifterWindow::Hamming;
        return true;
    }
    return false;
}

bool fitTrueEnvelopeFromPoints(const std::vector<RdcFitPoint>& points,
                               const TrueEnvelopeEstimatorOptions& options,
                               TrueEnvelopeFitResult& result)
{
    result = {};
    if (!validOptions(options))
    {
        result.errorMessage = "invalid true-envelope options";
        return false;
    }

    const auto usable = usableFitPoints(points, options.frequencyAxis);
    if (usable.empty())
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame, "no usable in-range fit points");
        result.errorMessage = "no usable fit points";
        return false;
    }

    result.diagnostics.fitPointCount = static_cast<int>(usable.size());
    if (static_cast<int>(usable.size()) < options.minFitPointCount)
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame,
                                      "true-envelope fit has fewer points than the configured dense-spectrum minimum");
    }

    RasterizedSpectrum spectrum;
    if (!rasterizeUpperContour(usable, options, spectrum))
    {
        result.errorMessage = "could not rasterize fit points";
        return false;
    }

    const double observedDensity = static_cast<double>(spectrum.observedBinCount) /
                                   static_cast<double>(options.sampleCount);
    if (observedDensity < options.sparseDensityWarningThreshold)
    {
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame,
                                      "true-envelope fit is based on sparse observed bins");
    }

    std::vector<double> envelope = smoothByDctLifter(spectrum.values,
                                                     options.lifterOrder,
                                                     options.lifterWindow);

    for (int iteration = 0; iteration < options.iterations; ++iteration)
    {
        std::vector<double> lifted = envelope;
        for (std::size_t i = 0; i < lifted.size(); ++i)
        {
            if (spectrum.observed[i])
                lifted[i] = std::max(lifted[i], spectrum.values[i]);
        }

        envelope = smoothByDctLifter(lifted, options.lifterOrder, options.lifterWindow);
        const double undershoot = maxObservedUndershoot(envelope, spectrum);
        result.iterationsUsed = iteration + 1;
        if (undershoot <= options.convergenceDb)
            break;
    }

    const double undershoot = maxObservedUndershoot(envelope, spectrum);
    if (undershoot > options.convergenceDb)
    {
        for (std::size_t i = 0; i < envelope.size(); ++i)
        {
            if (spectrum.observed[i])
                envelope[i] = std::max(envelope[i], spectrum.values[i]);
        }
        result.diagnostics.addWarning(EnvelopeWarningFallback,
                                      "true-envelope iterations did not fully converge before final upper-contour clamp");
    }

    result.diagnostics.weightedError = rmsObservedError(envelope, spectrum);
    result.diagnostics.conditionEstimate = 0.0;
    result.envelope.frequencyAxis = options.frequencyAxis;
    result.envelope.envelopeOrder = std::min(options.lifterOrder, options.sampleCount - 1);
    result.envelope.regularization = 0.0;
    result.envelope.penaltyType = toString(options.lifterWindow);
    result.envelope.storage = EnvelopeValueStorage::Samples;
    result.envelope.values = std::move(envelope);
    result.envelope.diagnostics = result.diagnostics;
    result.ok = true;
    return true;
}

double evaluateTrueEnvelope(const FrameEnvelope& envelope, const double frequencyHz, bool* ok)
{
    return evaluateSampledValues(envelope, frequencyHz, ok);
}

TrueEnvelopeEstimator::TrueEnvelopeEstimator(TrueEnvelopeEstimatorOptions nextOptions)
    : options(std::move(nextOptions))
{
}

std::string TrueEnvelopeEstimator::id() const
{
    return kId;
}

std::uint32_t TrueEnvelopeEstimator::version() const
{
    return kVersion;
}

bool TrueEnvelopeEstimator::prepare(const EnvelopeEstimatorContext& nextContext, std::string* errorMessage)
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
        options.lifterOrder = std::min(options.lifterOrder, context.maxOrder);

    if (!validOptions(options))
    {
        if (errorMessage)
            *errorMessage = "true-envelope options are invalid";
        prepared = false;
        return false;
    }

    lastDiagnostics = {};
    prepared = true;
    return true;
}

bool TrueEnvelopeEstimator::estimate(const EnvelopeFrameInput& frame,
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

    TrueEnvelopeFitResult fit;
    if (!fitTrueEnvelopeFromPoints(points, options, fit))
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

double TrueEnvelopeEstimator::evaluate(const FrameEnvelope& envelope,
                                       const double frequencyHz,
                                       bool* ok) const
{
    if (envelope.estimatorId != kId)
    {
        setOk(ok, false);
        return 0.0;
    }
    return evaluateTrueEnvelope(envelope, frequencyHz, ok);
}

EnvelopeDiagnostics TrueEnvelopeEstimator::diagnostics() const
{
    return lastDiagnostics;
}

} // namespace ats

#include "RegularizedCepstrum.h"

#include "FrequencyWarp.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
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

// Fills a pre-allocated basis buffer [0..order] for frequencyHz on axis.
// out must have capacity >= order+1.  Returns false if the frequency is
// outside the axis range and leaves out in an unspecified state.
bool fillBasisForFrequency(const double frequencyHz,
                           const EnvelopeAxisSpec& axis,
                           const int order,
                           std::vector<double>& out)
{
    bool posOk = false;
    const double position = normalizedPositionForFrequency(frequencyHz, axis, &posOk);
    if (!posOk)
        return false;

    out[0] = 1.0;
    const double theta = kPi * position;
    for (int k = 1; k <= order; ++k)
        out[static_cast<std::size_t>(k)] = 2.0 * std::cos(static_cast<double>(k) * theta);
    return true;
}

// Kept for the evaluate path which only needs a single basis vector and is not
// on a hot path (one call per query, not N calls per frame).
std::vector<double> basisForFrequency(const double frequencyHz, const EnvelopeAxisSpec& axis, const int order, bool& ok)
{
    std::vector<double> basis(static_cast<std::size_t>(order + 1), 0.0);
    ok = fillBasisForFrequency(frequencyHz, axis, order, basis);
    return basis;
}

double penaltyDiagonal(const int coefficientIndex, const CepstrumPenalty penalty)
{
    if (coefficientIndex <= 0)
        return 0.0;

    const double k = static_cast<double>(coefficientIndex);
    switch (penalty)
    {
        case CepstrumPenalty::None:
            return 0.0;
        case CepstrumPenalty::FirstDerivative:
            return k * k;
        case CepstrumPenalty::SecondDerivative:
            return k * k * k * k;
    }
    return 0.0;
}

// Cholesky (L L^T) solve for a symmetric positive-definite system.
// matrix is stored as a flat row-major upper triangle (matrix[i*n+j] for i<=j),
// which avoids n separate heap allocations for the inner rows.
bool solveSymmetricPositiveDefinite(const std::vector<double>& matrix,
                                    const std::vector<double>& rhs,
                                    std::vector<double>& solution,
                                    double& pivotRatio)
{
    const std::size_t n = rhs.size();
    // Flat lower-triangular Cholesky factor: lower[i*n+j], j <= i.
    std::vector<double> lower(n * n, 0.0);
    double minPivot = std::numeric_limits<double>::infinity();
    double maxPivot = 0.0;

    for (std::size_t i = 0; i < n; ++i)
    {
        for (std::size_t j = 0; j <= i; ++j)
        {
            double sum = matrix[i * n + j];
            for (std::size_t k = 0; k < j; ++k)
                sum -= lower[i * n + k] * lower[j * n + k];

            if (i == j)
            {
                if (!std::isfinite(sum) || sum <= 1.0e-18)
                    return false;
                lower[i * n + j] = std::sqrt(sum);
                minPivot = std::min(minPivot, lower[i * n + j]);
                maxPivot = std::max(maxPivot, lower[i * n + j]);
            }
            else
            {
                lower[i * n + j] = sum / lower[j * n + j];
            }
        }
    }

    std::vector<double> y(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        double sum = rhs[i];
        for (std::size_t k = 0; k < i; ++k)
            sum -= lower[i * n + k] * y[k];
        y[i] = sum / lower[i * n + i];
    }

    solution.assign(n, 0.0);
    for (std::size_t ii = n; ii-- > 0;)
    {
        double sum = y[ii];
        for (std::size_t k = ii + 1; k < n; ++k)
            sum -= lower[k * n + ii] * solution[k];
        solution[ii] = sum / lower[ii * n + ii];
    }

    pivotRatio = minPivot > 0.0 ? maxPivot / minPivot : std::numeric_limits<double>::infinity();
    return true;
}

double evaluateCoefficients(const std::vector<double>& coefficients,
                            const EnvelopeAxisSpec& axis,
                            const double frequencyHz,
                            bool& ok)
{
    if (coefficients.empty())
    {
        ok = false;
        return 0.0;
    }

    const int order = static_cast<int>(coefficients.size()) - 1;
    const auto basis = basisForFrequency(frequencyHz, axis, order, ok);
    if (!ok)
        return 0.0;

    return std::inner_product(coefficients.begin(), coefficients.end(), basis.begin(), 0.0);
}

} // namespace

const char* toString(const CepstrumPenalty penalty)
{
    switch (penalty)
    {
        case CepstrumPenalty::None:
            return "none";
        case CepstrumPenalty::FirstDerivative:
            return "first_derivative";
        case CepstrumPenalty::SecondDerivative:
            return "second_derivative";
    }
    return "second_derivative";
}

bool parseCepstrumPenalty(const std::string& text, CepstrumPenalty& out)
{
    if (text == "none")
    {
        out = CepstrumPenalty::None;
        return true;
    }
    if (text == "first_derivative")
    {
        out = CepstrumPenalty::FirstDerivative;
        return true;
    }
    if (text == "second_derivative")
    {
        out = CepstrumPenalty::SecondDerivative;
        return true;
    }
    return false;
}

double amplitudeToDb(const double amplitude, const double floor)
{
    const double safeFloor = std::max(floor, std::numeric_limits<double>::min());
    return 20.0 * std::log10(std::max(std::abs(amplitude), safeFloor));
}

double computeAtsPartialWeight(const EnvelopePartialPoint& partial,
                               const AtsPartialWeightOptions& options,
                               bool* usable)
{
    if (options.useSelection && !partial.selected)
    {
        setOk(usable, false);
        return 0.0;
    }

    if (!std::isfinite(partial.frequencyHz) || partial.frequencyHz <= 0.0 ||
        !std::isfinite(partial.amplitude))
    {
        setOk(usable, false);
        return 0.0;
    }

    double weight = 1.0;
    if (options.useSmr)
    {
        const double span = std::max(1.0e-9, options.maxSmrDb - options.minSmrDb);
        const double smrNorm = std::clamp((partial.smr - options.minSmrDb) / span, 0.0, 1.0);
        weight *= smrNorm;
    }

    if (options.useAmplitude)
    {
        const double db = amplitudeToDb(partial.amplitude, options.amplitudeFloor);
        const double span = std::max(1.0e-9, options.maxAmplitudeDb - options.minAmplitudeDb);
        const double ampNorm = std::clamp((db - options.minAmplitudeDb) / span, 0.0, 1.0);
        weight *= ampNorm;
    }

    if (options.useConfidence)
        weight *= std::clamp(partial.confidence, 0.0, 1.0);

    if (!std::isfinite(weight) || weight <= 0.0)
    {
        setOk(usable, false);
        return 0.0;
    }

    setOk(usable, true);
    return std::clamp(weight, std::max(0.0, options.minWeight), std::max(options.minWeight, options.maxWeight));
}

bool buildRdcFitPointsFromPartials(const EnvelopeFrameInput& frame,
                                   const AtsPartialWeightOptions& options,
                                   std::vector<RdcFitPoint>& out,
                                   EnvelopeDiagnostics* diagnostics)
{
    out.clear();
    if (diagnostics)
        *diagnostics = {};

    for (const auto& partial : frame.partials)
    {
        bool usable = false;
        const double weight = computeAtsPartialWeight(partial, options, &usable);
        if (!usable)
            continue;

        double amplitude = std::abs(partial.amplitude);
        if (amplitude < options.amplitudeFloor)
        {
            amplitude = options.amplitudeFloor;
            if (diagnostics)
            {
                ++diagnostics->clippedOrFlooredCount;
                diagnostics->warningFlags |= EnvelopeWarningAmplitudeFloor;
            }
        }

        out.push_back({ partial.frequencyHz, amplitudeToDb(amplitude, options.amplitudeFloor), weight });
    }

    if (diagnostics && diagnostics->clippedOrFlooredCount > 0)
        diagnostics->warnings.push_back("one or more partial amplitudes were floored before log conversion");

    return !out.empty();
}

namespace
{

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

double weightedMeanDb(const std::vector<RdcFitPoint>& points)
{
    double weightedSum = 0.0;
    double weightSum = 0.0;
    for (const auto& point : points)
    {
        weightedSum += point.weight * point.logAmplitudeDb;
        weightSum += point.weight;
    }
    return weightSum > 0.0 ? weightedSum / weightSum : 0.0;
}

bool solveCepstrumAttempt(const std::vector<RdcFitPoint>& points,
                          const RdcFitOptions& options,
                          const int order,
                          const double regularization,
                          std::vector<double>& coefficients,
                          EnvelopeDiagnostics& diagnostics,
                          std::string& errorMessage)
{
    const std::size_t n = static_cast<std::size_t>(order + 1);
    // Flat row-major symmetric matrix (upper triangle populated, then mirrored).
    std::vector<double> matrix(n * n, 0.0);
    std::vector<double> rhs(n, 0.0);

    // Pre-allocate one basis buffer and reuse it for every partial point,
    // avoiding O(N_partials) heap allocations in the inner loop.
    std::vector<double> basis(n, 0.0);

    double weightSum = 0.0;
    diagnostics.fitPointCount = 0;
    for (const auto& point : points)
    {
        if (!fillBasisForFrequency(point.frequencyHz, options.frequencyAxis, order, basis))
            continue;

        ++diagnostics.fitPointCount;
        weightSum += point.weight;
        for (std::size_t row = 0; row < n; ++row)
        {
            const double weightedBasis = point.weight * basis[row];
            rhs[row] += weightedBasis * point.logAmplitudeDb;
            for (std::size_t col = 0; col <= row; ++col)
                matrix[row * n + col] += weightedBasis * basis[col];
        }
    }

    if (diagnostics.fitPointCount == 0 || weightSum <= 0.0)
    {
        errorMessage = "no usable fit points";
        return false;
    }

    // Mirror lower triangle to upper and add regularization on diagonal.
    for (std::size_t row = 0; row < n; ++row)
    {
        for (std::size_t col = 0; col < row; ++col)
            matrix[col * n + row] = matrix[row * n + col];

        matrix[row * n + row] +=
            std::max(0.0, options.ridge) +
            std::max(0.0, regularization) * penaltyDiagonal(static_cast<int>(row), options.penalty);
    }

    double pivotRatio = 0.0;
    if (!solveSymmetricPositiveDefinite(matrix, rhs, coefficients, pivotRatio))
    {
        errorMessage = "cepstrum normal equations were not positive definite";
        return false;
    }

    double weightedSquaredError = 0.0;
    for (const auto& point : points)
    {
        bool evalOk = false;
        const double predicted = evaluateCoefficients(coefficients, options.frequencyAxis, point.frequencyHz, evalOk);
        if (!evalOk)
            continue;
        const double residual = predicted - point.logAmplitudeDb;
        weightedSquaredError += point.weight * residual * residual;
    }

    diagnostics.weightedError = std::sqrt(weightedSquaredError / weightSum);
    diagnostics.conditionEstimate = pivotRatio * pivotRatio;
    return true;
}

void finishFitResult(RdcFitResult& result,
                     const RdcFitOptions& options,
                     const int effectiveOrder,
                     const double effectiveRegularization,
                     std::vector<double> coefficients)
{
    result.effectiveOrder = effectiveOrder;
    result.envelope.frequencyAxis = options.frequencyAxis;
    result.envelope.envelopeOrder = effectiveOrder;
    result.envelope.regularization = effectiveRegularization;
    result.envelope.penaltyType = toString(options.penalty);
    result.envelope.storage = EnvelopeValueStorage::Coefficients;
    result.envelope.values = std::move(coefficients);
    result.envelope.diagnostics = result.diagnostics;
    result.ok = true;
}

void finishConstantFallback(RdcFitResult& result,
                            const RdcFitOptions& options,
                            const double valueDb,
                            const EnvelopeFallbackReason reason,
                            const std::string& warning)
{
    result.effectiveOrder = 0;
    result.diagnostics.fallbackReason = reason;
    result.diagnostics.addWarning(EnvelopeWarningFallback, warning);
    result.envelope.frequencyAxis = options.frequencyAxis;
    result.envelope.envelopeOrder = 0;
    result.envelope.regularization = 0.0;
    result.envelope.penaltyType = "constant_fallback";
    result.envelope.storage = EnvelopeValueStorage::Samples;
    result.envelope.values = { valueDb };
    result.envelope.diagnostics = result.diagnostics;
    result.ok = true;
}

} // namespace

bool fitRegularizedDiscreteCepstrum(const std::vector<RdcFitPoint>& points,
                                    const RdcFitOptions& options,
                                    RdcFitResult& result)
{
    result = {};

    if (!isValidFrequencyAxis(options.frequencyAxis))
    {
        result.errorMessage = "invalid frequency axis";
        return false;
    }
    if (options.order < 0)
    {
        result.errorMessage = "cepstrum order must be non-negative";
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

    int effectiveOrder = options.order;
    double effectiveRegularization = options.regularization;
    if (options.reduceOrderForSparseFrames && static_cast<int>(usable.size()) <= effectiveOrder)
    {
        effectiveOrder = std::max(0, static_cast<int>(usable.size()) - 1);
        effectiveRegularization = std::max(effectiveRegularization, options.sparseRegularizationFloor);
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        result.diagnostics.addWarning(EnvelopeWarningSparseFrame, "cepstrum order reduced for sparse frame");
    }

    std::vector<double> coefficients;
    EnvelopeDiagnostics attemptDiagnostics;
    std::string attemptError;
    bool solved = solveCepstrumAttempt(usable,
                                       options,
                                       effectiveOrder,
                                       effectiveRegularization,
                                       coefficients,
                                       attemptDiagnostics,
                                       attemptError);

    const bool retryForCondition = solved &&
        attemptDiagnostics.conditionEstimate > options.illConditionedThreshold &&
        options.retryIllConditionedWithRegularization;
    if (!solved || retryForCondition)
    {
        const double retryRegularization = std::max({
            effectiveRegularization * 10.0,
            options.illConditionedRegularizationFloor,
            options.sparseRegularizationFloor
        });

        EnvelopeDiagnostics retryDiagnostics;
        std::vector<double> retryCoefficients;
        std::string retryError;
        if (options.retryIllConditionedWithRegularization &&
            solveCepstrumAttempt(usable,
                                 options,
                                 effectiveOrder,
                                 retryRegularization,
                                 retryCoefficients,
                                 retryDiagnostics,
                                 retryError))
        {
            coefficients = std::move(retryCoefficients);
            attemptDiagnostics = retryDiagnostics;
            effectiveRegularization = retryRegularization;
            result.diagnostics.fallbackReason = EnvelopeFallbackReason::IllConditioned;
            result.diagnostics.addWarning(EnvelopeWarningFallback,
                                          "regularization increased for ill-conditioned frame");
            solved = true;
        }
        else if (!solved)
        {
            attemptError = retryError.empty() ? attemptError : retryError;
        }
    }

    if (!solved)
    {
        result.errorMessage = attemptError.empty() ? "cepstrum fit failed" : attemptError;
        result.diagnostics.fitPointCount = static_cast<int>(usable.size());
        if (options.fallbackToConstantEnvelope)
        {
            finishConstantFallback(result,
                                   options,
                                   weightedMeanDb(usable),
                                   EnvelopeFallbackReason::IllConditioned,
                                   result.errorMessage);
            return true;
        }
        return false;
    }

    const auto existingWarnings = result.diagnostics.warnings;
    const auto existingFlags = result.diagnostics.warningFlags;
    const auto existingFallback = result.diagnostics.fallbackReason;
    result.diagnostics = attemptDiagnostics;
    result.diagnostics.warningFlags |= existingFlags;
    result.diagnostics.warnings.insert(result.diagnostics.warnings.end(),
                                       existingWarnings.begin(),
                                       existingWarnings.end());
    if (existingFallback != EnvelopeFallbackReason::None)
        result.diagnostics.fallbackReason = existingFallback;

    finishFitResult(result, options, effectiveOrder, effectiveRegularization, std::move(coefficients));
    return true;
}

double evaluateRegularizedDiscreteCepstrum(const FrameEnvelope& envelope,
                                           const double frequencyHz,
                                           bool* ok)
{
    if (envelope.storage == EnvelopeValueStorage::Samples)
    {
        if (envelope.values.empty())
        {
            setOk(ok, false);
            return 0.0;
        }
        if (envelope.values.size() == 1)
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

    if (envelope.storage != EnvelopeValueStorage::Coefficients || envelope.values.empty())
    {
        setOk(ok, false);
        return 0.0;
    }

    bool evalOk = false;
    const double value = evaluateCoefficients(envelope.values, envelope.frequencyAxis, frequencyHz, evalOk);
    setOk(ok, evalOk);
    return value;
}

RegularizedCepstrumEnvelopeEstimator::RegularizedCepstrumEnvelopeEstimator(RegularizedCepstrumEstimatorOptions nextOptions)
    : options(std::move(nextOptions))
{
}

std::string RegularizedCepstrumEnvelopeEstimator::id() const
{
    return kId;
}

std::uint32_t RegularizedCepstrumEnvelopeEstimator::version() const
{
    return kVersion;
}

bool RegularizedCepstrumEnvelopeEstimator::prepare(const EnvelopeEstimatorContext& nextContext,
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
    if (!isValidFrequencyAxis(options.fit.frequencyAxis))
        options.fit.frequencyAxis = nextContext.frequencyAxis;
    if (options.fit.frequencyAxis.sampleRate <= 0.0)
        options.fit.frequencyAxis.sampleRate = nextContext.sampleRate;

    if (!isValidFrequencyAxis(options.fit.frequencyAxis))
    {
        if (errorMessage)
            *errorMessage = "frequency axis is invalid";
        prepared = false;
        return false;
    }

    if (context.maxOrder > 0)
        options.fit.order = std::min(options.fit.order, context.maxOrder);

    lastDiagnostics = {};
    prepared = true;
    return true;
}

bool RegularizedCepstrumEnvelopeEstimator::estimate(const EnvelopeFrameInput& frame,
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

    RdcFitResult fit;
    if (!fitRegularizedDiscreteCepstrum(points, options.fit, fit))
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

double RegularizedCepstrumEnvelopeEstimator::evaluate(const FrameEnvelope& envelope,
                                                      const double frequencyHz,
                                                      bool* ok) const
{
    if (envelope.estimatorId != kId)
    {
        setOk(ok, false);
        return 0.0;
    }
    return evaluateRegularizedDiscreteCepstrum(envelope, frequencyHz, ok);
}

EnvelopeDiagnostics RegularizedCepstrumEnvelopeEstimator::diagnostics() const
{
    return lastDiagnostics;
}

} // namespace ats

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

std::vector<double> basisForFrequency(const double frequencyHz, const EnvelopeAxisSpec& axis, const int order, bool& ok)
{
    bool posOk = false;
    const double position = normalizedPositionForFrequency(frequencyHz, axis, &posOk);
    ok = posOk;

    std::vector<double> basis(static_cast<std::size_t>(order + 1), 0.0);
    basis[0] = 1.0;
    const double theta = kPi * position;
    for (int k = 1; k <= order; ++k)
        basis[static_cast<std::size_t>(k)] = 2.0 * std::cos(static_cast<double>(k) * theta);
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

bool solveSymmetricPositiveDefinite(const std::vector<std::vector<double>>& matrix,
                                    const std::vector<double>& rhs,
                                    std::vector<double>& solution,
                                    double& pivotRatio)
{
    const std::size_t n = rhs.size();
    std::vector<std::vector<double>> lower(n, std::vector<double>(n, 0.0));
    double minPivot = std::numeric_limits<double>::infinity();
    double maxPivot = 0.0;

    for (std::size_t i = 0; i < n; ++i)
    {
        for (std::size_t j = 0; j <= i; ++j)
        {
            double sum = matrix[i][j];
            for (std::size_t k = 0; k < j; ++k)
                sum -= lower[i][k] * lower[j][k];

            if (i == j)
            {
                if (!std::isfinite(sum) || sum <= 1.0e-18)
                    return false;
                lower[i][j] = std::sqrt(sum);
                minPivot = std::min(minPivot, lower[i][j]);
                maxPivot = std::max(maxPivot, lower[i][j]);
            }
            else
            {
                lower[i][j] = sum / lower[j][j];
            }
        }
    }

    std::vector<double> y(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        double sum = rhs[i];
        for (std::size_t k = 0; k < i; ++k)
            sum -= lower[i][k] * y[k];
        y[i] = sum / lower[i][i];
    }

    solution.assign(n, 0.0);
    for (std::size_t ii = n; ii-- > 0;)
    {
        double sum = y[ii];
        for (std::size_t k = ii + 1; k < n; ++k)
            sum -= lower[k][ii] * solution[k];
        solution[ii] = sum / lower[ii][ii];
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

    const int coefficientCount = options.order + 1;
    std::vector<std::vector<double>> matrix(static_cast<std::size_t>(coefficientCount),
                                            std::vector<double>(static_cast<std::size_t>(coefficientCount), 0.0));
    std::vector<double> rhs(static_cast<std::size_t>(coefficientCount), 0.0);

    double weightSum = 0.0;
    for (const auto& point : points)
    {
        if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.logAmplitudeDb) ||
            !std::isfinite(point.weight) || point.weight <= 0.0)
        {
            continue;
        }

        bool basisOk = false;
        const auto basis = basisForFrequency(point.frequencyHz, options.frequencyAxis, options.order, basisOk);
        if (!basisOk)
            continue;

        ++result.diagnostics.fitPointCount;
        weightSum += point.weight;
        for (int row = 0; row < coefficientCount; ++row)
        {
            const double weightedBasis = point.weight * basis[static_cast<std::size_t>(row)];
            rhs[static_cast<std::size_t>(row)] += weightedBasis * point.logAmplitudeDb;
            for (int col = 0; col <= row; ++col)
            {
                matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] +=
                    weightedBasis * basis[static_cast<std::size_t>(col)];
            }
        }
    }

    if (result.diagnostics.fitPointCount == 0 || weightSum <= 0.0)
    {
        result.errorMessage = "no usable fit points";
        return false;
    }

    for (int row = 0; row < coefficientCount; ++row)
    {
        for (int col = 0; col < row; ++col)
            matrix[static_cast<std::size_t>(col)][static_cast<std::size_t>(row)] =
                matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
    }

    for (int k = 0; k < coefficientCount; ++k)
    {
        matrix[static_cast<std::size_t>(k)][static_cast<std::size_t>(k)] +=
            std::max(0.0, options.ridge) + std::max(0.0, options.regularization) * penaltyDiagonal(k, options.penalty);
    }

    std::vector<double> coefficients;
    double pivotRatio = 0.0;
    if (!solveSymmetricPositiveDefinite(matrix, rhs, coefficients, pivotRatio))
    {
        result.errorMessage = "cepstrum normal equations were not positive definite";
        result.diagnostics.fallbackReason = EnvelopeFallbackReason::IllConditioned;
        result.diagnostics.addWarning(EnvelopeWarningFallback, result.errorMessage);
        return false;
    }

    double weightedSquaredError = 0.0;
    for (const auto& point : points)
    {
        if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.logAmplitudeDb) ||
            !std::isfinite(point.weight) || point.weight <= 0.0)
        {
            continue;
        }

        bool evalOk = false;
        const double predicted = evaluateCoefficients(coefficients, options.frequencyAxis, point.frequencyHz, evalOk);
        if (!evalOk)
            continue;
        const double residual = predicted - point.logAmplitudeDb;
        weightedSquaredError += point.weight * residual * residual;
    }

    result.diagnostics.weightedError = std::sqrt(weightedSquaredError / weightSum);
    result.diagnostics.conditionEstimate = pivotRatio * pivotRatio;
    result.effectiveOrder = options.order;

    result.envelope.frequencyAxis = options.frequencyAxis;
    result.envelope.envelopeOrder = options.order;
    result.envelope.regularization = options.regularization;
    result.envelope.penaltyType = toString(options.penalty);
    result.envelope.storage = EnvelopeValueStorage::Coefficients;
    result.envelope.values = std::move(coefficients);
    result.envelope.diagnostics = result.diagnostics;
    result.ok = true;
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

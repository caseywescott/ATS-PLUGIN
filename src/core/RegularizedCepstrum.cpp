#include "RegularizedCepstrum.h"

#include "FrequencyWarp.h"
#include "TrueEnvelope.h"

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

void fillBasisForNormalizedPosition(const double position, const int order, std::vector<double>& out)
{
    out[0] = 1.0;
    const double theta = kPi * std::clamp(position, 0.0, 1.0);
    for (int k = 1; k <= order; ++k)
        out[static_cast<std::size_t>(k)] = 2.0 * std::cos(static_cast<double>(k) * theta);
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

EnvelopeAxisSpec barkPenaltyAxisFor(const EnvelopeAxisSpec& axis)
{
    EnvelopeAxisSpec barkAxis = axis;
    barkAxis.type = EnvelopeFrequencyAxis::Bark;
    return barkAxis;
}

int penaltySampleCountForOrder(const int order)
{
    return std::max(17, 4 * (order + 1) + 1);
}

bool buildSmoothnessPenaltyMatrix(const EnvelopeAxisSpec& fitAxis,
                                  const CepstrumPenalty penalty,
                                  const int order,
                                  std::vector<double>& matrix)
{
    const std::size_t n = static_cast<std::size_t>(order + 1);
    matrix.assign(n * n, 0.0);
    if (penalty == CepstrumPenalty::None || order <= 0)
        return true;

    const EnvelopeAxisSpec penaltyAxis = barkPenaltyAxisFor(fitAxis);
    if (!isValidFrequencyAxis(penaltyAxis))
        return false;

    const int sampleCount = penaltySampleCountForOrder(order);
    const double step = 1.0 / static_cast<double>(sampleCount - 1);
    std::vector<double> basisRow(n, 0.0);
    std::vector<double> basisSamples(static_cast<std::size_t>(sampleCount) * n, 0.0);

    for (int i = 0; i < sampleCount; ++i)
    {
        const double position = static_cast<double>(i) * step;
        bool freqOk = false;
        const double frequencyHz = frequencyForNormalizedPosition(position, penaltyAxis, &freqOk);
        if (!freqOk || !fillBasisForFrequency(frequencyHz, fitAxis, order, basisRow))
            return false;

        std::copy(basisRow.begin(),
                  basisRow.end(),
                  basisSamples.begin() + static_cast<std::size_t>(i) * n);
    }

    std::vector<double> derivative(n, 0.0);
    const int edge = penalty == CepstrumPenalty::FirstDerivative ? 1 : 2;
    for (int i = edge; i < sampleCount - edge; ++i)
    {
        const double* prev2 = basisSamples.data() + static_cast<std::size_t>(i - 2) * n;
        const double* prev1 = basisSamples.data() + static_cast<std::size_t>(i - 1) * n;
        const double* curr = basisSamples.data() + static_cast<std::size_t>(i) * n;
        const double* next1 = basisSamples.data() + static_cast<std::size_t>(i + 1) * n;
        const double* next2 = basisSamples.data() + static_cast<std::size_t>(i + 2) * n;

        for (std::size_t k = 0; k < n; ++k)
        {
            if (penalty == CepstrumPenalty::FirstDerivative)
                derivative[k] = (next1[k] - prev1[k]) / (2.0 * step);
            else
                derivative[k] = (-next2[k] + 16.0 * next1[k] - 30.0 * curr[k] + 16.0 * prev1[k] - prev2[k])
                              / (12.0 * step * step);
        }

        for (std::size_t row = 0; row < n; ++row)
        {
            const double value = derivative[row];
            if (value == 0.0)
                continue;
            for (std::size_t col = 0; col <= row; ++col)
                matrix[row * n + col] += value * derivative[col] * step;
        }
    }

    for (std::size_t row = 0; row < n; ++row)
    {
        for (std::size_t col = 0; col < row; ++col)
            matrix[col * n + row] = matrix[row * n + col];
    }

    double scaleNumerator = 0.0;
    double scaleDenominator = 0.0;
    for (int k = 1; k <= order; ++k)
    {
        const double legacy = penaltyDiagonal(k, penalty);
        if (legacy <= 0.0)
            continue;
        const double diagonal = matrix[static_cast<std::size_t>(k) * n + static_cast<std::size_t>(k)];
        if (diagonal <= 0.0)
            continue;
        scaleNumerator += diagonal;
        scaleDenominator += legacy;
    }

    if (scaleNumerator > 0.0 && scaleDenominator > 0.0)
    {
        const double scale = scaleNumerator / scaleDenominator;
        if (scale > 0.0)
        {
            for (double& value : matrix)
                value /= scale;
        }
    }

    return true;
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

    // Disable SMR contribution when all SMR values are zero — applying the
    // 0.25× downweight equally across all partials provides no discrimination.
    AtsPartialWeightOptions effectiveOptions = options;
    if (effectiveOptions.useSmr)
    {
        bool allSmrZero = true;
        for (const auto& partial : frame.partials)
        {
            if (partial.smr != 0.0)
            {
                allSmrZero = false;
                break;
            }
        }
        if (allSmrZero)
            effectiveOptions.useSmr = false;
    }

    for (const auto& partial : frame.partials)
    {
        bool usable = false;
        const double weight = computeAtsPartialWeight(partial, effectiveOptions, &usable);
        if (!usable)
            continue;

        double amplitude = std::abs(partial.amplitude);
        if (amplitude < effectiveOptions.amplitudeFloor)
        {
            amplitude = options.amplitudeFloor;
            if (diagnostics)
            {
                ++diagnostics->clippedOrFlooredCount;
                diagnostics->warningFlags |= EnvelopeWarningAmplitudeFloor;
            }
        }

        out.push_back({ partial.frequencyHz, amplitudeToDb(amplitude, effectiveOptions.amplitudeFloor), weight });
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

double meanWeight(const std::vector<RdcFitPoint>& points)
{
    if (points.empty())
        return 0.0;

    double sum = 0.0;
    for (const auto& point : points)
        sum += point.weight;
    return sum / static_cast<double>(points.size());
}

std::vector<double> normalizedPositionsForPoints(const std::vector<RdcFitPoint>& points,
                                                 const EnvelopeAxisSpec& axis)
{
    std::vector<double> positions;
    positions.reserve(points.size());
    for (const auto& point : points)
    {
        bool ok = false;
        const double position = normalizedPositionForFrequency(point.frequencyHz, axis, &ok);
        if (ok)
            positions.push_back(position);
    }
    std::sort(positions.begin(), positions.end());
    return positions;
}

double nearestGapDistance(const std::vector<double>& positions, const double target)
{
    if (positions.empty())
        return 1.0;

    const auto it = std::lower_bound(positions.begin(), positions.end(), target);
    double best = 1.0;
    if (it != positions.end())
        best = std::min(best, std::abs(*it - target));
    if (it != positions.begin())
        best = std::min(best, std::abs(*std::prev(it) - target));
    return best;
}

std::vector<RdcFitPoint> makeEnvelopeSupportPoints(const FrameEnvelope& envelope,
                                                   const EnvelopeAxisSpec& fitAxis,
                                                   const int count)
{
    std::vector<RdcFitPoint> support;
    if (count <= 0 || !isValidFrequencyAxis(fitAxis))
        return support;

    const EnvelopeAxisSpec barkAxis = barkPenaltyAxisFor(fitAxis);
    if (!isValidFrequencyAxis(barkAxis))
        return support;

    support.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const double position = count == 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(count - 1);
        bool freqOk = false;
        const double frequencyHz = frequencyForNormalizedPosition(position, barkAxis, &freqOk);
        bool evalOk = false;
        const double valueDb = evaluateRegularizedDiscreteCepstrum(envelope, frequencyHz, &evalOk);
        if (!freqOk || !evalOk)
            continue;
        support.push_back({ frequencyHz, valueDb, 1.0 });
    }

    return support;
}

bool shouldReusePreviousFrame(const FrameEnvelope& previousEnvelope,
                              const bool hasPreviousEnvelope,
                              const std::string& previousSourceModelId,
                              const std::uint64_t previousFrameIndex,
                              const EnvelopeFrameInput& frame)
{
    return hasPreviousEnvelope &&
           previousEnvelope.storage != EnvelopeValueStorage::Samples &&
           previousSourceModelId == frame.sourceModelId &&
           frame.frameIndex == previousFrameIndex + 1;
}

void appendCrossFrameVirtualPoints(const FrameEnvelope& previousEnvelope,
                                   const std::vector<RdcFitPoint>& previousSupportPoints,
                                   const EnvelopeAxisSpec& fitAxis,
                                   const double weightScale,
                                   const double minGap,
                                   const int maxCount,
                                   std::vector<RdcFitPoint>& points)
{
    if (previousSupportPoints.empty() || points.empty() || maxCount <= 0 || weightScale <= 0.0)
        return;

    const auto currentPositions = normalizedPositionsForPoints(points, barkPenaltyAxisFor(fitAxis));
    if (currentPositions.empty())
        return;

    struct Candidate
    {
        RdcFitPoint point;
        double position = 0.0;
        double gap = 0.0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(previousSupportPoints.size());
    const double baseWeight = meanWeight(points) * weightScale;
    const EnvelopeAxisSpec gapAxis = barkPenaltyAxisFor(fitAxis);
    for (const auto& supportPoint : previousSupportPoints)
    {
        bool posOk = false;
        const double position = normalizedPositionForFrequency(supportPoint.frequencyHz, gapAxis, &posOk);
        if (!posOk)
            continue;

        const double gap = nearestGapDistance(currentPositions, position);
        if (gap < minGap)
            continue;

        bool evalOk = false;
        const double valueDb = evaluateRegularizedDiscreteCepstrum(previousEnvelope,
                                                                   supportPoint.frequencyHz,
                                                                   &evalOk);
        if (!evalOk)
            continue;

        Candidate candidate;
        candidate.point.frequencyHz = supportPoint.frequencyHz;
        candidate.point.logAmplitudeDb = valueDb;
        candidate.point.weight = std::max(1.0e-6, baseWeight);
        candidate.position = position;
        candidate.gap = gap;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.gap > b.gap;
    });

    std::vector<double> takenPositions = currentPositions;
    for (const auto& candidate : candidates)
    {
        if (static_cast<int>(points.size()) >= maxCount + static_cast<int>(currentPositions.size()))
            break;
        if (nearestGapDistance(takenPositions, candidate.position) < minGap * 0.5)
            continue;

        points.push_back(candidate.point);
        takenPositions.push_back(candidate.position);
        std::sort(takenPositions.begin(), takenPositions.end());
    }
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
    std::vector<double> penaltyMatrix;
    if (!buildSmoothnessPenaltyMatrix(options.frequencyAxis, options.penalty, order, penaltyMatrix))
    {
        errorMessage = "failed to build smoothness penalty matrix";
        return false;
    }

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

        matrix[row * n + row] += std::max(0.0, options.ridge);
    }

    if (!penaltyMatrix.empty() && regularization > 0.0)
    {
        const double lambda = std::max(0.0, regularization);
        for (std::size_t i = 0; i < matrix.size(); ++i)
            matrix[i] += lambda * penaltyMatrix[i];
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

    // BIC-based order selection: try orders minBicOrder..effectiveOrder and pick
    // the one with the lowest Bayesian Information Criterion.
    if (options.useBicOrderSelection && effectiveOrder > 1)
    {
        const int bicFloor = std::max(1, std::min(options.minBicOrder, effectiveOrder));
        int bestOrder = effectiveOrder;
        double bestBic = std::numeric_limits<double>::infinity();
        for (int o = bicFloor; o <= effectiveOrder; ++o)
        {
            std::vector<double> trialCoeffs;
            EnvelopeDiagnostics trialDiag;
            std::string trialErr;
            if (!solveCepstrumAttempt(usable, options, o, effectiveRegularization, trialCoeffs, trialDiag, trialErr))
                continue;

            // Skip ill-conditioned trial orders so the normal ill-condition
            // retry path is not bypassed by BIC selecting a lower order.
            if (trialDiag.conditionEstimate > options.illConditionedThreshold)
                continue;

            const int n = trialDiag.fitPointCount;
            const int k = o + 1;
            const double sigma2 = std::max(trialDiag.weightedError * trialDiag.weightedError, 1.0e-30);
            const double bic = static_cast<double>(n) * std::log(sigma2)
                             + static_cast<double>(k) * std::log(static_cast<double>(std::max(n, 1)));
            if (bic < bestBic)
            {
                bestBic = bic;
                bestOrder = o;
            }
        }
        effectiveOrder = bestOrder;
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
    previousEnvelope = {};
    previousSourceModelId.clear();
    previousFrameIndex = 0;
    hasPreviousEnvelope = false;
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
    std::vector<RdcFitPoint> realPoints;
    if (!buildRdcFitPointsFromPartials(frame, options.weights, realPoints, &pointDiagnostics))
    {
        if (errorMessage)
            *errorMessage = "no usable weighted partial points";
        lastDiagnostics = pointDiagnostics;
        lastDiagnostics.fallbackReason = EnvelopeFallbackReason::TooFewPoints;
        lastDiagnostics.addWarning(EnvelopeWarningSparseFrame, "no usable weighted partial points");
        return false;
    }

    std::vector<RdcFitPoint> points = realPoints;
    if (options.enableCrossFrameVirtualPoints &&
        shouldReusePreviousFrame(previousEnvelope,
                                 hasPreviousEnvelope,
                                 previousSourceModelId,
                                 previousFrameIndex,
                                 frame))
    {
        // Amplitude-delta gate: compute mean |predicted - actual| in dB across the
        // real fit points.  If the current frame's partials have shifted substantially
        // from the previous envelope (onset, decay, transient) the previous-frame
        // support is stale and should not be injected — doing so anchors the fit to
        // spectral content that no longer exists.
        double virtualWeightScale = options.crossFrameVirtualPointWeightScale;
        if (options.crossFrameVirtualPointMaxDeltaDb > 0.0 && !realPoints.empty())
        {
            double deltaSum = 0.0;
            int deltaCount = 0;
            for (const auto& rp : realPoints)
            {
                bool evalOk = false;
                const double predicted = evaluateRegularizedDiscreteCepstrum(previousEnvelope,
                                                                             rp.frequencyHz,
                                                                             &evalOk);
                if (evalOk)
                {
                    deltaSum += std::abs(rp.logAmplitudeDb - predicted);
                    ++deltaCount;
                }
            }
            const double meanDeltaDb = deltaCount > 0 ? deltaSum / static_cast<double>(deltaCount) : 0.0;
            if (meanDeltaDb > options.crossFrameVirtualPointMaxDeltaDb)
                virtualWeightScale = 0.0;
        }

        if (virtualWeightScale > 0.0)
        {
            const int maxCount = std::max(0, options.crossFrameVirtualPointMaxCount);
            appendCrossFrameVirtualPoints(previousEnvelope,
                                          makeEnvelopeSupportPoints(previousEnvelope,
                                                                    options.fit.frequencyAxis,
                                                                    maxCount),
                                          options.fit.frequencyAxis,
                                          virtualWeightScale,
                                          std::max(0.0, options.crossFrameVirtualPointMinGap),
                                          maxCount,
                                          points);
        }
    }

    // Auto-switch to True Envelope for dense frames — it is more accurate
    // in the high-partial-count regime and is already implemented.
    if (options.autoSwitchToTrueEnvelope &&
        static_cast<int>(points.size()) > options.autoSwitchThreshold)
    {
        TrueEnvelopeEstimatorOptions teOpts;
        teOpts.frequencyAxis = options.fit.frequencyAxis;
        teOpts.weights = options.weights;
        TrueEnvelopeFitResult teResult;
        if (fitTrueEnvelopeFromPoints(points, teOpts, teResult) && teResult.ok)
        {
            out = std::move(teResult.envelope);
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
            previousEnvelope = out;
            previousSourceModelId = frame.sourceModelId;
            previousFrameIndex = frame.frameIndex;
            hasPreviousEnvelope = true;
            return true;
        }
        // Fall through to RDC if True Envelope fails
    }

    // Density-based BIC minimum order: for frames with many fit points (voice,
    // dense strings) require BIC to select at least ceil(N * density) to avoid
    // collapsing to order 1-3 when meaningful timbral/formant structure exists.
    RdcFitOptions fitOptions = options.fit;
    if (fitOptions.useBicOrderSelection && options.bicMinOrderPartialDensity > 0.0)
    {
        const int densityFloor = static_cast<int>(std::ceil(
            static_cast<double>(points.size()) * options.bicMinOrderPartialDensity));
        fitOptions.minBicOrder = std::max(fitOptions.minBicOrder,
                                          std::min(densityFloor, fitOptions.order));
    }

    RdcFitResult fit;
    if (!fitRegularizedDiscreteCepstrum(points, fitOptions, fit))
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
    previousEnvelope = out;
    previousSourceModelId = frame.sourceModelId;
    previousFrameIndex = frame.frameIndex;
    hasPreviousEnvelope = true;
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

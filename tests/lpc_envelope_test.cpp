#include "LpcEnvelope.h"
#include "RegularizedCepstrum.h"
#include "TrueEnvelope.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr double kPi = 3.141592653589793238462643383279502884;

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

ats::EnvelopeAxisSpec axis()
{
    ats::EnvelopeAxisSpec a;
    a.type = ats::EnvelopeFrequencyAxis::LinearHz;
    a.minFrequencyHz = 100.0;
    a.maxFrequencyHz = 8000.0;
    a.sampleRate = 48000.0;
    return a;
}

double positionForFrequency(const double frequencyHz)
{
    return (frequencyHz - 100.0) / (8000.0 - 100.0);
}

double frequencyAtPosition(const double position)
{
    return 100.0 + position * (8000.0 - 100.0);
}

double allPoleFixtureDb(const double frequencyHz)
{
    const double omega = kPi * positionForFrequency(frequencyHz);
    const double poleRadius = 0.86;
    const double poleAngle = 0.32 * kPi;
    const double a1 = -2.0 * poleRadius * std::cos(poleAngle);
    const double a2 = poleRadius * poleRadius;
    const double real = 1.0 + a1 * std::cos(-omega) + a2 * std::cos(-2.0 * omega);
    const double imag = a1 * std::sin(-omega) + a2 * std::sin(-2.0 * omega);
    const double denominator = std::max(real * real + imag * imag, 1.0e-12);
    return -36.0 - 10.0 * std::log10(denominator);
}

std::vector<ats::RdcFitPoint> denseAllPolePoints()
{
    std::vector<ats::RdcFitPoint> points;
    for (int i = 0; i < 96; ++i)
    {
        const double position = (static_cast<double>(i) + 0.5) / 96.0;
        const double frequencyHz = frequencyAtPosition(position);
        points.push_back({ frequencyHz, allPoleFixtureDb(frequencyHz), 1.0 });
    }
    return points;
}

double meanErrorLpc(const ats::FrameEnvelope& envelope)
{
    double sum = 0.0;
    for (int i = 0; i < 31; ++i)
    {
        const double frequencyHz = frequencyAtPosition(static_cast<double>(i) / 30.0);
        bool ok = false;
        const double actual = ats::evaluateLpcEnvelope(envelope, frequencyHz, &ok);
        if (!ok)
            return 1.0e9;
        sum += std::abs(actual - allPoleFixtureDb(frequencyHz));
    }
    return sum / 31.0;
}

double meanErrorRdc(const ats::FrameEnvelope& envelope)
{
    double sum = 0.0;
    for (int i = 0; i < 31; ++i)
    {
        const double frequencyHz = frequencyAtPosition(static_cast<double>(i) / 30.0);
        bool ok = false;
        const double actual = ats::evaluateRegularizedDiscreteCepstrum(envelope, frequencyHz, &ok);
        if (!ok)
            return 1.0e9;
        sum += std::abs(actual - allPoleFixtureDb(frequencyHz));
    }
    return sum / 31.0;
}

double meanErrorTrue(const ats::FrameEnvelope& envelope)
{
    double sum = 0.0;
    for (int i = 0; i < 31; ++i)
    {
        const double frequencyHz = frequencyAtPosition(static_cast<double>(i) / 30.0);
        bool ok = false;
        const double actual = ats::evaluateTrueEnvelope(envelope, frequencyHz, &ok);
        if (!ok)
            return 1.0e9;
        sum += std::abs(actual - allPoleFixtureDb(frequencyHz));
    }
    return sum / 31.0;
}

double evaluatedRangeLpc(const ats::FrameEnvelope& envelope)
{
    double lo = 1.0e9;
    double hi = -1.0e9;
    for (int i = 0; i < 64; ++i)
    {
        bool ok = false;
        const double value = ats::evaluateLpcEnvelope(envelope, frequencyAtPosition(static_cast<double>(i) / 63.0), &ok);
        if (!ok)
            return 0.0;
        lo = std::min(lo, value);
        hi = std::max(hi, value);
    }
    return hi - lo;
}

double evaluatedRangeRdc(const ats::FrameEnvelope& envelope)
{
    double lo = 1.0e9;
    double hi = -1.0e9;
    for (int i = 0; i < 64; ++i)
    {
        bool ok = false;
        const double value =
            ats::evaluateRegularizedDiscreteCepstrum(envelope, frequencyAtPosition(static_cast<double>(i) / 63.0), &ok);
        if (!ok)
            return 0.0;
        lo = std::min(lo, value);
        hi = std::max(hi, value);
    }
    return hi - lo;
}

} // namespace

int main()
{
    const auto points = denseAllPolePoints();

    ats::LpcEnvelopeEstimatorOptions lpcOptions;
    lpcOptions.frequencyAxis = axis();
    lpcOptions.sampleCount = 128;
    lpcOptions.order = 8;
    lpcOptions.regularization = 1.0e-7;

    ats::LpcEnvelopeFitResult lpcResult;
    if (!ats::fitLpcEnvelopeFromPoints(points, lpcOptions, lpcResult))
        return fail("LPC fit failed: " + lpcResult.errorMessage);
    if (lpcResult.envelope.storage != ats::EnvelopeValueStorage::Samples)
        return fail("LPC envelope should be stored as sampled values");
    if (lpcResult.envelope.penaltyType != "lpc_all_pole")
        return fail("LPC envelope did not record its all-pole estimator type");
    if (lpcResult.effectiveOrder <= 0 || lpcResult.predictorCoefficients.empty())
        return fail("LPC fit did not produce predictor coefficients");

    ats::RdcFitOptions rdcOptions;
    rdcOptions.frequencyAxis = axis();
    rdcOptions.order = 8;
    rdcOptions.regularization = 0.05;
    rdcOptions.penalty = ats::CepstrumPenalty::SecondDerivative;
    ats::RdcFitResult rdcResult;
    if (!ats::fitRegularizedDiscreteCepstrum(points, rdcOptions, rdcResult))
        return fail("RDC comparison fit failed: " + rdcResult.errorMessage);

    ats::TrueEnvelopeEstimatorOptions trueOptions;
    trueOptions.frequencyAxis = axis();
    trueOptions.sampleCount = 128;
    trueOptions.lifterOrder = 14;
    trueOptions.iterations = 48;
    ats::TrueEnvelopeFitResult trueResult;
    if (!ats::fitTrueEnvelopeFromPoints(points, trueOptions, trueResult))
        return fail("true-envelope comparison fit failed: " + trueResult.errorMessage);

    const double lpcError = meanErrorLpc(lpcResult.envelope);
    const double rdcError = meanErrorRdc(rdcResult.envelope);
    const double trueError = meanErrorTrue(trueResult.envelope);
    if (!(lpcError < 1.0))
        return fail("LPC all-pole fixture fit error was too high");
    if (!(lpcError <= rdcError + 0.75 && lpcError <= trueError + 0.75))
        return fail("LPC all-pole fixture comparison regressed against RDC/true envelope");

    std::vector<ats::RdcFitPoint> sparsePoints = {
        { frequencyAtPosition(0.04), -38.0, 1.0 },
        { frequencyAtPosition(0.18), -12.0, 1.0 },
        { frequencyAtPosition(0.47), -44.0, 1.0 },
        { frequencyAtPosition(0.69), -15.0, 1.0 },
        { frequencyAtPosition(0.93), -40.0, 1.0 },
    };

    ats::LpcEnvelopeEstimatorOptions sparseLpcOptions = lpcOptions;
    sparseLpcOptions.order = 10;
    ats::LpcEnvelopeFitResult sparseLpcResult;
    if (!ats::fitLpcEnvelopeFromPoints(sparsePoints, sparseLpcOptions, sparseLpcResult))
        return fail("sparse LPC fit failed");
    if (!sparseLpcResult.diagnostics.hasWarning(ats::EnvelopeWarningSparseFrame))
        return fail("sparse LPC fit did not warn about isolated partial support");

    ats::RdcFitOptions sparseRdcOptions = rdcOptions;
    sparseRdcOptions.order = 10;
    ats::RdcFitResult sparseRdcResult;
    if (!ats::fitRegularizedDiscreteCepstrum(sparsePoints, sparseRdcOptions, sparseRdcResult))
        return fail("sparse RDC comparison failed");
    if (!(evaluatedRangeLpc(sparseLpcResult.envelope) > 0.5 * evaluatedRangeRdc(sparseRdcResult.envelope)))
        return fail("sparse LPC comparison did not preserve a resonant all-pole range");

    std::cout << "ok: LPC all-pole envelope estimator\n";
    return 0;
}

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

double upperContourDb(const double frequencyHz)
{
    const double theta = kPi * positionForFrequency(frequencyHz);
    return -20.0 + 4.5 * std::cos(theta) - 1.7 * std::cos(2.0 * theta) + 0.6 * std::cos(3.0 * theta);
}

double frequencyAtGridIndex(const int index, const int count)
{
    const double t = static_cast<double>(index) / static_cast<double>(count - 1);
    return 100.0 + t * (8000.0 - 100.0);
}

double meanAbsoluteError(const ats::FrameEnvelope& envelope,
                         const std::vector<double>& frequencies,
                         const std::vector<double>& expected)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < frequencies.size(); ++i)
    {
        bool ok = false;
        const double actual = ats::evaluateTrueEnvelope(envelope, frequencies[i], &ok);
        if (!ok)
            return 1.0e9;
        sum += std::abs(actual - expected[i]);
    }
    return sum / static_cast<double>(frequencies.size());
}

double meanAbsoluteRdcError(const ats::FrameEnvelope& envelope,
                            const std::vector<double>& frequencies,
                            const std::vector<double>& expected)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < frequencies.size(); ++i)
    {
        bool ok = false;
        const double actual = ats::evaluateRegularizedDiscreteCepstrum(envelope, frequencies[i], &ok);
        if (!ok)
            return 1.0e9;
        sum += std::abs(actual - expected[i]);
    }
    return sum / static_cast<double>(frequencies.size());
}

} // namespace

int main()
{
    constexpr int sampleCount = 129;

    std::vector<ats::RdcFitPoint> densePoints;
    std::vector<double> evalFrequencies;
    std::vector<double> evalExpected;
    for (int i = 0; i < sampleCount; ++i)
    {
        const double frequencyHz = frequencyAtGridIndex(i, sampleCount);
        const double notchDepth = (i % 8 == 0) ? 0.0 : (2.0 + static_cast<double>((i * 7) % 5));
        densePoints.push_back({ frequencyHz, upperContourDb(frequencyHz) - notchDepth, 1.0 });
        if (i % 8 == 0)
        {
            evalFrequencies.push_back(frequencyHz);
            evalExpected.push_back(upperContourDb(frequencyHz));
        }
    }

    ats::TrueEnvelopeEstimatorOptions trueOptions;
    trueOptions.frequencyAxis = axis();
    trueOptions.sampleCount = sampleCount;
    trueOptions.lifterOrder = 10;
    trueOptions.iterations = 48;
    trueOptions.convergenceDb = 0.02;
    trueOptions.lifterWindow = ats::TrueEnvelopeLifterWindow::Hamming;

    ats::TrueEnvelopeFitResult trueResult;
    if (!ats::fitTrueEnvelopeFromPoints(densePoints, trueOptions, trueResult))
        return fail("true-envelope fit failed: " + trueResult.errorMessage);
    if (trueResult.envelope.storage != ats::EnvelopeValueStorage::Samples ||
        trueResult.envelope.values.size() != sampleCount)
    {
        return fail("true-envelope did not store sampled values");
    }
    if (trueResult.envelope.penaltyType != "hamming_lifter")
        return fail("true-envelope did not record the lifter option");

    ats::RdcFitOptions rdcOptions;
    rdcOptions.frequencyAxis = axis();
    rdcOptions.order = 10;
    rdcOptions.regularization = 0.05;
    rdcOptions.penalty = ats::CepstrumPenalty::SecondDerivative;

    ats::RdcFitResult rdcResult;
    if (!ats::fitRegularizedDiscreteCepstrum(densePoints, rdcOptions, rdcResult))
        return fail("rdc dense comparison fit failed: " + rdcResult.errorMessage);

    const double trueUpperError = meanAbsoluteError(trueResult.envelope, evalFrequencies, evalExpected);
    const double rdcUpperError = meanAbsoluteRdcError(rdcResult.envelope, evalFrequencies, evalExpected);
    if (!(trueUpperError < rdcUpperError * 0.75))
        return fail("true-envelope was not closer to the dense upper contour than RDC");

    for (const auto& point : densePoints)
    {
        bool ok = false;
        const double estimated = ats::evaluateTrueEnvelope(trueResult.envelope, point.frequencyHz, &ok);
        if (!ok || estimated + 0.05 < point.logAmplitudeDb)
            return fail("true-envelope did not stay above observed dense spectrum points");
    }

    ats::TrueEnvelopeEstimatorOptions rectangularOptions = trueOptions;
    rectangularOptions.lifterWindow = ats::TrueEnvelopeLifterWindow::Rectangular;
    ats::TrueEnvelopeFitResult rectangularResult;
    if (!ats::fitTrueEnvelopeFromPoints(densePoints, rectangularOptions, rectangularResult))
        return fail("rectangular true-envelope fit failed: " + rectangularResult.errorMessage);
    if (rectangularResult.envelope.penaltyType != "rectangular_lifter")
        return fail("true-envelope did not record the rectangular lifter option");
    if (rectangularResult.envelope.values == trueResult.envelope.values)
        return fail("true-envelope lifter options produced identical sampled envelopes");

    std::vector<ats::RdcFitPoint> sparsePoints;
    for (const int index : { 8, 34, 75, 121 })
        sparsePoints.push_back({ frequencyAtGridIndex(index, sampleCount), upperContourDb(frequencyAtGridIndex(index, sampleCount)), 1.0 });

    ats::TrueEnvelopeFitResult sparseTrueResult;
    if (!ats::fitTrueEnvelopeFromPoints(sparsePoints, trueOptions, sparseTrueResult))
        return fail("sparse true-envelope fit failed");
    if (!sparseTrueResult.diagnostics.hasWarning(ats::EnvelopeWarningSparseFrame))
        return fail("sparse true-envelope comparison did not report sparse support");

    ats::RdcFitOptions sparseRdcOptions = rdcOptions;
    sparseRdcOptions.order = 10;
    ats::RdcFitResult sparseRdcResult;
    if (!ats::fitRegularizedDiscreteCepstrum(sparsePoints, sparseRdcOptions, sparseRdcResult))
        return fail("sparse RDC comparison fit failed");
    if (sparseRdcResult.diagnostics.fallbackReason != ats::EnvelopeFallbackReason::TooFewPoints)
        return fail("sparse RDC comparison did not record order reduction");

    ats::TrueEnvelopeLifterWindow parsed = ats::TrueEnvelopeLifterWindow::Rectangular;
    if (!ats::parseTrueEnvelopeLifterWindow("hamming_lifter", parsed) ||
        parsed != ats::TrueEnvelopeLifterWindow::Hamming)
    {
        return fail("could not parse hamming lifter option");
    }

    std::cout << "ok: iterative true-envelope estimator\n";
    return 0;
}

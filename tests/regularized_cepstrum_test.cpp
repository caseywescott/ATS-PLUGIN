#include "FrequencyWarp.h"
#include "RegularizedCepstrum.h"

#include <cmath>
#include <iostream>
#include <limits>
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

ats::EnvelopeAxisSpec makeAxis()
{
    ats::EnvelopeAxisSpec axis;
    axis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    axis.minFrequencyHz = 100.0;
    axis.maxFrequencyHz = 8000.0;
    axis.sampleRate = 48000.0;
    return axis;
}

double normalizedPosition(const double freqHz)
{
    return (freqHz - 100.0) / (8000.0 - 100.0);
}

double knownEnvelopeDb(const double freqHz)
{
    const double theta = kPi * normalizedPosition(freqHz);
    return -18.0 + 2.5 * std::cos(theta) - 1.25 * std::cos(2.0 * theta) + 0.7 * std::cos(3.0 * theta);
}

double barkCurvatureEnergy(const ats::FrameEnvelope& envelope)
{
    ats::EnvelopeAxisSpec barkAxis = envelope.frequencyAxis;
    barkAxis.type = ats::EnvelopeFrequencyAxis::Bark;

    constexpr int kSamples = 41;
    constexpr double kStep = 1.0 / static_cast<double>(kSamples - 1);
    std::vector<double> samples;
    samples.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i)
    {
        bool freqOk = false;
        const double freqHz = ats::frequencyForNormalizedPosition(static_cast<double>(i) * kStep,
                                                                  barkAxis,
                                                                  &freqOk);
        bool evalOk = false;
        const double value = ats::evaluateRegularizedDiscreteCepstrum(envelope, freqHz, &evalOk);
        if (!freqOk || !evalOk)
            return std::numeric_limits<double>::infinity();
        samples.push_back(value);
    }

    double sum = 0.0;
    for (std::size_t i = 1; i + 1 < samples.size(); ++i)
    {
        const double curvature = (samples[i - 1] - 2.0 * samples[i] + samples[i + 1]) / (kStep * kStep);
        sum += curvature * curvature;
    }
    return sum;
}

} // namespace

int main()
{
    const std::vector<double> freqs = { 100.0, 190.0, 313.0, 777.0, 1240.0, 2191.0, 4100.0, 6113.0, 8000.0 };
    std::vector<ats::RdcFitPoint> points;
    for (const double freq : freqs)
        points.push_back({ freq, knownEnvelopeDb(freq), 1.0 });

    ats::RdcFitOptions options;
    options.frequencyAxis = makeAxis();
    options.order = 3;
    options.regularization = 1.0e-12;
    options.penalty = ats::CepstrumPenalty::SecondDerivative;

    ats::RdcFitResult result;
    if (!ats::fitRegularizedDiscreteCepstrum(points, options, result))
        return fail("fit failed: " + result.errorMessage);
    if (!result.ok)
        return fail("result was not marked ok");
    if (result.envelope.values.size() != 4)
        return fail("unexpected coefficient count");
    if (!result.uncertainty.valid || result.uncertainty.coefficientCount != 4)
        return fail("uncertainty output was not populated for a successful fit");
    {
        bool sigmaOk = false;
        const double sigma = ats::evaluatePredictionSigma(result.envelope, 1500.0, &sigmaOk);
        if (!sigmaOk || !std::isfinite(sigma) || sigma <= 0.0)
            return fail("prediction sigma should be finite and positive");
    }

    for (const double freq : { 140.0, 440.0, 1500.0, 3200.0, 7000.0 })
    {
        bool ok = false;
        const double estimated = ats::evaluateRegularizedDiscreteCepstrum(result.envelope, freq, &ok);
        if (!ok)
            return fail("evaluate failed");
        const double expected = knownEnvelopeDb(freq);
        if (std::abs(estimated - expected) > 1.0e-3)
            return fail("known envelope mismatch");
    }

    auto noisyPoints = points;
    noisyPoints[1].logAmplitudeDb += 4.0;
    noisyPoints[4].logAmplitudeDb -= 3.5;
    noisyPoints[7].logAmplitudeDb += 2.5;

    ats::RdcFitOptions unregularized = options;
    unregularized.order = 8;
    unregularized.regularization = 0.0;
    unregularized.penalty = ats::CepstrumPenalty::None;

    ats::RdcFitOptions regularized = unregularized;
    regularized.regularization = 25.0;
    regularized.penalty = ats::CepstrumPenalty::SecondDerivative;

    ats::RdcFitResult unregularizedResult;
    ats::RdcFitResult regularizedResult;
    if (!ats::fitRegularizedDiscreteCepstrum(noisyPoints, unregularized, unregularizedResult))
        return fail("unregularized fit failed: " + unregularizedResult.errorMessage);
    if (!ats::fitRegularizedDiscreteCepstrum(noisyPoints, regularized, regularizedResult))
        return fail("regularized fit failed: " + regularizedResult.errorMessage);

    if (!(barkCurvatureEnergy(regularizedResult.envelope) < barkCurvatureEnergy(unregularizedResult.envelope)))
        return fail("second-derivative regularization did not reduce Bark-space curvature");

    ats::RdcFitOptions firstDerivative = options;
    firstDerivative.order = 5;
    firstDerivative.regularization = 1.0;
    firstDerivative.penalty = ats::CepstrumPenalty::FirstDerivative;
    ats::RdcFitResult firstDerivativeResult;
    if (!ats::fitRegularizedDiscreteCepstrum(points, firstDerivative, firstDerivativeResult))
        return fail("first-derivative regularized fit failed: " + firstDerivativeResult.errorMessage);
    if (firstDerivativeResult.diagnostics.fitPointCount != static_cast<int>(points.size()))
        return fail("fit point count mismatch");

    // Zero-weight points must be excluded from the fit
    {
        auto zeroWeightPoints = points;
        for (auto& p : zeroWeightPoints)
            p.weight = 0.0;

        ats::RdcFitOptions zeroOpts = options;
        ats::RdcFitResult zeroResult;
        // With all weights zero the fit should either fail gracefully or succeed with 0 fit points
        const bool zeroOk = ats::fitRegularizedDiscreteCepstrum(zeroWeightPoints, zeroOpts, zeroResult);
        if (zeroOk && zeroResult.ok)
        {
            if (zeroResult.diagnostics.fitPointCount != 0)
                return fail("zero-weight points should not count as fit points");
        }
        // If it returns false, that is also acceptable — it must not crash or produce garbage
    }

    // Degenerate single-point fit: should succeed with heavy regularization providing smoothness
    {
        const std::vector<ats::RdcFitPoint> singlePoint = { { 1000.0, -20.0, 1.0 } };
        ats::RdcFitOptions singleOpts = options;
        singleOpts.order = 4;
        singleOpts.regularization = 10.0;
        ats::RdcFitResult singleResult;
        if (!ats::fitRegularizedDiscreteCepstrum(singlePoint, singleOpts, singleResult))
            return fail("single-point fit with heavy regularization should succeed: " + singleResult.errorMessage);
        if (!singleResult.ok)
            return fail("single-point result was not marked ok");
        // Evaluate at the fit point — should be close to the input value (within 5 dB with strong regularization)
        bool ok = false;
        const double ev = ats::evaluateRegularizedDiscreteCepstrum(singleResult.envelope, 1000.0, &ok);
        if (!ok)
            return fail("single-point evaluate failed");
        if (std::abs(ev - (-20.0)) > 5.0)
            return fail("single-point fit evaluated value too far from input");
    }

    // High-order fit with tight regularization: strong penalty should reduce
    // Bark-space curvature relative to the unregularized solution.
    {
        std::vector<ats::RdcFitPoint> densePoints;
        for (int i = 0; i < 20; ++i)
        {
            const double freq = 100.0 + (8000.0 - 100.0) * static_cast<double>(i) / 19.0;
            densePoints.push_back({ freq, knownEnvelopeDb(freq), 1.0 });
        }

        ats::RdcFitOptions looseOpts = options;
        looseOpts.order = 12;
        looseOpts.regularization = 0.0;
        looseOpts.penalty = ats::CepstrumPenalty::None;

        ats::RdcFitOptions highOpts = options;
        highOpts.order = 12;
        highOpts.regularization = 100.0;
        highOpts.penalty = ats::CepstrumPenalty::SecondDerivative;

        ats::RdcFitResult looseResult;
        if (!ats::fitRegularizedDiscreteCepstrum(densePoints, looseOpts, looseResult))
            return fail("unregularized high-order fit failed: " + looseResult.errorMessage);

        ats::RdcFitResult highResult;
        if (!ats::fitRegularizedDiscreteCepstrum(densePoints, highOpts, highResult))
            return fail("high-order regularized fit failed: " + highResult.errorMessage);
        if (highResult.envelope.values.empty())
            return fail("high-order fit produced no coefficients");
        if (highResult.envelope.storage != ats::EnvelopeValueStorage::Coefficients)
            return fail("high-order fit should use coefficient storage");
        if (!(barkCurvatureEnergy(highResult.envelope) < barkCurvatureEnergy(looseResult.envelope)))
            return fail("strong regularization should reduce Bark-space curvature for high-order fits");
    }

    // Dense clean frames should be more certain than sparse noisy ones.
    {
        std::vector<ats::RdcFitPoint> sparseNoisy;
        for (const double freq : { 180.0, 550.0, 1200.0, 2600.0, 4100.0, 6900.0 })
            sparseNoisy.push_back({ freq, knownEnvelopeDb(freq) + std::sin(freq * 0.001) * 3.0, 1.0 });

        ats::RdcFitOptions sparseOpts = options;
        sparseOpts.order = 5;
        sparseOpts.regularization = 0.08;

        ats::RdcFitResult sparseResult;
        if (!ats::fitRegularizedDiscreteCepstrum(sparseNoisy, sparseOpts, sparseResult))
            return fail("sparse noisy fit failed: " + sparseResult.errorMessage);

        bool denseSigmaOk = false;
        bool sparseSigmaOk = false;
        const double denseSigma = ats::evaluatePredictionSigma(result.envelope, 1500.0, &denseSigmaOk);
        const double sparseSigma = ats::evaluatePredictionSigma(sparseResult.envelope, 1500.0, &sparseSigmaOk);
        if (!denseSigmaOk || !sparseSigmaOk || !(denseSigma < sparseSigma))
            return fail("dense clean fit should have lower prediction sigma than sparse noisy fit");
    }

    std::cout << "ok: regularized discrete cepstrum solver\n";
    return 0;
}

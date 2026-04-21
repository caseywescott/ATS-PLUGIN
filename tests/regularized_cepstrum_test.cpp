#include "RegularizedCepstrum.h"

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

double roughness(const std::vector<double>& values)
{
    double sum = 0.0;
    for (std::size_t i = 1; i < values.size(); ++i)
    {
        const double k = static_cast<double>(i);
        sum += k * k * k * k * values[i] * values[i];
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

    if (!(roughness(regularizedResult.envelope.values) < roughness(unregularizedResult.envelope.values)))
        return fail("second-derivative regularization did not reduce coefficient roughness");

    ats::RdcFitOptions firstDerivative = options;
    firstDerivative.order = 5;
    firstDerivative.regularization = 1.0;
    firstDerivative.penalty = ats::CepstrumPenalty::FirstDerivative;
    ats::RdcFitResult firstDerivativeResult;
    if (!ats::fitRegularizedDiscreteCepstrum(points, firstDerivative, firstDerivativeResult))
        return fail("first-derivative regularized fit failed: " + firstDerivativeResult.errorMessage);
    if (firstDerivativeResult.diagnostics.fitPointCount != static_cast<int>(points.size()))
        return fail("fit point count mismatch");

    std::cout << "ok: regularized discrete cepstrum solver\n";
    return 0;
}

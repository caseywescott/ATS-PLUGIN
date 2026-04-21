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
    axis.maxFrequencyHz = 5000.0;
    axis.sampleRate = 48000.0;
    return axis;
}

double expectedDb(const double frequencyHz)
{
    const double x = (frequencyHz - 100.0) / (5000.0 - 100.0);
    return -20.0 + 3.0 * std::cos(kPi * x);
}

double dbToAmp(const double db)
{
    return std::pow(10.0, db / 20.0);
}

double rmsTrustedError(const ats::FrameEnvelope& envelope, const std::vector<double>& trustedFrequencies)
{
    double sum = 0.0;
    for (const double freq : trustedFrequencies)
    {
        bool ok = false;
        const double value = ats::evaluateRegularizedDiscreteCepstrum(envelope, freq, &ok);
        if (!ok)
            return 1.0e9;
        const double error = value - expectedDb(freq);
        sum += error * error;
    }
    return std::sqrt(sum / static_cast<double>(trustedFrequencies.size()));
}

} // namespace

int main()
{
    ats::EnvelopePartialPoint strong;
    strong.frequencyHz = 440.0;
    strong.amplitude = 0.8;
    strong.smr = 28.0;
    strong.confidence = 0.95;
    strong.selected = true;

    ats::EnvelopePartialPoint weak = strong;
    weak.amplitude = 1.0e-5;
    weak.smr = -8.0;
    weak.confidence = 0.05;

    bool strongUsable = false;
    bool weakUsable = false;
    const double strongWeight = ats::computeAtsPartialWeight(strong, {}, &strongUsable);
    const double weakWeight = ats::computeAtsPartialWeight(weak, {}, &weakUsable);
    if (!strongUsable || !weakUsable || !(strongWeight > weakWeight * 100.0))
        return fail("default weighting did not strongly prefer reliable partials");

    weak.selected = false;
    bool selectedUsable = true;
    const double selectedWeight = ats::computeAtsPartialWeight(weak, {}, &selectedUsable);
    if (selectedUsable || selectedWeight != 0.0)
        return fail("unselected partial was not rejected");

    ats::EnvelopeFrameInput frame;
    frame.sourceModelId = "weighting-fixture";
    frame.frameIndex = 0;
    frame.sampleRate = 48000.0;

    const std::vector<double> freqs = { 100.0, 380.0, 770.0, 1330.0, 2100.0, 3400.0, 5000.0 };
    for (const double freq : freqs)
    {
        ats::EnvelopePartialPoint partial;
        partial.frequencyHz = freq;
        partial.amplitude = dbToAmp(expectedDb(freq));
        partial.smr = 26.0;
        partial.confidence = 1.0;
        partial.selected = true;
        frame.partials.push_back(partial);
    }

    // A deliberately unreliable outlier at a trusted frequency. Equal weighting
    // bends the low-order envelope toward it; ATS weights should mostly ignore it.
    ats::EnvelopePartialPoint outlier;
    outlier.frequencyHz = 2100.0;
    outlier.amplitude = dbToAmp(expectedDb(outlier.frequencyHz) + 28.0);
    outlier.smr = -2.0;
    outlier.confidence = 0.02;
    outlier.selected = true;
    frame.partials.push_back(outlier);

    ats::EnvelopeDiagnostics pointDiagnostics;
    std::vector<ats::RdcFitPoint> weightedPoints;
    if (!ats::buildRdcFitPointsFromPartials(frame, {}, weightedPoints, &pointDiagnostics))
        return fail("failed to build weighted fit points");

    auto equalPoints = weightedPoints;
    for (auto& point : equalPoints)
        point.weight = 1.0;

    ats::RdcFitOptions fit;
    fit.frequencyAxis = makeAxis();
    fit.order = 2;
    fit.regularization = 0.01;
    fit.penalty = ats::CepstrumPenalty::SecondDerivative;

    ats::RdcFitResult equalResult;
    ats::RdcFitResult weightedResult;
    if (!ats::fitRegularizedDiscreteCepstrum(equalPoints, fit, equalResult))
        return fail("equal fit failed: " + equalResult.errorMessage);
    if (!ats::fitRegularizedDiscreteCepstrum(weightedPoints, fit, weightedResult))
        return fail("weighted fit failed: " + weightedResult.errorMessage);

    const double equalError = rmsTrustedError(equalResult.envelope, freqs);
    const double weightedError = rmsTrustedError(weightedResult.envelope, freqs);
    if (!(weightedError < equalError * 0.6))
        return fail("ATS weighting did not reduce outlier influence");

    ats::RegularizedCepstrumEstimatorOptions estimatorOptions;
    estimatorOptions.fit = fit;
    ats::RegularizedCepstrumEnvelopeEstimator estimator(estimatorOptions);
    ats::EnvelopeEstimatorContext context;
    context.sampleRate = 48000.0;
    context.frequencyAxis = makeAxis();
    context.maxOrder = 8;

    std::string error;
    if (!estimator.prepare(context, &error))
        return fail("estimator prepare failed: " + error);

    ats::FrameEnvelope envelope;
    if (!estimator.estimate(frame, envelope, &error))
        return fail("estimator estimate failed: " + error);
    if (envelope.estimatorId != ats::RegularizedCepstrumEnvelopeEstimator::kId)
        return fail("estimator id mismatch");

    std::cout << "ok: ATS partial weighting policies\n";
    return 0;
}

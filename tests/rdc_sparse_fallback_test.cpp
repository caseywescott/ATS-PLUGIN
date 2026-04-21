#include "RegularizedCepstrum.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

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

} // namespace

int main()
{
    ats::RdcFitOptions sparseOptions;
    sparseOptions.frequencyAxis = makeAxis();
    sparseOptions.order = 12;
    sparseOptions.regularization = 0.0;
    sparseOptions.penalty = ats::CepstrumPenalty::SecondDerivative;

    std::vector<ats::RdcFitPoint> sparsePoints = {
        { 440.0, -18.0, 1.0 },
        { 1900.0, -22.0, 0.75 },
    };

    ats::RdcFitResult sparseResult;
    if (!ats::fitRegularizedDiscreteCepstrum(sparsePoints, sparseOptions, sparseResult))
        return fail("sparse fit failed: " + sparseResult.errorMessage);
    if (sparseResult.effectiveOrder != 1 || sparseResult.envelope.envelopeOrder != 1)
        return fail("sparse frame did not reduce cepstrum order");
    if (sparseResult.envelope.regularization < sparseOptions.sparseRegularizationFloor)
        return fail("sparse frame did not raise regularization");
    if (sparseResult.diagnostics.fallbackReason != ats::EnvelopeFallbackReason::TooFewPoints)
        return fail("sparse fallback reason was not recorded");
    if (!sparseResult.diagnostics.hasWarning(ats::EnvelopeWarningSparseFrame))
        return fail("sparse warning flag missing");

    ats::RdcFitOptions illOptions;
    illOptions.frequencyAxis = makeAxis();
    illOptions.order = 4;
    illOptions.regularization = 0.0;
    illOptions.ridge = 0.0;
    illOptions.reduceOrderForSparseFrames = false;
    illOptions.illConditionedThreshold = 1.0;
    illOptions.illConditionedRegularizationFloor = 10.0;
    illOptions.retryIllConditionedWithRegularization = true;

    std::vector<ats::RdcFitPoint> clusteredPoints = {
        { 1000.0, -18.0, 1.0 },
        { 1000.2, -18.1, 1.0 },
        { 1000.4, -18.2, 1.0 },
        { 1000.6, -18.3, 1.0 },
        { 1000.8, -18.4, 1.0 },
    };

    ats::RdcFitResult illResult;
    if (!ats::fitRegularizedDiscreteCepstrum(clusteredPoints, illOptions, illResult))
        return fail("ill-conditioned retry failed: " + illResult.errorMessage);
    if (illResult.envelope.regularization < illOptions.illConditionedRegularizationFloor)
        return fail("ill-conditioned frame did not increase regularization");
    if (illResult.diagnostics.fallbackReason != ats::EnvelopeFallbackReason::IllConditioned)
        return fail("ill-conditioned fallback reason was not recorded");
    if (!illResult.diagnostics.hasWarning(ats::EnvelopeWarningFallback))
        return fail("ill-conditioned warning flag missing");

    ats::RdcFitOptions fallbackOptions = illOptions;
    fallbackOptions.retryIllConditionedWithRegularization = false;
    fallbackOptions.fallbackToConstantEnvelope = true;

    ats::RdcFitResult fallbackResult;
    if (!ats::fitRegularizedDiscreteCepstrum(clusteredPoints, fallbackOptions, fallbackResult))
        return fail("constant fallback returned failure");
    if (fallbackResult.envelope.storage != ats::EnvelopeValueStorage::Samples ||
        fallbackResult.envelope.values.size() != 1)
    {
        return fail("ill-conditioned failure did not fall back to sampled constant envelope");
    }
    if (fallbackResult.diagnostics.fallbackReason != ats::EnvelopeFallbackReason::IllConditioned)
        return fail("constant fallback reason mismatch");

    std::vector<ats::RdcFitPoint> inharmonic = {
        { 137.0, -24.0, 1.0 },
        { 421.0, -20.0, 1.0 },
        { 963.0, -18.0, 0.8 },
        { 1777.0, -19.5, 0.9 },
        { 3181.0, -22.0, 0.7 },
    };
    ats::RdcFitOptions inharmonicOptions;
    inharmonicOptions.frequencyAxis = makeAxis();
    inharmonicOptions.order = 3;
    ats::RdcFitResult inharmonicResult;
    if (!ats::fitRegularizedDiscreteCepstrum(inharmonic, inharmonicOptions, inharmonicResult))
        return fail("inharmonic fit failed: " + inharmonicResult.errorMessage);

    bool evalOk = false;
    const double value = ats::evaluateRegularizedDiscreteCepstrum(inharmonicResult.envelope, 963.0, &evalOk);
    if (!evalOk || !std::isfinite(value))
        return fail("inharmonic evaluation failed");

    std::cout << "ok: sparse and ill-conditioned RDC handling\n";
    return 0;
}

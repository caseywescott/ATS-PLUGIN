#include "EnvelopeEvaluation.h"
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

ats::EnvelopeAxisSpec axis(const ats::EnvelopeFrequencyAxis type, const double minHz, const double maxHz)
{
    ats::EnvelopeAxisSpec a;
    a.type = type;
    a.minFrequencyHz = minHz;
    a.maxFrequencyHz = maxHz;
    a.sampleRate = 48000.0;
    a.customMin = 0.0;
    a.customMax = 1.0;
    return a;
}

ats::FrameEnvelope makeEnvelope()
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = ats::RegularizedCepstrumEnvelopeEstimator::kId;
    envelope.estimatorVersion = ats::RegularizedCepstrumEnvelopeEstimator::kVersion;
    envelope.sourceModelId = "eval-fixture";
    envelope.frameIndex = 0;
    envelope.sampleRate = 48000.0;
    envelope.frequencyAxis = axis(ats::EnvelopeFrequencyAxis::LinearHz, 100.0, 8000.0);
    envelope.envelopeOrder = 1;
    envelope.storage = ats::EnvelopeValueStorage::Coefficients;
    envelope.values = { -12.0, 2.0 };
    return envelope;
}

ats::FrameEnvelope makeSampledEnvelope()
{
    auto envelope = makeEnvelope();
    envelope.envelopeOrder = 0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    envelope.values = { -30.0, -20.0, -10.0 };
    return envelope;
}

} // namespace

int main()
{
    const auto envelope = makeEnvelope();

    ats::EnvelopeEvaluationOptions direct;
    direct.mapping = ats::EnvelopeFrequencyMapping::DirectHz;
    direct.extrapolation = ats::EnvelopeExtrapolationPolicy::Clamp;
    auto sourceSamples = ats::evaluateEnvelopeAtFrequencies(envelope, { 100.0, 440.0, 8000.0 }, direct);
    if (sourceSamples.size() != 3)
        return fail("source partial evaluation count mismatch");
    for (const auto& sample : sourceSamples)
    {
        if (!sample.ok || !sample.inRange || sample.lookupFrequencyHz != sample.inputFrequencyHz)
            return fail("direct source evaluation failed");
    }

    auto clamped = ats::evaluateEnvelopeAtFrequency(envelope, 20.0, direct);
    if (!clamped.ok || clamped.inRange || std::abs(clamped.lookupFrequencyHz - 100.0) > 1.0e-12)
        return fail("clamp extrapolation failed");

    direct.extrapolation = ats::EnvelopeExtrapolationPolicy::Reject;
    auto rejected = ats::evaluateEnvelopeAtFrequency(envelope, 20.0, direct);
    if (rejected.ok)
        return fail("reject extrapolation failed");

    ats::EnvelopeEvaluationOptions normalized;
    normalized.mapping = ats::EnvelopeFrequencyMapping::NormalizedNyquist;
    normalized.sourceAxis = axis(ats::EnvelopeFrequencyAxis::LinearHz, 0.0, 24000.0);
    normalized.targetAxis = envelope.frequencyAxis;
    auto normalizedSample = ats::evaluateEnvelopeAtFrequency(envelope, 12000.0, normalized);
    if (!normalizedSample.ok || std::abs(normalizedSample.lookupFrequencyHz - 4050.0) > 1.0e-6)
        return fail("normalized Nyquist mapping failed");

    ats::EnvelopeEvaluationOptions perceptual;
    perceptual.mapping = ats::EnvelopeFrequencyMapping::PerceptualWarp;
    perceptual.perceptualAxis = ats::EnvelopeFrequencyAxis::Bark;
    perceptual.sourceAxis = axis(ats::EnvelopeFrequencyAxis::LinearHz, 100.0, 16000.0);
    perceptual.targetAxis = envelope.frequencyAxis;
    auto perceptualSample = ats::evaluateEnvelopeAtFrequency(envelope, 16000.0, perceptual);
    if (!perceptualSample.ok || std::abs(perceptualSample.lookupFrequencyHz - 8000.0) > 1.0e-6)
        return fail("perceptual mapping edge failed");

    ats::EnvelopeEvaluationOptions rangeWarp;
    rangeWarp.mapping = ats::EnvelopeFrequencyMapping::RangeWarp;
    rangeWarp.sourceAxis = axis(ats::EnvelopeFrequencyAxis::LinearHz, 200.0, 1000.0);
    rangeWarp.targetAxis = envelope.frequencyAxis;
    auto rangeSample = ats::evaluateEnvelopeAtFrequency(envelope, 600.0, rangeWarp);
    if (!rangeSample.ok || std::abs(rangeSample.lookupFrequencyHz - 4050.0) > 1.0e-6)
        return fail("range warp mapping failed");

    auto grid = ats::evaluateEnvelopeOnGrid(envelope, 9, direct);
    if (grid.size() != 9 || !grid.front().ok || !grid.back().ok)
        return fail("visualization grid evaluation failed");

    const auto sampled = makeSampledEnvelope();
    auto sampledLow = ats::evaluateEnvelopeAtFrequency(sampled, 100.0, direct);
    auto sampledMid = ats::evaluateEnvelopeAtFrequency(sampled, 4050.0, direct);
    auto sampledHigh = ats::evaluateEnvelopeAtFrequency(sampled, 8000.0, direct);
    if (!sampledLow.ok || !sampledMid.ok || !sampledHigh.ok ||
        std::abs(sampledLow.valueDb - (-30.0)) > 1.0e-12 ||
        std::abs(sampledMid.valueDb - (-20.0)) > 1.0e-12 ||
        std::abs(sampledHigh.valueDb - (-10.0)) > 1.0e-12)
    {
        return fail("sampled envelope interpolation failed");
    }

    std::cout << "ok: envelope evaluation and mapping\n";
    return 0;
}

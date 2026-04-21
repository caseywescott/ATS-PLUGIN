#include "SpectralEnvelopeEstimator.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    auto registry = ats::createDefaultEnvelopeEstimatorRegistry();
    if (!registry.contains(ats::NoOpEnvelopeEstimator::kId))
        return fail("default registry is missing no-op estimator");

    const auto ids = registry.ids();
    if (ids.size() != 1 || ids.front() != ats::NoOpEnvelopeEstimator::kId)
        return fail("default registry ids are not stable");

    if (!registry.create("missing/estimator"))
    {
        // Expected path: unknown ids must not create an estimator.
    }
    else
    {
        return fail("unknown estimator id created an instance");
    }

    auto estimator = registry.create(ats::NoOpEnvelopeEstimator::kId);
    if (!estimator)
        return fail("failed to create no-op estimator");

    ats::EnvelopeEstimatorContext context;
    context.sampleRate = 48000.0;
    context.frequencyAxis.type = ats::EnvelopeFrequencyAxis::NormalizedNyquist;
    context.frequencyAxis.sampleRate = 48000.0;
    context.frequencyAxis.minFrequencyHz = 0.0;
    context.frequencyAxis.maxFrequencyHz = 24000.0;
    context.maxOrder = 0;

    std::string error;
    if (!estimator->prepare(context, &error))
        return fail("prepare failed: " + error);

    ats::EnvelopeFrameInput frame;
    frame.sourceModelId = "fixture:inharmonic";
    frame.frameIndex = 5;
    frame.sampleRate = 48000.0;
    frame.partials = {
        { 101.3, 0.8, 12.0, 1.0, true },
        { 277.9, 0.4, 8.0, 0.7, true },
        { 913.2, 0.1, 2.0, 0.2, false },
    };

    ats::FrameEnvelope envelope;
    if (!estimator->estimate(frame, envelope, &error))
        return fail("estimate failed: " + error);

    if (envelope.estimatorId != ats::NoOpEnvelopeEstimator::kId)
        return fail("estimator id mismatch");
    if (envelope.estimatorVersion != ats::NoOpEnvelopeEstimator::kVersion)
        return fail("estimator version mismatch");
    if (envelope.sourceModelId != frame.sourceModelId)
        return fail("source model mismatch");
    if (envelope.frameIndex != frame.frameIndex)
        return fail("frame index mismatch");
    if (envelope.storage != ats::EnvelopeValueStorage::Samples)
        return fail("storage mismatch");
    if (envelope.values.size() != 1 || std::abs(envelope.values.front()) > 1.0e-12)
        return fail("no-op estimator did not return neutral envelope");
    if (envelope.diagnostics.fitPointCount != 2)
        return fail("fit point count should include selected partials only");

    bool ok = false;
    const double evaluated = estimator->evaluate(envelope, 1234.0, &ok);
    if (!ok || std::abs(evaluated) > 1.0e-12)
        return fail("evaluate did not return neutral value");

    ats::FrameEnvelope roundTrip;
    if (!ats::FrameEnvelope::deserialize(estimator->serialize(envelope), roundTrip, &error))
        return fail("serialized estimator envelope did not round-trip: " + error);
    if (roundTrip.estimatorId != envelope.estimatorId)
        return fail("round-trip estimator id mismatch");

    std::cout << "ok: spectral envelope estimator interface\n";
    return 0;
}

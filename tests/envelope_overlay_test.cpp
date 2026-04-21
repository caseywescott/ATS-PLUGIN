#include "EnvelopeOverlay.h"
#include "RegularizedCepstrum.h"

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

ats::EnvelopeAxisSpec axis()
{
    ats::EnvelopeAxisSpec a;
    a.type = ats::EnvelopeFrequencyAxis::LinearHz;
    a.minFrequencyHz = 100.0;
    a.maxFrequencyHz = 8000.0;
    a.sampleRate = 48000.0;
    return a;
}

ats::FrameEnvelope makeEnvelope(const double dc, const double tilt)
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = ats::RegularizedCepstrumEnvelopeEstimator::kId;
    envelope.estimatorVersion = ats::RegularizedCepstrumEnvelopeEstimator::kVersion;
    envelope.sourceModelId = "overlay-source";
    envelope.frameIndex = 0;
    envelope.sampleRate = 48000.0;
    envelope.frequencyAxis = axis();
    envelope.envelopeOrder = 1;
    envelope.storage = ats::EnvelopeValueStorage::Coefficients;
    envelope.values = { dc, tilt };
    return envelope;
}

} // namespace

int main()
{
    ats::EnvelopeEvaluationOptions options;
    options.mapping = ats::EnvelopeFrequencyMapping::DirectHz;
    options.extrapolation = ats::EnvelopeExtrapolationPolicy::Clamp;

    const auto source = makeEnvelope(-20.0, 1.0);
    const auto target = makeEnvelope(-14.0, 1.0);
    const auto data = ats::buildEnvelopeTransferOverlay(source, &target, 16, options, options);
    if (data.source.samples.size() != 16 || data.target.samples.size() != 16 ||
        data.transferGain.samples.size() != 16)
    {
        return fail("overlay sample counts mismatch");
    }

    for (const auto& sample : data.transferGain.samples)
    {
        if (!sample.ok || std::abs(sample.valueDb - 6.0) > 1.0e-9)
            return fail("transfer gain overlay mismatch");
    }

    const auto pending = ats::buildEnvelopeTransferOverlay(source, nullptr, 8, options, options);
    if (pending.source.samples.size() != 8)
        return fail("source overlay missing when target is unavailable");
    if (pending.target.reliable || pending.transferGain.reliable)
        return fail("pending target/transfer curves should be unreliable");

    std::cout << "ok: envelope overlay data\n";
    return 0;
}

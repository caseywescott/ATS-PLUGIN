#include "SampledEnvelopeCache.h"
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

ats::FrameEnvelope makeEnvelope()
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = ats::RegularizedCepstrumEnvelopeEstimator::kId;
    envelope.estimatorVersion = ats::RegularizedCepstrumEnvelopeEstimator::kVersion;
    envelope.sourceModelId = "source-a";
    envelope.frameIndex = 2;
    envelope.sampleRate = 48000.0;
    envelope.frequencyAxis = axis();
    envelope.envelopeOrder = 1;
    envelope.regularization = 0.01;
    envelope.penaltyType = "second_derivative";
    envelope.storage = ats::EnvelopeValueStorage::Coefficients;
    envelope.values = { -18.0, 1.5 };
    return envelope;
}

ats::FrameEnvelope makeSampledEnvelope()
{
    auto envelope = makeEnvelope();
    envelope.envelopeOrder = 0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    envelope.values = { -24.0, -18.0, -12.0, -6.0 };
    return envelope;
}

} // namespace

int main()
{
    auto envelope = makeEnvelope();
    ats::EnvelopeEvaluationOptions options;
    options.mapping = ats::EnvelopeFrequencyMapping::DirectHz;
    options.extrapolation = ats::EnvelopeExtrapolationPolicy::Clamp;

    ats::SampledEnvelopeCacheEntry entry;
    if (!ats::buildSampledEnvelopeCacheEntry(envelope, options, 32, entry))
        return fail("failed to build cache entry");
    if (entry.samples.size() != 32)
        return fail("cache sample count mismatch");
    if (envelope.storage != ats::EnvelopeValueStorage::Coefficients || envelope.values.size() != 2)
        return fail("cache build modified coefficient source of truth");

    if (!ats::isSampledEnvelopeCacheValid(entry, envelope, options, 32))
        return fail("fresh cache entry should be valid");

    auto sourceChanged = envelope;
    sourceChanged.sourceModelId = "source-b";
    if (ats::isSampledEnvelopeCacheValid(entry, sourceChanged, options, 32))
        return fail("source model id change did not invalidate cache");

    auto valueChanged = envelope;
    valueChanged.values[1] += 0.25;
    if (ats::isSampledEnvelopeCacheValid(entry, valueChanged, options, 32))
        return fail("coefficient change did not invalidate cache");

    auto optionsChanged = options;
    optionsChanged.mapping = ats::EnvelopeFrequencyMapping::RangeWarp;
    optionsChanged.sourceAxis = axis();
    optionsChanged.targetAxis = axis();
    if (ats::isSampledEnvelopeCacheValid(entry, envelope, optionsChanged, 32))
        return fail("evaluation settings change did not invalidate cache");

    ats::SampledEnvelopeCache cache;
    const ats::SampledEnvelopeCacheEntry* cached = nullptr;
    if (!cache.getOrBuild(envelope, options, 32, cached) || cached == nullptr)
        return fail("cache get-or-build failed");
    if (cache.reusedLastBuild())
        return fail("first cache build should not report reuse");

    const ats::SampledEnvelopeCacheEntry* cachedAgain = nullptr;
    if (!cache.getOrBuild(envelope, options, 32, cachedAgain) || cachedAgain == nullptr)
        return fail("cache reuse failed");
    if (!cache.reusedLastBuild() || cachedAgain != cached)
        return fail("cache did not reuse valid samples");

    if (!cache.getOrBuild(valueChanged, options, 32, cachedAgain) || cache.reusedLastBuild())
        return fail("cache did not rebuild after coefficient change");

    auto sampled = makeSampledEnvelope();
    ats::SampledEnvelopeCacheEntry sampledEntry;
    if (!ats::buildSampledEnvelopeCacheEntry(sampled, options, 4, sampledEntry))
        return fail("failed to build cache entry from sampled envelope");
    if (sampledEntry.samples.size() != 4 ||
        std::abs(sampledEntry.samples.front().valueDb - (-24.0)) > 1.0e-12 ||
        std::abs(sampledEntry.samples.back().valueDb - (-6.0)) > 1.0e-12)
    {
        return fail("sampled envelope cache interpolation mismatch");
    }

    std::cout << "ok: sampled envelope cache\n";
    return 0;
}

#include "SampledEnvelopeCache.h"

#include <cstring>

namespace ats
{
namespace
{

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hashBytes(std::uint64_t& hash, const void* data, const std::size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
}

void hashString(std::uint64_t& hash, const std::string& text)
{
    hashBytes(hash, text.data(), text.size());
}

template <typename T>
void hashPod(std::uint64_t& hash, const T& value)
{
    hashBytes(hash, &value, sizeof(value));
}

void hashDouble(std::uint64_t& hash, const double value)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t), "unexpected double size");
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hashPod(hash, bits);
}

void hashAxis(std::uint64_t& hash, const EnvelopeAxisSpec& axis)
{
    hashPod(hash, static_cast<int>(axis.type));
    hashDouble(hash, axis.minFrequencyHz);
    hashDouble(hash, axis.maxFrequencyHz);
    hashDouble(hash, axis.sampleRate);
    hashDouble(hash, axis.customMin);
    hashDouble(hash, axis.customMax);
}

} // namespace

SampledEnvelopeCacheKey makeSampledEnvelopeCacheKey(const FrameEnvelope& envelope,
                                                    const EnvelopeEvaluationOptions& options,
                                                    const int gridSize)
{
    std::uint64_t hash = kFnvOffset;
    hashString(hash, envelope.estimatorId);
    hashPod(hash, envelope.estimatorVersion);
    hashString(hash, envelope.sourceModelId);
    hashPod(hash, envelope.frameIndex);
    hashDouble(hash, envelope.sampleRate);
    hashAxis(hash, envelope.frequencyAxis);
    hashPod(hash, envelope.envelopeOrder);
    hashDouble(hash, envelope.regularization);
    hashString(hash, envelope.penaltyType);
    hashPod(hash, static_cast<int>(envelope.storage));
    hashPod(hash, static_cast<std::uint64_t>(envelope.values.size()));
    for (const double value : envelope.values)
        hashDouble(hash, value);

    hashPod(hash, static_cast<int>(options.mapping));
    hashPod(hash, static_cast<int>(options.extrapolation));
    hashAxis(hash, options.sourceAxis);
    hashAxis(hash, options.targetAxis);
    hashPod(hash, static_cast<int>(options.perceptualAxis));
    hashPod(hash, gridSize);

    return { hash, gridSize };
}

bool isSampledEnvelopeCacheValid(const SampledEnvelopeCacheEntry& entry,
                                 const FrameEnvelope& envelope,
                                 const EnvelopeEvaluationOptions& options,
                                 const int gridSize)
{
    return entry.key == makeSampledEnvelopeCacheKey(envelope, options, gridSize);
}

bool buildSampledEnvelopeCacheEntry(const FrameEnvelope& envelope,
                                    const EnvelopeEvaluationOptions& options,
                                    const int gridSize,
                                    SampledEnvelopeCacheEntry& out)
{
    if (gridSize <= 0)
        return false;

    SampledEnvelopeCacheEntry next;
    next.key = makeSampledEnvelopeCacheKey(envelope, options, gridSize);
    next.samples = evaluateEnvelopeOnGrid(envelope, gridSize, options);
    if (next.samples.empty())
        return false;

    out = std::move(next);
    return true;
}

bool SampledEnvelopeCache::getOrBuild(const FrameEnvelope& envelope,
                                      const EnvelopeEvaluationOptions& options,
                                      const int gridSize,
                                      const SampledEnvelopeCacheEntry*& out)
{
    out = nullptr;
    if (valid && isSampledEnvelopeCacheValid(entry, envelope, options, gridSize))
    {
        lastReused = true;
        out = &entry;
        return true;
    }

    lastReused = false;
    SampledEnvelopeCacheEntry next;
    if (!buildSampledEnvelopeCacheEntry(envelope, options, gridSize, next))
    {
        valid = false;
        return false;
    }

    entry = std::move(next);
    valid = true;
    out = &entry;
    return true;
}

void SampledEnvelopeCache::clear()
{
    entry = {};
    valid = false;
    lastReused = false;
}

} // namespace ats

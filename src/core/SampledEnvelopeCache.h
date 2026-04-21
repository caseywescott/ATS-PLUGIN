#pragma once

#include "EnvelopeEvaluation.h"

#include <cstdint>
#include <vector>

namespace ats
{

struct SampledEnvelopeCacheKey
{
    std::uint64_t fingerprint = 0;
    int gridSize = 0;

    [[nodiscard]] bool operator==(const SampledEnvelopeCacheKey& other) const
    {
        return fingerprint == other.fingerprint && gridSize == other.gridSize;
    }

    [[nodiscard]] bool operator!=(const SampledEnvelopeCacheKey& other) const
    {
        return !(*this == other);
    }
};

struct SampledEnvelopeCacheEntry
{
    SampledEnvelopeCacheKey key;
    std::vector<EnvelopeEvaluationSample> samples;
};

[[nodiscard]] SampledEnvelopeCacheKey makeSampledEnvelopeCacheKey(const FrameEnvelope& envelope,
                                                                  const EnvelopeEvaluationOptions& options,
                                                                  int gridSize);
[[nodiscard]] bool isSampledEnvelopeCacheValid(const SampledEnvelopeCacheEntry& entry,
                                               const FrameEnvelope& envelope,
                                               const EnvelopeEvaluationOptions& options,
                                               int gridSize);
[[nodiscard]] bool buildSampledEnvelopeCacheEntry(const FrameEnvelope& envelope,
                                                  const EnvelopeEvaluationOptions& options,
                                                  int gridSize,
                                                  SampledEnvelopeCacheEntry& out);

class SampledEnvelopeCache
{
public:
    [[nodiscard]] bool getOrBuild(const FrameEnvelope& envelope,
                                  const EnvelopeEvaluationOptions& options,
                                  int gridSize,
                                  const SampledEnvelopeCacheEntry*& out);
    [[nodiscard]] bool reusedLastBuild() const { return lastReused; }
    void clear();

private:
    SampledEnvelopeCacheEntry entry;
    bool valid = false;
    bool lastReused = false;
};

} // namespace ats

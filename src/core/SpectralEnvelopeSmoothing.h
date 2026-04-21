#pragma once

#include "SpectralEnvelope.h"

#include <vector>

namespace ats
{

struct EnvelopeSmoothingOptions
{
    double amount = 0.5;
    bool resetOnTransient = true;
};

struct EnvelopeSmoothingFrame
{
    FrameEnvelope envelope;
    bool transientBoundary = false;
};

[[nodiscard]] bool canSmoothEnvelopes(const FrameEnvelope& previous, const FrameEnvelope& current);
[[nodiscard]] FrameEnvelope smoothEnvelopeFrame(const FrameEnvelope& previous,
                                                const FrameEnvelope& current,
                                                const EnvelopeSmoothingOptions& options);
[[nodiscard]] std::vector<FrameEnvelope> smoothEnvelopeSeries(const std::vector<EnvelopeSmoothingFrame>& frames,
                                                              const EnvelopeSmoothingOptions& options);
[[nodiscard]] double envelopeSeriesShimmerRms(const std::vector<FrameEnvelope>& frames);

} // namespace ats

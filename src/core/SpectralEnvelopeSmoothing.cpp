#include "SpectralEnvelopeSmoothing.h"

#include <algorithm>
#include <cmath>

namespace ats
{
namespace
{

bool sameAxisSpec(const EnvelopeAxisSpec& a, const EnvelopeAxisSpec& b)
{
    return a.type == b.type &&
           a.minFrequencyHz == b.minFrequencyHz &&
           a.maxFrequencyHz == b.maxFrequencyHz &&
           a.sampleRate == b.sampleRate &&
           a.customMin == b.customMin &&
           a.customMax == b.customMax;
}

} // namespace

bool canSmoothEnvelopes(const FrameEnvelope& previous, const FrameEnvelope& current)
{
    return previous.estimatorId == current.estimatorId &&
           previous.estimatorVersion == current.estimatorVersion &&
           previous.sourceModelId == current.sourceModelId &&
           previous.sampleRate == current.sampleRate &&
           previous.storage == current.storage &&
           previous.values.size() == current.values.size() &&
           sameAxisSpec(previous.frequencyAxis, current.frequencyAxis) &&
           previous.envelopeOrder == current.envelopeOrder &&
           previous.regularization == current.regularization &&
           previous.penaltyType == current.penaltyType;
}

FrameEnvelope smoothEnvelopeFrame(const FrameEnvelope& previous,
                                  const FrameEnvelope& current,
                                  const EnvelopeSmoothingOptions& options)
{
    const double amount = std::clamp(options.amount, 0.0, 1.0);
    if (amount <= 0.0 || !canSmoothEnvelopes(previous, current))
    {
        FrameEnvelope unsmoothed = current;
        unsmoothed.smoothingAmount = 0.0;
        return unsmoothed;
    }

    FrameEnvelope smoothed = current;
    for (std::size_t i = 0; i < smoothed.values.size(); ++i)
        smoothed.values[i] = amount * previous.values[i] + (1.0 - amount) * current.values[i];
    smoothed.smoothingAmount = amount;
    return smoothed;
}

std::vector<FrameEnvelope> smoothEnvelopeSeries(const std::vector<EnvelopeSmoothingFrame>& frames,
                                                const EnvelopeSmoothingOptions& options)
{
    std::vector<FrameEnvelope> result;
    result.reserve(frames.size());

    bool hasPrevious = false;
    FrameEnvelope previous;
    std::uint64_t smoothingGroup = 0;
    for (const auto& frame : frames)
    {
        const bool reset = !hasPrevious || (options.resetOnTransient && frame.transientBoundary);
        FrameEnvelope next = reset ? frame.envelope : smoothEnvelopeFrame(previous, frame.envelope, options);
        if (reset)
            next.smoothingAmount = 0.0;

        next.smoothingGroup = smoothingGroup;
        result.push_back(next);
        previous = result.back();
        hasPrevious = true;

        if (options.resetOnTransient && frame.transientBoundary)
            ++smoothingGroup;
    }

    return result;
}

double envelopeSeriesShimmerRms(const std::vector<FrameEnvelope>& frames)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 1; i < frames.size(); ++i)
    {
        if (!canSmoothEnvelopes(frames[i - 1], frames[i]))
            continue;

        for (std::size_t j = 0; j < frames[i].values.size(); ++j)
        {
            const double diff = frames[i].values[j] - frames[i - 1].values[j];
            sum += diff * diff;
            ++count;
        }
    }

    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

} // namespace ats

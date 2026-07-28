#pragma once

#include "MorphTimelineService.h"
#include "../core/AtsPrimitives.h"

#include <cstddef>
#include <vector>

namespace ats::engine
{

struct MorphTimelineRenderPlan
{
    std::vector<double> morph;
    std::vector<double> hybrid;
    std::vector<double> envelopeMorph;
};

class MorphRenderService
{
public:
    using Result = ats::JobResult<MorphTimelineRenderPlan>;

    [[nodiscard]] static Result renderTimeline(ats::JobGeneration generation,
                                                const ats::CancellationToken& cancellation,
                                                const std::vector<TimelinePoint>& points,
                                                std::size_t sampleCount)
    {
        Result result;
        result.generation = generation;
        if (cancellation.isCancelled())
        {
            result.status = Result::Status::cancelled;
            result.error = { "cancelled", "timeline render cancelled before start" };
            return result;
        }
        if (sampleCount == 0)
        {
            result.status = Result::Status::failed;
            result.error = { "invalid_request", "timeline render requires samples" };
            return result;
        }
        result.value.morph.reserve(sampleCount);
        result.value.hybrid.reserve(sampleCount);
        result.value.envelopeMorph.reserve(sampleCount);
        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            if (cancellation.isCancelled())
            {
                result.status = Result::Status::cancelled;
                result.error = { "cancelled", "timeline render cancelled" };
                return result;
            }
            const auto time = sampleCount > 1 ? static_cast<double>(i) / (sampleCount - 1) : 0.0;
            result.value.morph.push_back(MorphTimelineService::evaluate(points, TimelinePointKind::morph, time));
            result.value.hybrid.push_back(MorphTimelineService::evaluate(points, TimelinePointKind::hybrid, time));
            result.value.envelopeMorph.push_back(
                MorphTimelineService::evaluate(points, TimelinePointKind::envelopeMorph, time));
        }
        result.status = Result::Status::success;
        return result;
    }
};

} // namespace ats::engine

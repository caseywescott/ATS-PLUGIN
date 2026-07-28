#include "../src/engine/MorphRenderService.h"

#include <cassert>
#include <cmath>

int main()
{
    using namespace ats::engine;
    const std::vector<TimelinePoint> points {
        { 0.0, 0.0, TimelinePointKind::morph },
        { 0.5, 1.0, TimelinePointKind::morph, ats::MorphCurveShape::EaseIn },
        { 1.0, 0.0, TimelinePointKind::morph },
        { 0.25, 0.5, TimelinePointKind::hybrid },
        { 0.75, 0.25, TimelinePointKind::hybrid }
    };
    const auto safe = MorphTimelineService::sanitize(points);
    assert(safe.size() == 5);
    assert(MorphTimelineService::evaluate(safe, TimelinePointKind::morph, 0.0) == 0.0);
    assert(MorphTimelineService::evaluate(safe, TimelinePointKind::morph, 0.5) == 1.0);
    assert(MorphTimelineService::evaluate(safe, TimelinePointKind::hybrid, 0.5) > 0.25);

    int source = -1;
    assert(!MorphTimelineService::constantEndpointSource(points, source));
    assert(source == -1);
    assert(MorphTimelineService::constantEndpointSource({ { 0.0, 0.0 }, { 1.0, 0.0 } }, source));
    assert(source == 0);

    const auto moved = MorphTimelineService::edit(points, { TimelineEditOperation::move, 1,
                                                              { 0.4, 0.75, TimelinePointKind::morph,
                                                                ats::MorphCurveShape::EaseOut } });
    assert(moved.success && moved.changed);
    const auto removed = MorphTimelineService::edit(points, { TimelineEditOperation::remove, 0 });
    assert(!removed.success);

    ats::CancellationToken live;
    const auto rendered = MorphRenderService::renderTimeline({ 8 }, live, points, 9);
    assert(rendered.status == ats::JobResult<MorphTimelineRenderPlan>::Status::success);
    assert(rendered.value.morph.size() == 9);
    assert(std::isfinite(rendered.value.hybrid[4]));
    ats::CancellationToken cancelled;
    cancelled.cancel();
    const auto stopped = MorphRenderService::renderTimeline({ 9 }, cancelled, points, 9);
    assert(stopped.status == ats::JobResult<MorphTimelineRenderPlan>::Status::cancelled);
    return 0;
}

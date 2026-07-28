#include "../src/engine/AtsAnalysisService.h"

#include <cassert>

int main()
{
    const double samples[] { 0.0, 0.0, 0.0, 0.0 };
    ats::CancellationToken cancelled;
    cancelled.cancel();
    const auto cancelledResult = ats::engine::AtsAnalysisService::analyze(
        { 21 }, cancelled, { samples, 4, 48000, {} });
    assert(cancelledResult.status == ats::JobResult<ats::AnalysisOutput>::Status::cancelled);
    assert(cancelledResult.error.code == "cancelled");

    ats::CancellationToken live;
    const auto invalidResult = ats::engine::AtsAnalysisService::analyze(
        { 22 }, live, { nullptr, 0, 48000, {} });
    assert(invalidResult.status == ats::JobResult<ats::AnalysisOutput>::Status::failed);
    assert(invalidResult.error.code == "invalid_request");
    return 0;
}

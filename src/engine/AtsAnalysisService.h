#pragma once

#include "AtsEngine.h"

namespace ats::engine
{

class AtsAnalysisService
{
public:
    using Request = ats::AnalysisRequest;
    using Result = ats::AnalysisOutput;
    using Progress = ats::AtsEngine::Progress;

    [[nodiscard]] static ats::JobResult<Result> analyze(ats::JobGeneration generation,
                                                         const ats::CancellationToken& cancellation,
                                                         const Request& request,
                                                         Progress progress = {})
    { return ats::AtsEngine::analyze(generation, cancellation, request, std::move(progress)); }
};

} // namespace ats::engine

#pragma once

#include "../engine/AtsEngine.h"

namespace ats::dsp
{

class AtsResynthesisService
{
public:
    using Result = ats::RenderStats;

    [[nodiscard]] static ats::JobResult<Result> render(ats::JobGeneration generation,
                                                        const ats::CancellationToken& cancellation,
                                                        const ATS_SOUND* sound,
                                                        const ats::SynthParams& parameters,
                                                        ats::AudioBufferView destination)
    { return ats::AtsEngine::render(generation, cancellation, sound, parameters, destination); }
};

} // namespace ats::dsp

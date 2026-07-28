#pragma once

#include "AtsModel.h"
#include "AtsParams.h"
#include "AtsPrimitives.h"

#include <functional>

namespace ats
{

struct AnalysisRequest
{
    const double* samples = nullptr;
    std::size_t sampleCount = 0;
    int sampleRate = 0;
    AnalysisParams parameters{};
};

struct AnalysisOutput
{
    AtsSoundPtr sound;
    std::vector<double> residual;
};

struct RenderStats
{
    std::size_t framesWritten = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    bool finite = true;
};

class AtsEngine
{
public:
    using Progress = std::function<void(double)>;

    // Analysis and rendering are worker-domain operations. The request owns
    // its input for the duration of the call; no path, host, or UI type enters.
    [[nodiscard]] static JobResult<AnalysisOutput> analyze(JobGeneration generation,
                                                            const CancellationToken& cancellation,
                                                            const AnalysisRequest& request,
                                                            Progress progress = {});

    [[nodiscard]] static JobResult<RenderStats> render(JobGeneration generation,
                                                        const CancellationToken& cancellation,
                                                        const ATS_SOUND* sound,
                                                        const SynthParams& parameters,
                                                        AudioBufferView destination);
};

} // namespace ats

#pragma once

#include "EnvelopeEvaluation.h"

#include <string>
#include <vector>

namespace ats
{

struct EnvelopeOverlayCurve
{
    std::string label;
    std::vector<EnvelopeEvaluationSample> samples;
    EnvelopeDiagnostics diagnostics;
    bool reliable = true;
    std::string status;
};

struct EnvelopeOverlayData
{
    EnvelopeOverlayCurve source;
    EnvelopeOverlayCurve target;
    EnvelopeOverlayCurve transferGain;
};

[[nodiscard]] EnvelopeOverlayData buildEnvelopeTransferOverlay(
    const FrameEnvelope& sourceEnvelope,
    const FrameEnvelope* targetEnvelope,
    int gridSize,
    const EnvelopeEvaluationOptions& sourceOptions,
    const EnvelopeEvaluationOptions& targetOptions);

} // namespace ats

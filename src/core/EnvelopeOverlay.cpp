#include "EnvelopeOverlay.h"

#include <algorithm>

namespace ats
{

EnvelopeOverlayData buildEnvelopeTransferOverlay(const FrameEnvelope& sourceEnvelope,
                                                 const FrameEnvelope* targetEnvelope,
                                                 const int gridSize,
                                                 const EnvelopeEvaluationOptions& sourceOptions,
                                                 const EnvelopeEvaluationOptions& targetOptions)
{
    EnvelopeOverlayData data;
    data.source.label = "Source";
    data.source.samples = evaluateEnvelopeOnGrid(sourceEnvelope, gridSize, sourceOptions);
    data.source.diagnostics = sourceEnvelope.diagnostics;
    data.source.reliable = !data.source.samples.empty() &&
                           sourceEnvelope.diagnostics.fallbackReason == EnvelopeFallbackReason::None;
    if (!sourceEnvelope.diagnostics.warnings.empty())
        data.source.status = sourceEnvelope.diagnostics.warnings.front();
    if (!data.source.reliable)
        data.source.status = "Source envelope has diagnostics";

    data.target.label = "Target";
    data.transferGain.label = "Transfer";

    if (targetEnvelope == nullptr)
    {
        data.target.reliable = false;
        data.target.status = "Target envelope unavailable";
        data.transferGain.reliable = false;
        data.transferGain.status = "Transfer gain unavailable";
        return data;
    }

    data.target.samples = evaluateEnvelopeOnGrid(*targetEnvelope, gridSize, targetOptions);
    data.target.diagnostics = targetEnvelope->diagnostics;
    data.target.reliable = !data.target.samples.empty() &&
                           targetEnvelope->diagnostics.fallbackReason == EnvelopeFallbackReason::None;
    if (!targetEnvelope->diagnostics.warnings.empty())
        data.target.status = targetEnvelope->diagnostics.warnings.front();
    if (!data.target.reliable)
        data.target.status = "Target envelope has diagnostics";

    const auto n = std::min(data.source.samples.size(), data.target.samples.size());
    data.transferGain.samples.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        EnvelopeEvaluationSample sample;
        sample.inputFrequencyHz = data.source.samples[i].inputFrequencyHz;
        sample.lookupFrequencyHz = data.target.samples[i].lookupFrequencyHz;
        sample.inRange = data.source.samples[i].inRange && data.target.samples[i].inRange;
        sample.ok = data.source.samples[i].ok && data.target.samples[i].ok;
        sample.valueDb = sample.ok ? data.target.samples[i].valueDb - data.source.samples[i].valueDb : 0.0;
        data.transferGain.samples.push_back(sample);
    }
    data.transferGain.reliable = !data.transferGain.samples.empty() && data.source.reliable && data.target.reliable;
    if (!data.transferGain.reliable)
        data.transferGain.status = "Transfer gain has unreliable samples";

    return data;
}

} // namespace ats

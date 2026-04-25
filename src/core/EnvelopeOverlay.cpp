#include "EnvelopeOverlay.h"

#include <algorithm>
#include <cmath>

namespace ats
{
namespace
{

double overlayTransferGainDb(const double sourceEnvelopeDb,
                             const double targetEnvelopeDb,
                             const SpectralEnvelopeTransferRecipe& recipe)
{
    const double mix = std::clamp(recipe.envelopeMix, 0.0, 1.0);
    const double mixedEnvelopeDb = sourceEnvelopeDb + mix * (targetEnvelopeDb - sourceEnvelopeDb);
    return -recipe.sourceDecolorAmount * sourceEnvelopeDb +
           recipe.targetColorAmount * mixedEnvelopeDb;
}

} // namespace

EnvelopeOverlayData buildEnvelopeTransferOverlay(const FrameEnvelope& sourceEnvelope,
                                                 const FrameEnvelope* targetEnvelope,
                                                 const int gridSize,
                                                 const EnvelopeEvaluationOptions& sourceOptions,
                                                 const EnvelopeEvaluationOptions& targetOptions,
                                                 const SpectralEnvelopeTransferRecipe* recipe)
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

    SpectralEnvelopeTransferRecipe directRecipe;
    directRecipe.extrapolation = targetOptions.extrapolation;
    const auto& activeRecipe = recipe != nullptr ? *recipe : directRecipe;

    EnvelopeEvaluationOptions mappedTargetOptions;
    mappedTargetOptions.mapping = EnvelopeFrequencyMapping::DirectHz;
    mappedTargetOptions.extrapolation = activeRecipe.extrapolation;
    mappedTargetOptions.targetAxis = targetEnvelope->frequencyAxis;

    data.transferGain.samples.reserve(data.source.samples.size());
    for (const auto& sourceSample : data.source.samples)
    {
        EnvelopeEvaluationSample sample;
        sample.inputFrequencyHz = sourceSample.inputFrequencyHz;
        if (sourceSample.ok)
        {
            const auto mapping = mapHybridTargetFrequency(sourceSample.inputFrequencyHz,
                                                          sourceEnvelope,
                                                          *targetEnvelope,
                                                          activeRecipe);
            if (mapping.ok)
            {
                const auto targetSample = evaluateEnvelopeAtFrequency(*targetEnvelope,
                                                                       mapping.targetFrequencyHz,
                                                                       mappedTargetOptions);
                sample.lookupFrequencyHz = mapping.targetFrequencyHz;
                sample.inRange = sourceSample.inRange && mapping.inRange && targetSample.inRange;
                sample.ok = targetSample.ok;
                sample.valueDb = targetSample.ok ? overlayTransferGainDb(sourceSample.valueDb,
                                                                         targetSample.valueDb,
                                                                         activeRecipe)
                                                 : 0.0;
            }
        }
        data.transferGain.samples.push_back(sample);
    }
    data.transferGain.reliable = !data.transferGain.samples.empty() && data.source.reliable && data.target.reliable;
    if (!data.transferGain.reliable)
        data.transferGain.status = "Transfer gain has unreliable samples";

    return data;
}

} // namespace ats

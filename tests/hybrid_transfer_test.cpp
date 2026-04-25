#include "SpectralEnvelopeHybrid.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

ats::EnvelopeAxisSpec axis()
{
    ats::EnvelopeAxisSpec a;
    a.type = ats::EnvelopeFrequencyAxis::LinearHz;
    a.minFrequencyHz = 100.0;
    a.maxFrequencyHz = 5000.0;
    a.sampleRate = 48000.0;
    return a;
}

ats::FrameEnvelope constantEnvelope(const std::string& modelId, const double db)
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = ats::RegularizedCepstrumEnvelopeEstimator::kId;
    envelope.estimatorVersion = ats::RegularizedCepstrumEnvelopeEstimator::kVersion;
    envelope.sourceModelId = modelId;
    envelope.frameIndex = 0;
    envelope.sampleRate = 48000.0;
    envelope.frequencyAxis = axis();
    envelope.envelopeOrder = 0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    envelope.values = { db };
    return envelope;
}

ats::FrameEnvelope sampledEnvelope(const std::string& modelId, const std::vector<double>& values)
{
    auto envelope = constantEnvelope(modelId, values.empty() ? 0.0 : values.front());
    envelope.envelopeOrder = 0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    envelope.values = values;
    return envelope;
}

bool closeEnough(const double a, const double b, const double tolerance = 1.0e-9)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

} // namespace

int main()
{
    ats::SpectralEnvelopeTransferRecipe recipe;
    recipe.amplitudeFloor = 1.0e-12;
    recipe.sourceDecolorAmount = 1.0;
    recipe.targetColorAmount = 1.0;
    recipe.envelopeMix = 1.0;
    recipe.partialNormalization = ats::HybridNormalizationPolicy::None;

    const double fineDetailDb = -3.0;
    const double sourceEnvDb = -12.0;
    const double targetEnvDb = -6.0;
    const double sourceAmp = ats::dbToAmplitude(fineDetailDb + sourceEnvDb);

    const auto transferred = ats::applyLogDomainEnvelopeTransfer(sourceAmp, 440.0, sourceEnvDb, targetEnvDb, recipe);
    if (!transferred.ok)
        return fail("log-domain transfer failed");
    if (!closeEnough(transferred.outputAmplitude, ats::dbToAmplitude(fineDetailDb + targetEnvDb)))
        return fail("decolor/recolor did not preserve fine detail");

    recipe.sourceDecolorAmount = 0.0;
    const auto targetOnly = ats::applyLogDomainEnvelopeTransfer(sourceAmp, 440.0, sourceEnvDb, targetEnvDb, recipe);
    if (!targetOnly.ok)
        return fail("target-only transfer failed");
    if (!closeEnough(targetOnly.outputAmplitude, ats::dbToAmplitude(fineDetailDb + sourceEnvDb + targetEnvDb)))
        return fail("target-only mode was not explicit source-plus-target coloring");

    if (!(targetOnly.outputAmplitude < transferred.outputAmplitude * 0.5))
        return fail("target-only baseline did not expose retained source color");

    recipe.sourceDecolorAmount = 1.0;
    recipe.envelopeMix = 0.0;
    const auto sourceMix = ats::applyLogDomainEnvelopeTransfer(sourceAmp, 440.0, sourceEnvDb, targetEnvDb, recipe);
    if (!closeEnough(sourceMix.outputAmplitude, sourceAmp))
        return fail("envelope mix 0 should preserve source color after decolor/recolor");

    recipe.envelopeMix = 1.0;
    std::vector<ats::HybridPartialInput> partials = {
        { 220.0, sourceAmp },
        { 777.0, sourceAmp * 0.5 },
        { 2900.0, sourceAmp * 0.25 },
    };
    const auto outputs = ats::applyEnvelopeTransferDirect(partials,
                                                         constantEnvelope("source", sourceEnvDb),
                                                         constantEnvelope("target", targetEnvDb),
                                                         recipe);
    if (outputs.size() != partials.size())
        return fail("direct transfer output count mismatch");
    for (const auto& output : outputs)
    {
        if (!output.ok || !closeEnough(output.transferGainDb, targetEnvDb - sourceEnvDb))
            return fail("direct transfer gain mismatch");
    }

    recipe.partialNormalization = ats::HybridNormalizationPolicy::FrameRms;
    const auto normalized = ats::applyEnvelopeTransferDirect(partials,
                                                            constantEnvelope("source", sourceEnvDb),
                                                            constantEnvelope("target", targetEnvDb),
                                                            recipe);
    if (normalized.size() != partials.size())
        return fail("normalized transfer output count mismatch");
    for (std::size_t i = 0; i < normalized.size(); ++i)
    {
        if (!normalized[i].ok || !closeEnough(normalized[i].outputAmplitude, partials[i].amplitude))
            return fail("partial normalization policy was not applied by transfer API");
    }

    recipe.partialNormalization = ats::HybridNormalizationPolicy::None;
    const auto sampledOutputs = ats::applyEnvelopeTransferDirect({ { 2550.0, ats::dbToAmplitude(-21.0) } },
                                                                 sampledEnvelope("sampled-source", { -30.0, -20.0, -10.0 }),
                                                                 sampledEnvelope("sampled-target", { -12.0, -6.0, 0.0 }),
                                                                 recipe);
    if (sampledOutputs.size() != 1 || !sampledOutputs.front().ok ||
        !closeEnough(sampledOutputs.front().transferGainDb, 14.0))
    {
        return fail("hybrid transfer did not evaluate interpolated sampled envelopes");
    }

    std::cout << "ok: log-domain envelope transfer\n";
    return 0;
}

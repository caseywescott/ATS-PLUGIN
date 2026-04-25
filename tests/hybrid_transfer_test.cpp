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

    // envelopeMix=0.5 should blend output between source and fully-transferred amplitudes
    {
        const double srcAmp = ats::dbToAmplitude(fineDetailDb + sourceEnvDb);
        const double fullTransferAmp = ats::dbToAmplitude(fineDetailDb + targetEnvDb);

        recipe.sourceDecolorAmount = 1.0;
        recipe.envelopeMix = 0.5;
        const auto half = ats::applyLogDomainEnvelopeTransfer(srcAmp, 440.0, sourceEnvDb, targetEnvDb, recipe);
        if (!half.ok)
            return fail("envelopeMix=0.5 transfer failed");

        const double lo = std::min(srcAmp, fullTransferAmp);
        const double hi = std::max(srcAmp, fullTransferAmp);
        if (!(half.outputAmplitude > lo && half.outputAmplitude < hi))
            return fail("envelopeMix=0.5 output should be strictly between source and full-transfer amplitudes");

        // Blend is in dB space, so verify the output dB is approximately halfway between
        // source dB and full-transfer dB (within 1 dB of the midpoint)
        const double srcDb       = fineDetailDb + sourceEnvDb;
        const double fullDb      = fineDetailDb + targetEnvDb;
        const double midDb       = 0.5 * (srcDb + fullDb);
        const double halfDb      = 20.0 * std::log10(std::max(half.outputAmplitude, 1.0e-30));
        if (std::abs(halfDb - midDb) > 1.0)
            return fail("envelopeMix=0.5 output dB is not approximately halfway between source and full-transfer dB");

        recipe.envelopeMix = 1.0;
    }

    // FrameRms normalization with identical src/tgt envelopes must not change amplitudes
    {
        const double flatAmp = ats::dbToAmplitude(-18.0);
        const std::vector<ats::HybridPartialInput> flatPartials = {
            { 300.0, flatAmp },
            { 600.0, flatAmp },
            { 900.0, flatAmp },
        };
        recipe.partialNormalization = ats::HybridNormalizationPolicy::FrameRms;
        const auto flat = ats::applyEnvelopeTransferDirect(flatPartials,
                                                           constantEnvelope("src-flat", -18.0),
                                                           constantEnvelope("tgt-flat", -18.0),
                                                           recipe);
        if (flat.size() != flatPartials.size())
            return fail("flat FrameRms output count mismatch");
        for (std::size_t i = 0; i < flat.size(); ++i)
        {
            if (!flat[i].ok)
                return fail("flat FrameRms transfer returned !ok");
            if (!closeEnough(flat[i].outputAmplitude, flatAmp, 1.0e-6))
                return fail("flat FrameRms with identical src/tgt should preserve amplitudes");
        }
        recipe.partialNormalization = ats::HybridNormalizationPolicy::None;
    }

    std::cout << "ok: log-domain envelope transfer\n";
    return 0;
}

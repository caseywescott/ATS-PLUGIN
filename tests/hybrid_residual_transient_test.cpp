#include "SpectralEnvelopeHybrid.h"

#include <algorithm>
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

bool closeEnough(const double a, const double b, const double tolerance = 1.0e-9)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
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

} // namespace

int main()
{
    ats::SpectralEnvelopeTransferRecipe recipe;
    recipe.amplitudeFloor = 1.0e-12;
    recipe.sourceDecolorAmount = 1.0;
    recipe.targetColorAmount = 1.0;
    recipe.envelopeMix = 1.0;

    const auto sourceResidual = constantEnvelope("source-residual", -12.0);
    const auto targetResidual = constantEnvelope("target-residual", -6.0);
    std::vector<ats::HybridResidualBandInput> residualBands = {
        { 250.0, 1.0, false },
        { 1000.0, 0.25, true },
    };

    recipe.residualPolicy = ats::HybridResidualPolicy::Unchanged;
    auto residualOut = ats::applyResidualEnvelopeTransfer(residualBands, sourceResidual, targetResidual, recipe);
    if (residualOut.size() != residualBands.size() || !closeEnough(residualOut[0].outputEnergy, residualBands[0].energy))
        return fail("unchanged residual policy should preserve source band energy");

    recipe.residualPolicy = ats::HybridResidualPolicy::TargetColored;
    residualOut = ats::applyResidualEnvelopeTransfer(residualBands, sourceResidual, targetResidual, recipe);
    if (!(residualOut[0].outputEnergy < residualBands[0].energy))
        return fail("target-colored residual policy should recolor non-orphan bands");

    recipe.residualPolicy = ats::HybridResidualPolicy::DecolorThenColor;
    residualOut = ats::applyResidualEnvelopeTransfer(residualBands, sourceResidual, targetResidual, recipe);
    if (!(residualOut[0].outputEnergy > residualBands[0].energy))
        return fail("decolor-plus-color should move toward target contour");

    recipe.preserveOrphanResidual = true;
    residualOut = ats::applyResidualEnvelopeTransfer(residualBands, sourceResidual, targetResidual, recipe);
    if (!closeEnough(residualOut[1].outputEnergy, residualBands[1].energy))
        return fail("orphan residual band should remain unchanged when preservation enabled");

    recipe.preserveOrphanResidual = false;
    residualOut = ats::applyResidualEnvelopeTransfer(residualBands, sourceResidual, targetResidual, recipe);
    if (closeEnough(residualOut[1].outputEnergy, residualBands[1].energy))
        return fail("orphan residual band should be transformable when preservation disabled");

    const std::vector<ats::ResidualEnvelopeBandPoint> criticalBands = {
        { 300.0, 0.04 },
        { 800.0, 0.09 },
        { 2000.0, 0.25 },
    };
    const auto estimateA = ats::estimateResidualEnvelopeFromCriticalBands(criticalBands, recipe.amplitudeFloor, 0.4);
    const auto estimateB = ats::estimateResidualEnvelopeFromCriticalBands(criticalBands, recipe.amplitudeFloor, 0.4);
    if (!estimateA.ok || !estimateB.ok || estimateA.frequenciesHz != estimateB.frequenciesHz || estimateA.valuesDb != estimateB.valuesDb)
        return fail("residual envelope estimator should be deterministic");

    const auto seedA = ats::makeDeterministicNoiseSeed(1234u, 9u, 3u);
    const auto seedB = ats::makeDeterministicNoiseSeed(1234u, 9u, 3u);
    const auto seedC = ats::makeDeterministicNoiseSeed(1234u, 9u, 4u);
    if (seedA != seedB || seedA == seedC)
        return fail("deterministic noise seed handling is unstable");

    const double sourceAmp = 0.5;
    const double sourceEnvDb = -12.0;
    const double targetEnvDb = -6.0;
    const auto normal = ats::applyTransientAwareEnvelopeTransfer(sourceAmp,
                                                                 440.0,
                                                                 sourceEnvDb,
                                                                 targetEnvDb,
                                                                 false,
                                                                 recipe);
    if (!normal.ok)
        return fail("non-transient transfer should run normal transform");

    recipe.transientPolicy = ats::HybridTransientPolicy::PreserveSource;
    const auto preserve = ats::applyTransientAwareEnvelopeTransfer(sourceAmp,
                                                                   440.0,
                                                                   sourceEnvDb,
                                                                   targetEnvDb,
                                                                   true,
                                                                   recipe);
    if (!preserve.ok || !closeEnough(preserve.outputAmplitude, sourceAmp))
        return fail("preserve transient policy should keep source amplitude");

    recipe.transientPolicy = ats::HybridTransientPolicy::Bypass;
    const auto bypass = ats::applyTransientAwareEnvelopeTransfer(sourceAmp,
                                                                 440.0,
                                                                 sourceEnvDb,
                                                                 targetEnvDb,
                                                                 true,
                                                                 recipe);
    if (!bypass.ok || !closeEnough(bypass.outputAmplitude, sourceAmp))
        return fail("bypass transient policy should keep source amplitude");

    recipe.transientPolicy = ats::HybridTransientPolicy::TargetColor;
    const auto targetColor = ats::applyTransientAwareEnvelopeTransfer(sourceAmp,
                                                                      440.0,
                                                                      sourceEnvDb,
                                                                      targetEnvDb,
                                                                      true,
                                                                      recipe);
    if (!targetColor.ok || closeEnough(targetColor.outputAmplitude, sourceAmp))
        return fail("target-color transient policy should apply color during transients");

    recipe.transientPolicy = ats::HybridTransientPolicy::CrossfadeSource;
    recipe.transientCrossfade = 0.0;
    const auto crossfade0 = ats::applyTransientAwareEnvelopeTransfer(sourceAmp,
                                                                     440.0,
                                                                     sourceEnvDb,
                                                                     targetEnvDb,
                                                                     true,
                                                                     recipe);
    recipe.transientCrossfade = 1.0;
    const auto crossfade1 = ats::applyTransientAwareEnvelopeTransfer(sourceAmp,
                                                                     440.0,
                                                                     sourceEnvDb,
                                                                     targetEnvDb,
                                                                     true,
                                                                     recipe);
    if (!crossfade0.ok || !crossfade1.ok || !closeEnough(crossfade0.outputAmplitude, sourceAmp) ||
        closeEnough(crossfade1.outputAmplitude, sourceAmp))
    {
        return fail("crossfade transient policy should interpolate between preserved and transferred output");
    }

    std::cout << "ok: hybrid residual and transient policies\n";
    return 0;
}

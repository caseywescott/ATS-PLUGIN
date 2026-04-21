#include "SpectralEnvelopeHybrid.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool closeEnough(const double a, const double b)
{
    return std::abs(a - b) < 1.0e-12;
}

} // namespace

int main()
{
    ats::SpectralEnvelopeTransferRecipe recipe;
    recipe.recipeId = "recipe\tone";
    recipe.sourceModelId = "source-model";
    recipe.targetModelId = "target-model";
    recipe.estimatorId = ats::RegularizedCepstrumEnvelopeEstimator::kId;
    recipe.estimatorVersion = ats::RegularizedCepstrumEnvelopeEstimator::kVersion;
    recipe.envelopeOrder = 18;
    recipe.regularization = 0.125;
    recipe.penalty = ats::CepstrumPenalty::FirstDerivative;
    recipe.warpAxis = ats::EnvelopeFrequencyAxis::Erb;
    recipe.sourceDecolorAmount = 0.8;
    recipe.targetColorAmount = 0.65;
    recipe.envelopeMix = 0.4;
    recipe.amplitudeFloor = 1.0e-9;
    recipe.mapping = ats::HybridFrequencyMapping::Correspondence;
    recipe.extrapolation = ats::EnvelopeExtrapolationPolicy::Reject;
    recipe.partialNormalization = ats::HybridNormalizationPolicy::EnergyPreserving;
    recipe.residualNormalization = ats::HybridNormalizationPolicy::LoudnessLike;
    recipe.residualPolicy = ats::HybridResidualPolicy::DecolorThenColor;
    recipe.transientPolicy = ats::HybridTransientPolicy::CrossfadeSource;
    recipe.harmonicMappingEnabled = true;
    recipe.harmonicityReliable = false;
    recipe.sourceF0Hz = 110.0;
    recipe.targetF0Hz = 220.0;
    recipe.correspondence = {
        { 101.3, 207.9, 0.9 },
        { 433.7, 612.2, 0.5 },
    };

    const std::string first = recipe.serialize();
    const std::string second = recipe.serialize();
    if (first != second)
        return fail("recipe serialization is not deterministic");

    ats::SpectralEnvelopeTransferRecipe roundTrip;
    std::string error;
    if (!ats::SpectralEnvelopeTransferRecipe::deserialize(first, roundTrip, &error))
        return fail("recipe deserialize failed: " + error);

    if (roundTrip.recipeId != recipe.recipeId ||
        roundTrip.sourceModelId != recipe.sourceModelId ||
        roundTrip.targetModelId != recipe.targetModelId)
    {
        return fail("recipe model ids did not round-trip");
    }
    if (roundTrip.estimatorId != recipe.estimatorId ||
        roundTrip.estimatorVersion != recipe.estimatorVersion ||
        roundTrip.envelopeOrder != recipe.envelopeOrder ||
        roundTrip.penalty != recipe.penalty ||
        roundTrip.warpAxis != recipe.warpAxis)
    {
        return fail("estimator settings did not round-trip");
    }
    if (!closeEnough(roundTrip.sourceDecolorAmount, recipe.sourceDecolorAmount) ||
        !closeEnough(roundTrip.targetColorAmount, recipe.targetColorAmount) ||
        !closeEnough(roundTrip.envelopeMix, recipe.envelopeMix) ||
        !closeEnough(roundTrip.amplitudeFloor, recipe.amplitudeFloor))
    {
        return fail("transfer controls did not round-trip");
    }
    if (roundTrip.mapping != recipe.mapping ||
        roundTrip.partialNormalization != recipe.partialNormalization ||
        roundTrip.residualNormalization != recipe.residualNormalization ||
        roundTrip.residualPolicy != recipe.residualPolicy ||
        roundTrip.transientPolicy != recipe.transientPolicy)
    {
        return fail("policy settings did not round-trip");
    }
    if (roundTrip.correspondence.size() != recipe.correspondence.size() ||
        !closeEnough(roundTrip.correspondence[1].targetFrequencyHz, recipe.correspondence[1].targetFrequencyHz))
    {
        return fail("correspondence did not round-trip");
    }
    if (roundTrip.serialize() != first)
        return fail("recipe round-trip changed bytes");

    std::cout << "ok: spectral envelope transfer recipe\n";
    return 0;
}

#include "SpectralEnvelopeHybrid.h"

#include <algorithm>
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

bool closeEnough(const double a, const double b, const double tolerance = 1.0e-9)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

ats::EnvelopeAxisSpec axis(const double minHz, const double maxHz)
{
    ats::EnvelopeAxisSpec a;
    a.type = ats::EnvelopeFrequencyAxis::LinearHz;
    a.minFrequencyHz = minHz;
    a.maxFrequencyHz = maxHz;
    a.sampleRate = 48000.0;
    return a;
}

ats::FrameEnvelope envelope(const std::string& modelId, const double minHz, const double maxHz)
{
    ats::FrameEnvelope e;
    e.estimatorId = ats::RegularizedCepstrumEnvelopeEstimator::kId;
    e.estimatorVersion = ats::RegularizedCepstrumEnvelopeEstimator::kVersion;
    e.sourceModelId = modelId;
    e.sampleRate = 48000.0;
    e.frequencyAxis = axis(minHz, maxHz);
    e.envelopeOrder = 0;
    e.storage = ats::EnvelopeValueStorage::Samples;
    e.values = { -12.0 };
    return e;
}

} // namespace

int main()
{
    const auto source = envelope("source", 200.0, 1000.0);
    const auto target = envelope("target", 1000.0, 5000.0);

    ats::SpectralEnvelopeTransferRecipe recipe;
    if (recipe.mapping != ats::HybridFrequencyMapping::DirectHz || recipe.harmonicMappingEnabled)
        return fail("default mapping should not assume harmonic partials");

    auto mapped = ats::mapHybridTargetFrequency(333.0, source, target, recipe);
    if (!mapped.ok || !closeEnough(mapped.targetFrequencyHz, 1000.0) || mapped.inRange)
        return fail("default direct mapping should clamp without harmonic assumptions");

    recipe.mapping = ats::HybridFrequencyMapping::DirectHz;
    mapped = ats::mapHybridTargetFrequency(1500.0, source, target, recipe);
    if (!mapped.ok || !mapped.inRange || !closeEnough(mapped.targetFrequencyHz, 1500.0))
        return fail("direct Hz mapping failed");

    recipe.mapping = ats::HybridFrequencyMapping::NormalizedNyquist;
    mapped = ats::mapHybridTargetFrequency(600.0, source, target, recipe);
    if (!mapped.ok || !mapped.inRange || !closeEnough(mapped.targetFrequencyHz, 3000.0))
        return fail("normalized Nyquist mapping failed");

    recipe.mapping = ats::HybridFrequencyMapping::RangeWarp;
    mapped = ats::mapHybridTargetFrequency(600.0, source, target, recipe);
    if (!mapped.ok || !mapped.inRange || !closeEnough(mapped.targetFrequencyHz, 3000.0))
        return fail("range-warp mapping failed");

    recipe.mapping = ats::HybridFrequencyMapping::PerceptualWarp;
    recipe.warpAxis = ats::EnvelopeFrequencyAxis::Bark;
    mapped = ats::mapHybridTargetFrequency(1000.0, source, target, recipe);
    if (!mapped.ok || !mapped.inRange || !closeEnough(mapped.targetFrequencyHz, 5000.0))
        return fail("perceptual edge mapping failed");

    recipe.mapping = ats::HybridFrequencyMapping::Correspondence;
    recipe.correspondence = {
        { 200.0, 1000.0, 1.0 },
        { 333.0, 777.0, 1.0 },
        { 1000.0, 5000.0, 1.0 },
    };
    mapped = ats::mapHybridTargetFrequency(333.0, source, target, recipe);
    if (!mapped.ok || mapped.inRange || !closeEnough(mapped.targetFrequencyHz, 1000.0))
        return fail("correspondence mapping should respect target-axis clamping");

    const auto wideTarget = envelope("wide-target", 100.0, 5000.0);
    mapped = ats::mapHybridTargetFrequency(333.0, source, wideTarget, recipe);
    if (!mapped.ok || !mapped.inRange || !closeEnough(mapped.targetFrequencyHz, 777.0))
        return fail("correspondence mapping failed for non-harmonic partials");

    auto outputs = ats::applyEnvelopeTransferMapped({ { 333.0, 0.25 } }, source, wideTarget, recipe);
    if (outputs.size() != 1 || !outputs.front().ok || !closeEnough(outputs.front().targetFrequencyHz, 777.0))
        return fail("mapped transfer did not use correspondence target frequency");

    recipe.mapping = ats::HybridFrequencyMapping::HarmonicNumber;
    recipe.sourceF0Hz = 111.0;
    recipe.targetF0Hz = 200.0;
    mapped = ats::mapHybridTargetFrequency(333.0, source, wideTarget, recipe);
    if (mapped.ok)
        return fail("harmonic mapping should require explicit enablement");

    recipe.harmonicMappingEnabled = true;
    mapped = ats::mapHybridTargetFrequency(333.0, source, wideTarget, recipe);
    if (mapped.ok)
        return fail("harmonic mapping should require reliable harmonicity");

    recipe.harmonicityReliable = true;
    mapped = ats::mapHybridTargetFrequency(333.0, source, wideTarget, recipe);
    if (!mapped.ok || !mapped.inRange || !closeEnough(mapped.targetFrequencyHz, 600.0))
        return fail("gated harmonic-number mapping failed");

    std::cout << "ok: hybrid frequency mapping modes\n";
    return 0;
}

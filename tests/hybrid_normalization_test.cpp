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

ats::HybridPartialOutput partial(const double sourceAmplitude, const double outputAmplitude)
{
    ats::HybridPartialOutput p;
    p.frequencyHz = 440.0;
    p.targetFrequencyHz = 440.0;
    p.sourceAmplitude = sourceAmplitude;
    p.outputAmplitude = outputAmplitude;
    p.transferGainDb = 0.0;
    p.ok = true;
    return p;
}

} // namespace

int main()
{
    constexpr double floor = 1.0e-12;

    std::vector<ats::HybridPartialOutput> none = { partial(1.0, 2.0) };
    auto stats = ats::applyHybridGainNormalization(none, ats::HybridNormalizationPolicy::None, floor);
    if (!stats.ok || stats.applied || !closeEnough(none.front().outputAmplitude, 2.0))
        return fail("none normalization should leave gains unchanged");

    std::vector<ats::HybridPartialOutput> frameRms = {
        partial(1.0, 2.0),
        partial(1.0, 2.0),
    };
    stats = ats::applyHybridGainNormalization(frameRms, ats::HybridNormalizationPolicy::FrameRms, floor);
    if (!stats.ok || !stats.applied || !closeEnough(stats.outputRmsAfter, stats.sourceRms))
        return fail("frame RMS normalization did not match source RMS");
    if (!closeEnough(frameRms.front().outputAmplitude, 1.0) ||
        !closeEnough(stats.gainDb, -6.020599913279624, 1.0e-8))
    {
        return fail("frame RMS normalization gain mismatch");
    }

    std::vector<ats::HybridPartialOutput> energy = {
        partial(1.0, 2.0),
        partial(2.0, 4.0),
    };
    stats = ats::applyHybridGainNormalization(energy, ats::HybridNormalizationPolicy::EnergyPreserving, floor);
    if (!stats.ok || !stats.applied || !closeEnough(stats.outputEnergyAfter, stats.sourceEnergy))
        return fail("energy-preserving normalization failed");

    std::vector<ats::HybridPartialOutput> loudness = {
        partial(1.0, 4.0),
        partial(1.0, 4.0),
    };
    stats = ats::applyHybridGainNormalization(loudness, ats::HybridNormalizationPolicy::LoudnessLike, floor);
    if (!stats.ok || !stats.applied || !closeEnough(loudness.front().outputAmplitude, 1.0))
        return fail("loudness-like normalization failed");

    std::vector<ats::HybridPartialOutput> partialPath = { partial(1.0, 4.0) };
    std::vector<ats::HybridPartialOutput> residualPath = { partial(1.0, 0.5) };
    const auto partialStats = ats::applyHybridGainNormalization(partialPath, ats::HybridNormalizationPolicy::FrameRms, floor);
    const auto residualStats = ats::applyHybridGainNormalization(residualPath, ats::HybridNormalizationPolicy::FrameRms, floor);
    if (!partialStats.ok || !residualStats.ok ||
        !closeEnough(partialPath.front().outputAmplitude, 1.0) ||
        !closeEnough(residualPath.front().outputAmplitude, 1.0) ||
        !(partialStats.gainDb < residualStats.gainDb))
    {
        return fail("partial and residual paths should normalize independently");
    }

    std::vector<ats::HybridPartialOutput> frameA = {
        partial(1.0, 0.5),
        partial(1.0, 0.5),
    };
    std::vector<ats::HybridPartialOutput> frameB = {
        partial(1.0, 8.0),
        partial(1.0, 8.0),
    };
    const auto frameAStats = ats::applyHybridGainNormalization(frameA, ats::HybridNormalizationPolicy::FrameRms, floor);
    const auto frameBStats = ats::applyHybridGainNormalization(frameB, ats::HybridNormalizationPolicy::FrameRms, floor);
    if (!frameAStats.ok || !frameBStats.ok ||
        !closeEnough(frameAStats.outputRmsAfter, frameBStats.outputRmsAfter))
    {
        return fail("frame RMS normalization should avoid large output-RMS jumps");
    }

    std::cout << "ok: hybrid gain normalization policies\n";
    return 0;
}

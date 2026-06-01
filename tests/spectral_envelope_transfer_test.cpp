#include "AtsSpectralEnvelope.h"

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

bool closeEnough(double a, double b, double tolerance = 1.0e-9)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

} // namespace

int main()
{
    {
        ats::BarkSpectralEnvelope envelope;
        envelope.build({ { 12000.0, 0.25 }, { 22050.0, 0.5 } });
        const double highSample = envelope.sampleAtHz(24000.0);
        if (!envelope.isValid() || !std::isfinite(highSample) || highSample <= 0.0)
            return fail("Bark envelope high-frequency lookup must clamp to a valid edge sample");
    }

    {
        ats::BarkSpectralEnvelope sparse;
        sparse.build({ { 120.0, 0.2 }, { 9000.0, 0.8 } });
        const double gapSample = sparse.sampleAtHz(2500.0);
        if (!sparse.isValid() || !std::isfinite(gapSample) || gapSample <= 0.0)
            return fail("sparse Bark envelope gap fill should produce a positive contour");
    }

    {
        const auto transfer = ats::applySpectralEnvelopeTransfer(0.1, 1.0, 1000.0, 1.0);
        if (!transfer.ok || !closeEnough(transfer.outputAmplitude, 100.0, 1.0e-8))
            return fail("envelope transfer should not hard-clamp large valid ratios");
    }

    {
        const auto transfer = ats::applySpectralEnvelopeTransfer(0.5, 1.0, 0.0, 1.0, 1.0e-6);
        if (!transfer.ok || !(transfer.outputAmplitude < 1.0e-5))
            return fail("zero target envelope should attenuate toward the floor, not bypass transfer");
    }

    {
        const auto partial = ats::resolveHybridizedPartialRender(ats::HybridizedPartialKind::Matched,
                                                                 100.0,
                                                                 0.25,
                                                                 200.0,
                                                                 1.0,
                                                                 0.5);
        if (!partial.active || !closeEnough(partial.frequencyHz, 150.0)
            || !closeEnough(partial.amplitude, 0.625))
        {
            return fail("hybridized source amplitude should continue through the normal morph blend");
        }
    }

    {
        const auto target = ats::resolveHybridizedPartialRender(ats::HybridizedPartialKind::UnmatchedTarget,
                                                                0.0,
                                                                0.0,
                                                                440.0,
                                                                0.8,
                                                                0.5);
        if (!target.active || !closeEnough(target.frequencyHz, 440.0) || !closeEnough(target.amplitude, 0.4))
            return fail("hybrid transfer should not suppress target-only partials when morph amount is nonzero");
    }

    std::cout << "ok: spectral envelope transfer helpers\n";
    return 0;
}

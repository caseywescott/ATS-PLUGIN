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

    // Reused DCT tables and scratch must be isolated by (binCount, order) and fully reset.
    {
        const std::vector<ats::SpectralEnvelopePoint> source {
            { 110.0, 0.15 }, { 260.0, 0.8 }, { 720.0, 0.35 }, { 1800.0, 0.6 }, { 6400.0, 0.2 }
        };
        const std::vector<double> probes { 90.0, 220.0, 500.0, 1300.0, 4000.0, 12000.0 };

        ats::BarkSpectralEnvelope first;
        first.build(source, 64, 9);
        std::vector<double> expected;
        for (double hz : probes)
            expected.push_back(first.sampleAtHz(hz));

        ats::BarkSpectralEnvelope otherKey;
        otherKey.build({ { 80.0, 0.9 }, { 3000.0, 0.1 }, { 15000.0, 0.7 } }, 96, 15);
        ats::BarkSpectralEnvelope sameKeyOtherInput;
        sameKeyOtherInput.build({ { 140.0, 0.4 }, { 900.0, 0.95 }, { 10000.0, 0.08 } }, 64, 9);

        ats::BarkSpectralEnvelope rebuilt;
        rebuilt.build(source, 64, 9);
        for (size_t i = 0; i < probes.size(); ++i)
            if (rebuilt.sampleAtHz(probes[i]) != expected[i])
                return fail("reused DCT workspace changed an envelope sample");
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
        // Geometric frequency midpoint = 100 * sqrt(2); equal-power amplitude midpoint =
        // sqrt(0.5*0.25^2 + 0.5*1.0^2).
        if (!partial.active || !closeEnough(partial.frequencyHz, 100.0 * std::sqrt(2.0))
            || !closeEnough(partial.amplitude, std::sqrt(0.5 * 0.0625 + 0.5 * 1.0)))
        {
            return fail("matched morph should use log-frequency and equal-power blends");
        }
    }

    {
        const auto target = ats::resolveHybridizedPartialRender(ats::HybridizedPartialKind::UnmatchedTarget,
                                                                0.0,
                                                                0.0,
                                                                440.0,
                                                                0.8,
                                                                0.5);
        // Equal-power fade-in: amplitude = targetAmplitude * sqrt(amount) = 0.8 * sqrt(0.5).
        if (!target.active || !closeEnough(target.frequencyHz, 440.0)
            || !closeEnough(target.amplitude, 0.8 * std::sqrt(0.5)))
            return fail("hybrid transfer should not suppress target-only partials when morph amount is nonzero");
    }

    // reimposeRdcEnvelopeFrame: flattening a combed spectrum onto its smooth envelope.
    {
        constexpr int N = 16;
        const double minHz = 100.0, maxHz = 8000.0;
        std::vector<double> freqs(N), amps(N);
        for (int i = 0; i < N; ++i)
        {
            const double t = (double) i / (double) (N - 1);
            freqs[(size_t) i] = minHz * std::pow(maxHz / minHz, t); // log-spaced
            // Smooth underlying envelope: a single broad formant bump in dB.
            const double envDb = -6.0 + 18.0 * std::exp(-std::pow((t - 0.45) / 0.22, 2.0));
            // Comb ripple: alternate +/-6 dB about the trend.
            const double rippleDb = (i % 2 == 0) ? 6.0 : -6.0;
            amps[(size_t) i] = std::pow(10.0, (envDb + rippleDb) / 20.0);
        }

        auto roughness = [&](const std::vector<double>& a) {
            double acc = 0.0;
            for (int i = 1; i + 1 < N; ++i)
            {
                const double d2 = 20.0 * std::log10(a[(size_t)(i - 1)])
                                  - 40.0 * std::log10(a[(size_t) i])
                                  + 20.0 * std::log10(a[(size_t)(i + 1)]);
                acc += d2 * d2;
            }
            return acc;
        };

        const double before = roughness(amps);
        std::vector<double> smoothed = amps;
        if (!ats::reimposeRdcEnvelopeFrame(freqs, smoothed, 0.8, minHz, maxHz))
            return fail("reimposeRdcEnvelopeFrame should succeed with enough active partials");

        const double after = roughness(smoothed);
        if (!(after < before * 0.25))
            return fail("smoothing should substantially reduce comb ripple roughness");
        for (double v : smoothed)
            if (!std::isfinite(v) || v < 0.0)
                return fail("smoothed amplitudes must be finite and non-negative");
    }

    // Inactive partials are left untouched; too few active partials -> no-op false.
    {
        std::vector<double> freqs { 100.0, 200.0, 400.0 };
        std::vector<double> amps { 0.5, 0.0, 0.25 }; // only 2 active
        const std::vector<double> before = amps;
        if (ats::reimposeRdcEnvelopeFrame(freqs, amps, 0.5, 100.0, 400.0))
            return fail("fewer than three active partials should return false");
        if (amps != before)
            return fail("failed fit must leave amplitudes unchanged");
    }

    // softCapEnvelopeBoost: soft-knee ceiling on the envelope-transfer boost ratio.
    {
        const double src = 0.1;
        const double maxRatio = 31.6227766; // +30 dB, the live kMaxEnvBoostLinear
        const double kneeDb = 6.0;

        // Well below the knee: pass-through (no gain reduction).
        if (!closeEnough(ats::softCapEnvelopeBoost(0.4, src, maxRatio, kneeDb), 0.4, 1.0e-9))
            return fail("soft cap should pass boosts well below the ceiling unchanged");

        // Far above the ceiling: clamped exactly at src * maxRatio.
        if (!closeEnough(ats::softCapEnvelopeBoost(100.0, src, maxRatio, kneeDb), src * maxRatio, 1.0e-9))
            return fail("soft cap should clamp large boosts to source * maxRatio");

        // Output never exceeds the ceiling and is monotonically non-decreasing in the input.
        double prev = 0.0;
        for (double out = 0.0; out <= 20.0; out += 0.001)
        {
            const double capped = ats::softCapEnvelopeBoost(out, src, maxRatio, kneeDb);
            if (capped > src * maxRatio + 1.0e-9)
                return fail("soft cap output must never exceed the ceiling");
            if (capped + 1.0e-12 < prev)
                return fail("soft cap must be monotonic in the input amplitude");
            prev = capped;
        }

        // Continuity of value and slope at the lower knee edge (ceiling - knee/2).
        // ratio at lower edge in dB = 20log10(maxRatio) - 3 dB.
        const double ceilingDb = 20.0 * std::log10(maxRatio);
        const double lowerEdgeRatio = std::pow(10.0, (ceilingDb - 0.5 * kneeDb) / 20.0);
        const double eps = 1.0e-4;
        const double belowOut = src * lowerEdgeRatio * (1.0 - eps);
        const double aboveOut = src * lowerEdgeRatio * (1.0 + eps);
        const double belowCap = ats::softCapEnvelopeBoost(belowOut, src, maxRatio, kneeDb);
        const double aboveCap = ats::softCapEnvelopeBoost(aboveOut, src, maxRatio, kneeDb);
        // Just below the knee the map is identity, so value matches the input there.
        if (!closeEnough(belowCap, belowOut, 1.0e-6))
            return fail("soft cap should be identity just below the knee");
        // Crossing the knee must not jump (continuity).
        if (std::abs(aboveCap - belowCap) > 1.0e-3 * std::max(1.0, belowCap))
            return fail("soft cap must be continuous across the lower knee edge");

        // Degenerate inputs.
        if (ats::softCapEnvelopeBoost(-1.0, src, maxRatio, kneeDb) != 0.0)
            return fail("soft cap of a non-positive output should be zero");
        if (!closeEnough(ats::softCapEnvelopeBoost(0.5, 0.0, maxRatio, kneeDb), 0.5, 1.0e-12))
            return fail("soft cap with non-positive source should pass the output through");
    }

    std::cout << "ok: spectral envelope transfer helpers\n";
    return 0;
}

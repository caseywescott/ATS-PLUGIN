// Hatch (2004) §4.2 onset-aligned time warp for morphing.
#include "AtsOnsetAlignedWarp.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

void assertMonotonic(const ats::AutomationCurve& curve)
{
    for (std::size_t i = 1; i < curve.points.size(); ++i)
    {
        assert(curve.points[i].x > curve.points[i - 1].x);
        assert(curve.points[i].y >= curve.points[i - 1].y);
    }
    if (!curve.points.empty())
    {
        assert(curve.points.front().x == 0.0f && curve.points.front().y == 0.0f);
        assert(curve.points.back().x == 1.0f && curve.points.back().y == 1.0f);
    }
}

// Every segment's slope d(source seconds)/d(output seconds) within [1/R, R].
void assertSlopes(const ats::AutomationCurve& curve, double outputDuration, double sourceDuration, double ratio)
{
    for (std::size_t i = 1; i < curve.points.size(); ++i)
    {
        const double dx = (static_cast<double>(curve.points[i].x) - curve.points[i - 1].x) * outputDuration;
        const double dy = (static_cast<double>(curve.points[i].y) - curve.points[i - 1].y) * sourceDuration;
        assert(dx > 0.0);
        const double slope = dy / dx;
        assert(slope >= 1.0 / ratio - 1.0e-3);
        assert(slope <= ratio + 1.0e-3);
    }
}

} // namespace

int main()
{
    const std::vector<double> onsetsA { 0.5, 1.5 };
    const std::vector<double> onsetsB { 0.7, 1.2 };

    // alpha = 0.5: targets {0.6, 1.35}, both sources' onsets land on them.
    {
        ats::OnsetAlignedWarpParams params;
        params.morphAmount = 0.5;
        params.durationA = 2.0;
        params.durationB = 2.0;
        const auto warp = ats::buildOnsetAlignedWarp(onsetsA, onsetsB, params);
        assert(near(warp.outputDuration, 2.0, 1.0e-12));
        assert(warp.pairedOnsets == 2);
        assert(warp.targetOnsetSeconds.size() == 2);
        assert(near(warp.targetOnsetSeconds[0], 0.6, 1.0e-12));
        assert(near(warp.targetOnsetSeconds[1], 1.35, 1.0e-12));
        const double D = warp.outputDuration;
        assert(near(warp.warpA.valueAt(0.6 / D, 0.6 / D), 0.5 / 2.0, 1.0e-5));
        assert(near(warp.warpB.valueAt(0.6 / D, 0.6 / D), 0.7 / 2.0, 1.0e-5));
        assert(near(warp.warpA.valueAt(1.35 / D, 1.35 / D), 1.5 / 2.0, 1.0e-5));
        assert(near(warp.warpB.valueAt(1.35 / D, 1.35 / D), 1.2 / 2.0, 1.0e-5));
        assertMonotonic(warp.warpA);
        assertMonotonic(warp.warpB);
        assertSlopes(warp.warpA, D, 2.0, params.maxWarpRatio);
        assertSlopes(warp.warpB, D, 2.0, params.maxWarpRatio);

        // Attack protection: 5 ms after each onset runs at slope 1 (in seconds).
        const double holdEnd = (0.6 + params.attackHoldSeconds) / D;
        assert(near(warp.warpA.valueAt(holdEnd, holdEnd), (0.5 + params.attackHoldSeconds) / 2.0, 1.0e-5));
        assert(near(warp.warpB.valueAt(holdEnd, holdEnd), (0.7 + params.attackHoldSeconds) / 2.0, 1.0e-5));
        // endpoints + (onset + hold) x 2
        assert(warp.warpA.points.size() == 6 && warp.warpB.points.size() == 6);

        // Deterministic.
        const auto again = ats::buildOnsetAlignedWarp(onsetsA, onsetsB, params);
        assert(again.targetOnsetSeconds == warp.targetOnsetSeconds);
        assert(again.warpA.points.size() == warp.warpA.points.size());
    }

    // alpha = 0: A keeps its timing (identity -> empty curve), B is pulled onto A's onsets.
    {
        ats::OnsetAlignedWarpParams params;
        params.morphAmount = 0.0;
        params.durationA = 2.0;
        params.durationB = 2.0;
        const auto warp = ats::buildOnsetAlignedWarp(onsetsA, onsetsB, params);
        assert(near(warp.outputDuration, 2.0, 1.0e-12));
        assert(near(warp.targetOnsetSeconds[0], 0.5, 1.0e-12) && near(warp.targetOnsetSeconds[1], 1.5, 1.0e-12));
        if (!warp.warpA.empty())
            for (double x = 0.0; x <= 1.0; x += 0.05)
                assert(near(warp.warpA.valueAt(x, x), x, 1.0e-5));
        assert(near(warp.warpB.valueAt(0.25, 0.25), 0.35, 1.0e-5));
        assert(near(warp.warpB.valueAt(0.75, 0.75), 0.60, 1.0e-5));
        assertMonotonic(warp.warpB);
    }

    // alpha = 1 with unequal durations: D = DB, B identity, A stretched onto B's grid.
    {
        ats::OnsetAlignedWarpParams params;
        params.morphAmount = 1.0;
        params.durationA = 2.0;
        params.durationB = 3.0;
        const auto warp = ats::buildOnsetAlignedWarp(onsetsA, { 1.0, 2.0 }, params);
        assert(near(warp.outputDuration, 3.0, 1.0e-12));
        assert(warp.warpB.empty());
        assert(near(warp.warpA.valueAt(1.0 / 3.0, 0.0), 0.25, 1.0e-5));
        assert(near(warp.warpA.valueAt(2.0 / 3.0, 0.0), 0.75, 1.0e-5));
        assertMonotonic(warp.warpA);
        assertSlopes(warp.warpA, 3.0, 2.0, params.maxWarpRatio);
    }

    // Slope clamp: an onset 0.2 s into A has to land at 1.5 s on the output; the raw
    // slope 0.2 / 1.5 is below 1/4, so the anchor is moved to 1.5 / 4 = 0.375 s.
    {
        ats::OnsetAlignedWarpParams params;
        params.morphAmount = 1.0;
        params.durationA = 2.0;
        params.durationB = 2.0;
        const auto warp = ats::buildOnsetAlignedWarp({ 0.2 }, { 1.5 }, params);
        assert(warp.pairedOnsets == 1);
        assert(near(warp.targetOnsetSeconds[0], 1.5, 1.0e-12));
        assertMonotonic(warp.warpA);
        assertSlopes(warp.warpA, 2.0, 2.0, params.maxWarpRatio);
        assert(near(warp.warpA.valueAt(0.75, 0.75), 0.375 / 2.0, 1.0e-5));
        assert(!near(warp.warpA.valueAt(0.75, 0.75), 0.1, 1.0e-3));

        // A looser ratio leaves the anchor where the onset asks for it.
        params.maxWarpRatio = 16.0;
        const auto loose = ats::buildOnsetAlignedWarp({ 0.2 }, { 1.5 }, params);
        assert(near(loose.warpA.valueAt(0.75, 0.75), 0.1, 1.0e-5));
        assertSlopes(loose.warpA, 2.0, 2.0, params.maxWarpRatio);
    }

    // Different onset counts: greedy nearest by normalised time, the rest ignored.
    {
        ats::OnsetAlignedWarpParams params;
        params.morphAmount = 0.5;
        params.durationA = 2.0;
        params.durationB = 2.0;
        const auto warp = ats::buildOnsetAlignedWarp({ 0.5 }, { 0.4, 1.6 }, params);
        assert(warp.pairedOnsets == 1);
        assert(near(warp.targetOnsetSeconds[0], 0.45, 1.0e-12));
        assert(near(warp.warpA.valueAt(0.225, 0.0), 0.25, 1.0e-5));
        assert(near(warp.warpB.valueAt(0.225, 0.0), 0.20, 1.0e-5));
        assertMonotonic(warp.warpA);
        assertMonotonic(warp.warpB);
    }

    // No onsets -> empty (identity) curves; invalid durations -> nothing.
    {
        ats::OnsetAlignedWarpParams params;
        params.durationA = 2.0;
        params.durationB = 2.0;
        const auto none = ats::buildOnsetAlignedWarp({}, {}, params);
        assert(none.warpA.empty() && none.warpB.empty());
        assert(none.pairedOnsets == 0 && none.targetOnsetSeconds.empty());
        assert(near(none.outputDuration, 2.0, 1.0e-12));
        const auto oneSided = ats::buildOnsetAlignedWarp(onsetsA, {}, params);
        assert(oneSided.warpA.empty() && oneSided.warpB.empty() && oneSided.pairedOnsets == 0);

        params.durationB = 0.0;
        const auto invalid = ats::buildOnsetAlignedWarp(onsetsA, onsetsB, params);
        assert(invalid.warpA.empty() && invalid.outputDuration == 0.0);
    }

    std::cout << "ats_onset_aligned_warp_test: ok\n";
    return 0;
}

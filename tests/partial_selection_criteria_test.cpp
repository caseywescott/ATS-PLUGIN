#include "../src/ui/PartialSelectionCriteria.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace ats::ui;

int main()
{
    // Field parsing: blank is "unused", junk is invalid.
    std::optional<double> v;
    assert(parseCriterion("", v) && !v);
    assert(parseCriterion("   ", v) && !v);
    assert(parseCriterion(" 440 ", v) && v && *v == 440.0);
    assert(parseCriterion("-60.5", v) && v && *v == -60.5);
    assert(!parseCriterion("abc", v) && !v);
    assert(!parseCriterion("12x", v));
    assert(!parseCriterion("inf", v));

    // Builder summaries: level is the mean active amplitude in dB, duration
    // counts active frames over the model duration.
    SpectralTraceInput input;
    input.durationSeconds = 1.0;
    input.frameCount = 10;
    const auto addPartial = [&input](int index, double hz, double amp, int activeFrames, double smr) {
        SpectralPartialInput p;
        p.partialIndex = index;
        p.meanSmrDb = smr;
        for (int f = 0; f < 10; ++f)
            p.frames.push_back(f < activeFrames ? SpectralPartialFrame { hz, amp } : SpectralPartialFrame {});
        input.partials.push_back(p);
    };
    addPartial(0, 110.0, 0.5, 10, 40.0);   // loud, long, low
    addPartial(1, 880.0, 0.01, 2, 10.0);   // quiet, short, high
    addPartial(2, 440.0, 0.1, 5, std::numeric_limits<double>::quiet_NaN()); // SMR unknown
    const auto snapshot = SpectralTraceBuilder::build(input);
    for (const auto& trace : snapshot.partials)
    {
        if (trace.partialIndex == 0)
        {
            assert(std::abs(trace.meanLevelDb - 20.0 * std::log10(0.5)) < 1.0e-9);
            assert(std::abs(trace.activeSeconds - 1.0) < 1.0e-9);
            assert(trace.meanSmrDb == 40.0);
        }
        if (trace.partialIndex == 1)
            assert(std::abs(trace.activeSeconds - 0.2) < 1.0e-9);
    }

    const auto match = [&snapshot](PartialCriteria c) { return matchPartials(snapshot, c); };
    assert(match({}).empty()); // nothing set selects nothing

    PartialCriteria band;
    band.minFrequencyHz = 300.0;
    band.maxFrequencyHz = 1000.0;
    assert((match(band) == std::vector<int> { 1, 2 }));
    std::swap(band.minFrequencyHz, band.maxFrequencyHz); // reversed bounds are normalised
    assert((match(band) == std::vector<int> { 1, 2 }));

    PartialCriteria loud;
    loud.minLevelDb = -30.0;
    assert((match(loud) == std::vector<int> { 0, 2 }));

    PartialCriteria longOnes;
    longOnes.minSeconds = 0.4;
    assert((match(longOnes) == std::vector<int> { 0, 2 }));
    PartialCriteria shortOnes;
    shortOnes.maxSeconds = 0.3;
    assert((match(shortOnes) == std::vector<int> { 1 }));

    PartialCriteria masked;
    masked.minSmrDb = 5.0; // the partial with unknown SMR never qualifies
    assert((match(masked) == std::vector<int> { 0, 1 }));

    PartialCriteria combined;
    combined.minFrequencyHz = 100.0;
    combined.minLevelDb = -30.0;
    combined.minSmrDb = 20.0;
    assert((match(combined) == std::vector<int> { 0 }));

    assert((invertPartials(snapshot, { 1 }) == std::vector<int> { 0, 2 }));
    assert(invertPartials(snapshot, { 0, 1, 2 }).empty());
    assert((unitePartials({ 2, 0 }, { 0, 1 }) == std::vector<int> { 0, 1, 2 }));

    // Noisy-partial criterion (O'Sullivan & Fernstrom, ICAD 2003): the share
    // of interior frames with a positive amplitude second difference.  A
    // wiggly envelope alternating 0.5 / 0.3 is convex on every other frame
    // (ratio 0.5); a linear decay has zero curvature (ratio 0).
    SpectralTraceInput noisy;
    noisy.durationSeconds = 1.0;
    noisy.frameCount = 10;
    {
        SpectralPartialInput wiggly;
        wiggly.partialIndex = 0;
        for (int f = 0; f < 10; ++f)
            wiggly.frames.push_back(SpectralPartialFrame { 2000.0, f % 2 == 0 ? 0.5 : 0.3 });
        SpectralPartialInput smooth;
        smooth.partialIndex = 1;
        for (int f = 0; f < 10; ++f) // 1/16 steps are exact in binary: zero curvature, no roundoff
            smooth.frames.push_back(SpectralPartialFrame { 3000.0, 1.0 - 0.0625 * f });
        SpectralPartialInput brief; // two active frames: no interior frame, ratio 0
        brief.partialIndex = 2;
        for (int f = 0; f < 10; ++f)
            brief.frames.push_back(f < 2 ? SpectralPartialFrame { 500.0, 0.4 } : SpectralPartialFrame {});
        noisy.partials = { wiggly, smooth, brief };
    }
    const auto noisySnapshot = SpectralTraceBuilder::build(noisy);
    for (const auto& trace : noisySnapshot.partials)
    {
        if (trace.partialIndex == 0)
            assert(std::abs(trace.activityRatio - 0.5) < 1.0e-12);
        if (trace.partialIndex == 1)
            assert(trace.activityRatio == 0.0);
        if (trace.partialIndex == 2)
            assert(trace.activityRatio == 0.0);
    }
    // The first snapshot's constant / flat envelopes all score 0.
    for (const auto& trace : snapshot.partials)
        assert(trace.activityRatio == 0.0);

    PartialCriteria active;
    assert(active.empty());
    active.minActivity = 0.3;
    assert(!active.empty());
    assert((matchPartials(noisySnapshot, active) == std::vector<int> { 0 }));
    active.minActivity = 0.0; // every active partial has ratio >= 0
    assert((matchPartials(noisySnapshot, active) == std::vector<int> { 0, 1, 2 }));
    active.minActivity = 0.3;
    active.minFrequencyHz = 2500.0; // combined with another bound: nothing is both
    assert(matchPartials(noisySnapshot, active).empty());

    std::cout << "partial_selection_criteria_test: ok\n";
    return 0;
}

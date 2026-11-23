// Hatch (2004) §4.4 harmonic alignment: harmonic-locked morph correspondence.
#include "AtsMorphHarmonicCorrespondence.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

std::vector<ats::MorphBandAssignment> series(double f0, int harmonics, int frameIndex = 0,
                                             int detunedHarmonic = 0, double detuneRatio = 1.0)
{
    std::vector<ats::MorphBandAssignment> out;
    for (int h = 1; h <= harmonics; ++h)
    {
        ats::MorphBandAssignment a;
        a.partialIndex = h - 1;
        a.frameIndex = frameIndex;
        a.frequencyHz = f0 * h * (h == detunedHarmonic ? detuneRatio : 1.0);
        a.amplitude = 1.0 / h;
        a.bandIndex = h; // any non-negative band; the harmonic builder labels by f0, not band
        a.kind = ats::MorphBandAssignmentKind::Assigned;
        out.push_back(a);
    }
    return out;
}

int countKind(const std::vector<ats::MorphCorrespondenceRecord>& records, ats::MorphCorrespondenceKind kind)
{
    int n = 0;
    for (const auto& r : records)
        if (r.kind == kind)
            ++n;
    return n;
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

} // namespace

int main()
{
    using ats::MorphCorrespondenceKind;

    // A = harmonics 1..8 of 200 Hz, B = harmonics 1..6 of 300 Hz, B's h=3 detuned +2%.
    {
        const auto a = series(200.0, 8);
        const auto b = series(300.0, 6, 0, 3, 1.02);
        const auto result = ats::buildHarmonicLockedCorrespondence(a, b);
        assert(!result.usedGrid);
        assert(near(result.fundamentalSourceHz, 200.0, 0.5));
        assert(near(result.fundamentalTargetHz, 300.0, 2.0));
        assert(result.matched == 6);
        assert(result.unmatchedSource == 2);
        assert(result.unmatchedTarget == 0);
        assert(countKind(result.records, MorphCorrespondenceKind::Matched) == 6);
        assert(countKind(result.records, MorphCorrespondenceKind::UnmatchedSource) == 2);
        assert(result.records.size() == 8);

        for (const auto& r : result.records)
        {
            if (r.kind == MorphCorrespondenceKind::Matched)
            {
                // Harmonic h of A pairs with harmonic h of B: 200<->300, 400<->600, ...
                assert(r.rank >= 1 && r.rank <= 6);
                assert(near(r.sourceFrequencyHz, 200.0 * r.rank, 1.0e-9));
                const double expectedB = 300.0 * r.rank * (r.rank == 3 ? 1.02 : 1.0);
                assert(near(r.targetFrequencyHz, expectedB, 1.0e-9));
                assert(r.sourcePartialIndex == r.rank - 1 && r.targetPartialIndex == r.rank - 1);
                assert(r.bandIndex == r.rank); // band index comes from the source assignment
                assert(r.hasCostDiagnostics);
                assert(r.logFrequencyCost >= 0.0 && r.logFrequencyCost <= std::log(1.0595));
                assert(r.totalCost == r.logFrequencyCost);
                assert(r.sourceAmplitude > 0.0 && r.targetAmplitude > 0.0);
            }
            else
            {
                // A's 7th and 8th harmonics have no partner: zero-amplitude target side.
                assert(r.kind == MorphCorrespondenceKind::UnmatchedSource);
                assert(r.rank == 7 || r.rank == 8);
                assert(near(r.sourceFrequencyHz, 200.0 * r.rank, 1.0e-9));
                assert(r.targetPartialIndex == -1 && r.targetAmplitude == 0.0 && r.targetFrequencyHz == 0.0);
                assert(!r.hasCostDiagnostics);
            }
        }

        // Deterministic.
        const auto again = ats::buildHarmonicLockedCorrespondence(a, b);
        assert(again.records == result.records);
    }

    // A 10% detuned partial is rejected at the semitone threshold, accepted at 1.2.
    {
        const auto a = series(200.0, 4);
        const auto b = series(300.0, 4, 0, 3, 1.10);
        ats::HarmonicCorrespondenceOptions options;
        options.fundamentalSourceHz = 200.0;
        options.fundamentalTargetHz = 300.0;

        const auto strict = ats::buildHarmonicLockedCorrespondence(a, b, options);
        assert(strict.matched == 3);
        assert(strict.unmatchedSource == 1 && strict.unmatchedTarget == 1);
        bool sawRejectedSource = false, sawRejectedTarget = false;
        for (const auto& r : strict.records)
        {
            if (r.kind == MorphCorrespondenceKind::UnmatchedSource)
            {
                sawRejectedSource = true;
                assert(r.rank == 3 && near(r.sourceFrequencyHz, 600.0, 1.0e-9));
            }
            if (r.kind == MorphCorrespondenceKind::UnmatchedTarget)
            {
                sawRejectedTarget = true;
                assert(r.rank == 3 && near(r.targetFrequencyHz, 990.0, 1.0e-9));
                assert(r.sourcePartialIndex == -1 && r.sourceAmplitude == 0.0);
            }
        }
        assert(sawRejectedSource && sawRejectedTarget);

        options.ratioThreshold = 1.2;
        const auto loose = ats::buildHarmonicLockedCorrespondence(a, b, options);
        assert(loose.matched == 4 && loose.unmatchedSource == 0 && loose.unmatchedTarget == 0);
        for (const auto& r : loose.records)
            if (r.rank == 3)
                assert(near(r.logFrequencyCost, std::log(1.10), 1.0e-9));
    }

    // Octave wrap: 200 Hz A series vs 400 Hz B series.
    {
        const auto a = series(200.0, 8);
        const auto b = series(400.0, 6);

        // Without wrap: pitch-normalised harmonic numbers pair (200<->400, 400<->800, ...).
        const auto plain = ats::buildHarmonicLockedCorrespondence(a, b);
        assert(plain.matched == 6 && plain.unmatchedSource == 2 && plain.unmatchedTarget == 0);
        for (const auto& r : plain.records)
            if (r.kind == MorphCorrespondenceKind::Matched)
                assert(near(r.targetFrequencyHz, 2.0 * r.sourceFrequencyHz, 1.0e-9));

        // With wrap: harmonic h of A pairs with harmonic h/2 of B at the same absolute
        // frequency (400<->400, 800<->800, 1200<->1200, 1600<->1600).
        ats::HarmonicCorrespondenceOptions options;
        options.octaveWrap = true;
        const auto wrapped = ats::buildHarmonicLockedCorrespondence(a, b, options);
        assert(wrapped.matched == 4);
        assert(wrapped.unmatchedSource == 4); // A's odd harmonics 200, 600, 1000, 1400
        assert(wrapped.unmatchedTarget == 2); // B's 2000, 2400 lie above A's range
        for (const auto& r : wrapped.records)
        {
            if (r.kind == MorphCorrespondenceKind::Matched)
            {
                assert(near(r.targetFrequencyHz, r.sourceFrequencyHz, 1.0e-9));
                assert(r.rank % 2 == 0);
                assert(near(r.logFrequencyCost, 0.0, 1.0e-12));
            }
            else if (r.kind == MorphCorrespondenceKind::UnmatchedSource)
                assert(r.rank % 2 == 1);
        }

        // Less than an octave apart the wrap is a no-op.
        const auto close = ats::buildHarmonicLockedCorrespondence(series(200.0, 4), series(300.0, 4), options);
        assert(close.matched == 4);
    }

    // Grid fallback: a single-partial side cannot yield a fundamental.
    {
        const auto a = series(440.0, 1);
        const auto b = series(300.0, 4);
        ats::HarmonicCorrespondenceOptions options;
        options.gridHz = 100.0;
        const auto result = ats::buildHarmonicLockedCorrespondence(a, b, options);
        assert(result.usedGrid);
        assert(result.fundamentalSourceHz == 100.0 && result.fundamentalTargetHz == 100.0);
        // On the 100 Hz grid A's 440 Hz is label 4; B's 300/600/900/1200 are labels 3/6/9/12.
        assert(result.matched == 0);
        assert(result.unmatchedSource == 1 && result.unmatchedTarget == 4);
        for (const auto& r : result.records)
            if (r.kind == MorphCorrespondenceKind::UnmatchedSource)
                assert(r.rank == 4);

        // The grid still pairs partials that share a grid label within the threshold.
        const auto gridMatch = ats::buildHarmonicLockedCorrespondence(series(400.0, 1), series(410.0, 1), options);
        assert(gridMatch.usedGrid && gridMatch.matched == 1);

        // Empty input: nothing to pair, grid reported.
        const auto empty = ats::buildHarmonicLockedCorrespondence({}, {}, options);
        assert(empty.records.empty() && empty.usedGrid && empty.matched == 0);
    }

    // Inactive / silent assignments are ignored like the other builders do.
    {
        auto a = series(200.0, 4);
        a[1].kind = ats::MorphBandAssignmentKind::Silent;
        const auto result = ats::buildHarmonicLockedCorrespondence(a, series(300.0, 4));
        assert(result.matched == 3 && result.unmatchedTarget == 1);
    }

    // Serialisation round trip of the new enum value through the recipe name helpers.
    {
        const std::string name = ats::morphCorrespondenceStrategyName(ats::MorphCorrespondenceStrategy::HarmonicLocked);
        assert(name == "harmonic_locked");
        ats::MorphCorrespondenceStrategy parsed = ats::MorphCorrespondenceStrategy::FrequencyRank;
        assert(ats::morphCorrespondenceStrategyFromName(name, parsed));
        assert(parsed == ats::MorphCorrespondenceStrategy::HarmonicLocked);
        assert(!ats::morphCorrespondenceStrategyFromName("not_a_strategy", parsed));
        assert(parsed == ats::MorphCorrespondenceStrategy::HarmonicLocked);
        for (auto strategy : { ats::MorphCorrespondenceStrategy::FrequencyRank,
                               ats::MorphCorrespondenceStrategy::GreedyNearest,
                               ats::MorphCorrespondenceStrategy::AssignmentMatrix,
                               ats::MorphCorrespondenceStrategy::HarmonicLocked })
        {
            ats::MorphCorrespondenceStrategy back = ats::MorphCorrespondenceStrategy::HarmonicLocked;
            assert(ats::morphCorrespondenceStrategyFromName(ats::morphCorrespondenceStrategyName(strategy), back));
            assert(back == strategy);
        }
    }

    // Host-shaped input: one entry per partial, each at its own peak frame
    // (makeMorphAssignments). A per-frame "lowest" median read ~2 kHz here.
    {
        auto a = series(100.0, 40);
        auto b = series(150.0, 30);
        for (std::size_t i = 0; i < a.size(); ++i) a[i].frameIndex = static_cast<int>(i * 3);
        for (std::size_t i = 0; i < b.size(); ++i) b[i].frameIndex = static_cast<int>(i * 5 + 1);
        const auto result = ats::buildHarmonicLockedCorrespondence(a, b);
        assert(!result.usedGrid);
        assert(near(result.fundamentalSourceHz, 100.0, 1.0));
        assert(near(result.fundamentalTargetHz, 150.0, 1.5));
        assert(result.matched == 30 && result.unmatchedSource == 10 && result.unmatchedTarget == 0);
        for (const auto& r : result.records)
            if (r.kind == ats::MorphCorrespondenceKind::Matched)
                assert(near(r.targetFrequencyHz / r.sourceFrequencyHz, 1.5, 1.0e-9));
    }
    // A missing fundamental must not halve the estimate: harmonics 2..10 of 200 Hz.
    {
        auto a = series(200.0, 10);
        a.erase(a.begin());
        const auto result = ats::buildHarmonicLockedCorrespondence(a, series(200.0, 10));
        assert(near(result.fundamentalSourceHz, 200.0, 2.0));
    }

    std::cout << "morph_harmonic_correspondence_test: ok\n";
    return 0;
}

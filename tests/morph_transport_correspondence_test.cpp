// Numerical gates for the morph-domain optimal-transport correspondence adapter
// (AtsMorphTransportCorrespondence): identity reproduces self-pairing, per-band
// energy conservation, count-mismatch mass-splitting (no abrupt birth/death),
// band isolation, and determinism.

#include "AtsMorphTransportCorrespondence.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "morph_transport_correspondence_test: " << message << '\n';
    return 1;
}

bool approxEqual(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

ats::MorphBandAssignment makeAssignment(int partial, int band, double freq, double amp)
{
    ats::MorphBandAssignment a;
    a.partialIndex = partial;
    a.frameIndex = 0;
    a.frequencyHz = freq;
    a.amplitude = amp;
    a.bandIndex = band;
    a.kind = ats::MorphBandAssignmentKind::Assigned;
    return a;
}

double energy(double amp) { return amp * amp; }

} // namespace

int main()
{
    using namespace ats;

    // 1. Identity: A == B. Every edge is Matched and pairs a partial with itself
    //    (zero log-frequency cost), no fade-in/out, transported energy == total.
    {
        std::vector<MorphBandAssignment> a = {
            makeAssignment(0, 3, 220.0, 0.5),
            makeAssignment(1, 3, 260.0, 0.3),
            makeAssignment(2, 7, 900.0, 0.4),
        };
        const MorphTransportCorrespondence corr = buildOptimalTransportCorrespondence(a, a);
        if (!approxEqual(corr.transportCost, 0.0, 1e-9))
            return fail("identity: expected zero transport cost");
        for (const auto& e : corr.edges)
        {
            if (e.kind != MorphCorrespondenceKind::Matched)
                return fail("identity: expected only matched edges");
            if (e.sourcePartialIndex != e.targetPartialIndex)
                return fail("identity: expected self-pairing");
        }
        const double total = energy(0.5) + energy(0.3) + energy(0.4);
        if (!approxEqual(corr.transportedMass, total, 1e-9))
            return fail("identity: transported energy != total energy");
    }

    // 2. Per-band energy conservation. For each partial, matched + unmatched mass
    //    equals its energy; transported + leftover accounts for all energy.
    {
        std::vector<MorphBandAssignment> src = {
            makeAssignment(0, 5, 400.0, 0.6),
            makeAssignment(1, 5, 480.0, 0.2),
        };
        std::vector<MorphBandAssignment> tgt = {
            makeAssignment(0, 5, 410.0, 0.3),
            makeAssignment(1, 5, 470.0, 0.3),
            makeAssignment(2, 5, 520.0, 0.2),
        };
        const MorphTransportCorrespondence corr = buildOptimalTransportCorrespondence(
            src, tgt, MorphTransportCorrespondenceOptions{ 0.0 });

        std::map<int, double> srcAccount, tgtAccount;
        for (const auto& e : corr.edges)
        {
            if (e.kind == MorphCorrespondenceKind::Matched)
            {
                srcAccount[e.sourcePartialIndex] += e.mass;
                tgtAccount[e.targetPartialIndex] += e.mass;
            }
            else if (e.kind == MorphCorrespondenceKind::UnmatchedSource)
                srcAccount[e.sourcePartialIndex] += e.mass;
            else
                tgtAccount[e.targetPartialIndex] += e.mass;
        }
        if (!approxEqual(srcAccount[0], energy(0.6), 1e-9) || !approxEqual(srcAccount[1], energy(0.2), 1e-9))
            return fail("conservation: source energy not fully accounted");
        if (!approxEqual(tgtAccount[0], energy(0.3), 1e-9) || !approxEqual(tgtAccount[1], energy(0.3), 1e-9)
            || !approxEqual(tgtAccount[2], energy(0.2), 1e-9))
            return fail("conservation: target energy not fully accounted");
    }

    // 3. Count mismatch within a band splits mass instead of abrupt birth/death.
    //    2 sources -> 3 targets (equal total energy) must fan at least one source
    //    across multiple targets, with no unmatched leftover.
    {
        std::vector<MorphBandAssignment> src = {
            makeAssignment(0, 4, 300.0, std::sqrt(0.5)),
            makeAssignment(1, 4, 360.0, std::sqrt(0.5)),
        };
        std::vector<MorphBandAssignment> tgt = {
            makeAssignment(0, 4, 305.0, std::sqrt(1.0 / 3.0)),
            makeAssignment(1, 4, 330.0, std::sqrt(1.0 / 3.0)),
            makeAssignment(2, 4, 365.0, std::sqrt(1.0 / 3.0)),
        };
        const MorphTransportCorrespondence corr = buildOptimalTransportCorrespondence(src, tgt);

        std::map<int, int> fanout;
        bool anyUnmatched = false;
        for (const auto& e : corr.edges)
        {
            if (e.kind == MorphCorrespondenceKind::Matched)
                ++fanout[e.sourcePartialIndex];
            else
                anyUnmatched = true;
        }
        bool anySplit = false;
        for (const auto& kv : fanout)
            if (kv.second > 1) anySplit = true;
        if (!anySplit)
            return fail("mismatch: expected a source energy to split across targets");
        if (anyUnmatched)
            return fail("mismatch: equal-energy bands should leave no fade in/out");
    }

    // 4. Band isolation: energy never crosses a band boundary, and a band present
    //    on only one side becomes pure fade-out / fade-in.
    {
        std::vector<MorphBandAssignment> src = {
            makeAssignment(0, 2, 150.0, 0.5),   // band 2 source-only
            makeAssignment(1, 9, 1200.0, 0.4),  // band 9 shared
        };
        std::vector<MorphBandAssignment> tgt = {
            makeAssignment(0, 9, 1180.0, 0.4),  // band 9 shared
            makeAssignment(1, 14, 3000.0, 0.6), // band 14 target-only
        };
        const MorphTransportCorrespondence corr = buildOptimalTransportCorrespondence(src, tgt);

        bool band2Faded = false, band14Faded = false, band9Matched = false;
        for (const auto& e : corr.edges)
        {
            if (e.kind == MorphCorrespondenceKind::Matched && e.bandIndex != 9)
                return fail("band isolation: matched edge outside the shared band");
            if (e.bandIndex == 2 && e.kind == MorphCorrespondenceKind::UnmatchedSource)
                band2Faded = true;
            if (e.bandIndex == 14 && e.kind == MorphCorrespondenceKind::UnmatchedTarget)
                band14Faded = true;
            if (e.bandIndex == 9 && e.kind == MorphCorrespondenceKind::Matched)
                band9Matched = true;
        }
        if (!band2Faded) return fail("band isolation: source-only band 2 should fade out");
        if (!band14Faded) return fail("band isolation: target-only band 14 should fade in");
        if (!band9Matched) return fail("band isolation: shared band 9 should match");
    }

    // 5. Determinism: shuffling the input order yields an identical edge list.
    {
        std::vector<MorphBandAssignment> src = {
            makeAssignment(0, 5, 400.0, 0.6), makeAssignment(1, 5, 480.0, 0.2),
            makeAssignment(2, 8, 1000.0, 0.5), makeAssignment(3, 8, 1100.0, 0.3),
        };
        std::vector<MorphBandAssignment> tgt = {
            makeAssignment(0, 5, 410.0, 0.3), makeAssignment(1, 5, 470.0, 0.3),
            makeAssignment(2, 8, 1050.0, 0.4),
        };
        const MorphTransportCorrespondence ref = buildOptimalTransportCorrespondence(src, tgt);

        std::mt19937 rng(987654);
        auto s2 = src, t2 = tgt;
        std::shuffle(s2.begin(), s2.end(), rng);
        std::shuffle(t2.begin(), t2.end(), rng);
        const MorphTransportCorrespondence again = buildOptimalTransportCorrespondence(s2, t2);

        if (ref.edges.size() != again.edges.size())
            return fail("determinism: edge count changed under shuffle");
        for (size_t k = 0; k < ref.edges.size(); ++k)
        {
            const auto& x = ref.edges[k];
            const auto& y = again.edges[k];
            if (x.bandIndex != y.bandIndex || x.kind != y.kind
                || x.sourcePartialIndex != y.sourcePartialIndex
                || x.targetPartialIndex != y.targetPartialIndex
                || !approxEqual(x.mass, y.mass, 1e-12))
                return fail("determinism: edge list differs under shuffle");
        }
        if (!approxEqual(ref.transportCost, again.transportCost, 1e-12))
            return fail("determinism: transport cost differs under shuffle");
    }

    std::cout << "morph_transport_correspondence_test: OK\n";
    return 0;
}

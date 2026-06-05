// Numerical gates for the 1D optimal-transport morph correspondence
// (AtsMorphTransport). Covers Commit 11's checks: identity when A==B, mass
// conservation, monotone (pop-free) continuity in morph amount, optimality vs
// greedy nearest pairing on count-mismatched pairs, and determinism.

#include "AtsMorphTransport.h"

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
    std::cerr << "morph_transport_test: " << message << '\n';
    return 1;
}

bool approxEqual(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

std::vector<ats::TransportPoint> makePoints(const std::vector<double>& pos,
                                            const std::vector<double>& mass)
{
    std::vector<ats::TransportPoint> pts;
    for (size_t i = 0; i < pos.size(); ++i)
        pts.push_back({ pos[i], mass[i], static_cast<int>(i) });
    return pts;
}

// Greedy nearest-neighbour transport: for each source (heaviest first), pour its
// mass into the nearest target with remaining capacity. Used as the baseline the
// OT plan must not be worse than. Returns total mass*distance.
double greedyNearestCost(std::vector<ats::TransportPoint> src,
                         std::vector<ats::TransportPoint> tgt)
{
    auto norm = [](std::vector<ats::TransportPoint>& v) {
        double t = 0.0;
        for (auto& p : v) t += p.mass;
        if (t > 0.0) for (auto& p : v) p.mass /= t;
    };
    norm(src);
    norm(tgt);
    std::sort(src.begin(), src.end(), [](const auto& a, const auto& b) { return a.mass > b.mass; });

    double cost = 0.0;
    for (auto& s : src)
    {
        double remaining = s.mass;
        while (remaining > 1e-12)
        {
            int best = -1;
            double bestDist = 0.0;
            for (size_t j = 0; j < tgt.size(); ++j)
            {
                if (tgt[j].mass <= 1e-12) continue;
                const double d = std::fabs(s.position - tgt[j].position);
                if (best < 0 || d < bestDist) { best = static_cast<int>(j); bestDist = d; }
            }
            if (best < 0) break;
            const double w = std::min(remaining, tgt[best].mass);
            cost += w * bestDist;
            remaining -= w;
            tgt[best].mass -= w;
        }
    }
    return cost;
}

} // namespace

int main()
{
    using namespace ats;

    // 1. Identity: A == B. One coupling per point, pairing equal indices, zero
    //    cost, unit transported mass.
    {
        auto a = makePoints({ 1.0, 3.0, 7.0, 12.0 }, { 0.4, 0.1, 0.3, 0.2 });
        const TransportPlan plan = buildOptimalTransportPlan(a, a);
        if (!approxEqual(plan.transportCost, 0.0, 1e-12))
            return fail("identity: expected zero transport cost");
        if (!approxEqual(plan.transportedMass, 1.0, 1e-9))
            return fail("identity: expected unit transported mass");
        for (const auto& c : plan.couplings)
            if (c.sourceIndex != c.targetIndex)
                return fail("identity: expected each point to map to itself");
    }

    // 2. Mass conservation. Per-source out-mass equals its normalized mass,
    //    per-target in-mass equals its normalized mass, totals sum to one.
    {
        const std::vector<double> sm = { 0.5, 0.2, 0.3 };
        const std::vector<double> tm = { 0.25, 0.25, 0.25, 0.25 };
        auto src = makePoints({ 2.0, 5.0, 9.0 }, sm);
        auto tgt = makePoints({ 1.0, 4.0, 6.0, 11.0 }, tm);
        const TransportPlan plan = buildOptimalTransportPlan(src, tgt);

        std::map<int, double> outMass, inMass;
        double total = 0.0;
        for (const auto& c : plan.couplings)
        {
            outMass[c.sourceIndex] += c.mass;
            inMass[c.targetIndex] += c.mass;
            total += c.mass;
        }
        if (!approxEqual(total, 1.0, 1e-9))
            return fail("conservation: total transported mass != 1");
        const double sumS = sm[0] + sm[1] + sm[2];
        for (size_t i = 0; i < sm.size(); ++i)
            if (!approxEqual(outMass[static_cast<int>(i)], sm[i] / sumS, 1e-9))
                return fail("conservation: source out-mass mismatch");
        const double sumT = tm[0] + tm[1] + tm[2] + tm[3];
        for (size_t j = 0; j < tm.size(); ++j)
            if (!approxEqual(inMass[static_cast<int>(j)], tm[j] / sumT, 1e-9))
                return fail("conservation: target in-mass mismatch");
    }

    // 3. Monotone continuity (pop-free). The optimal 1D plan is monotone: the
    //    couplings are simultaneously non-decreasing in source and target
    //    position, so no two interpolated tracks cross at any morph amount t,
    //    and each interpolant moves continuously/monotonically in t.
    {
        auto src = makePoints({ 1.0, 4.0, 8.0 }, { 0.3, 0.3, 0.4 });
        auto tgt = makePoints({ 2.0, 3.0, 9.0, 13.0 }, { 0.2, 0.3, 0.2, 0.3 });
        const TransportPlan plan = buildOptimalTransportPlan(src, tgt);

        for (size_t k = 1; k < plan.couplings.size(); ++k)
        {
            if (plan.couplings[k].sourcePosition + 1e-12 < plan.couplings[k - 1].sourcePosition)
                return fail("continuity: couplings not sorted by source position");
            if (plan.couplings[k].targetPosition + 1e-12 < plan.couplings[k - 1].targetPosition)
                return fail("continuity: plan not monotone (tracks would cross)");
        }

        // No crossings across a full morph sweep: for every pair of couplings the
        // interpolated-position ordering is preserved for all t in [0,1].
        auto interp = [](const TransportCoupling& c, double t) {
            return (1.0 - t) * c.sourcePosition + t * c.targetPosition;
        };
        for (size_t p = 0; p + 1 < plan.couplings.size(); ++p)
        {
            for (double t = 0.0; t <= 1.0 + 1e-9; t += 0.05)
            {
                if (interp(plan.couplings[p], t) > interp(plan.couplings[p + 1], t) + 1e-9)
                    return fail("continuity: interpolated tracks cross during sweep");
            }
        }
    }

    // 4. Count mismatch -> mass splitting, and optimality vs greedy. A 2-point
    //    source feeding a 3-point target must split at least one source's mass
    //    across multiple targets, and the OT cost must not exceed greedy nearest.
    {
        auto src = makePoints({ 2.0, 8.0 }, { 0.5, 0.5 });
        auto tgt = makePoints({ 1.0, 5.0, 9.0 }, { 0.34, 0.33, 0.33 });
        const TransportPlan plan = buildOptimalTransportPlan(src, tgt);

        std::map<int, int> fanout;
        for (const auto& c : plan.couplings)
            ++fanout[c.sourceIndex];
        bool anySplit = false;
        for (const auto& kv : fanout)
            if (kv.second > 1) anySplit = true;
        if (!anySplit)
            return fail("mismatch: expected at least one source mass to split");

        const double greedy = greedyNearestCost(src, tgt);
        if (plan.transportCost > greedy + 1e-9)
            return fail("mismatch: OT cost exceeds greedy nearest cost");
    }

    // 5. Determinism. Shuffling the input order must not change the plan.
    {
        auto src = makePoints({ 1.0, 4.0, 8.0, 11.0 }, { 0.2, 0.4, 0.1, 0.3 });
        auto tgt = makePoints({ 2.0, 3.0, 9.0 }, { 0.5, 0.2, 0.3 });
        const TransportPlan ref = buildOptimalTransportPlan(src, tgt);

        std::mt19937 rng(12345);
        auto shuffledSrc = src;
        auto shuffledTgt = tgt;
        std::shuffle(shuffledSrc.begin(), shuffledSrc.end(), rng);
        std::shuffle(shuffledTgt.begin(), shuffledTgt.end(), rng);
        const TransportPlan again = buildOptimalTransportPlan(shuffledSrc, shuffledTgt);

        if (ref.couplings.size() != again.couplings.size())
            return fail("determinism: coupling count changed under input shuffle");
        for (size_t k = 0; k < ref.couplings.size(); ++k)
        {
            const auto& x = ref.couplings[k];
            const auto& y = again.couplings[k];
            if (x.sourceIndex != y.sourceIndex || x.targetIndex != y.targetIndex
                || !approxEqual(x.mass, y.mass, 1e-12))
                return fail("determinism: plan differs under input shuffle");
        }
        if (!approxEqual(ref.transportCost, again.transportCost, 1e-12))
            return fail("determinism: transport cost differs under input shuffle");
    }

    // 6. Unbalanced totals without normalization: only min(total) mass moves,
    //    the surplus on the heavier side is left untransported.
    {
        auto src = makePoints({ 1.0, 5.0 }, { 0.6, 0.6 });   // total 1.2
        auto tgt = makePoints({ 2.0, 6.0 }, { 0.4, 0.4 });   // total 0.8
        TransportOptions opt;
        opt.normalizeMass = false;
        const TransportPlan plan = buildOptimalTransportPlan(src, tgt, opt);
        if (!approxEqual(plan.transportedMass, 0.8, 1e-9))
            return fail("unbalanced: expected transported mass == min(total)");
        if (!approxEqual(plan.sourceMass, 1.2, 1e-9) || !approxEqual(plan.targetMass, 0.8, 1e-9))
            return fail("unbalanced: raw side totals not reported");
    }

    std::cout << "morph_transport_test: OK\n";
    return 0;
}

#include "AtsOnsetAlignedWarp.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ats
{
namespace
{

constexpr double kTimeEpsilon = 1.0e-9;
// Two anchors per paired onset (onset + attack hold) plus the two endpoints must
// fit AutomationCurve::kMaxPoints, which normalise() would otherwise truncate.
constexpr std::size_t kMaxPairs = (AutomationCurve::kMaxPoints - 2) / 2;

std::vector<double> cleanOnsets(const std::vector<double>& onsets, double duration)
{
    std::vector<double> out;
    out.reserve(onsets.size());
    for (const auto t : onsets)
        if (std::isfinite(t) && t >= 0.0 && t <= duration)
            out.push_back(t);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end(),
                          [](double a, double b) { return std::abs(a - b) <= kTimeEpsilon; }),
              out.end());
    return out;
}

struct OnsetPair
{
    double timeA;
    double timeB;
};

std::vector<OnsetPair> pairOnsets(const std::vector<double>& a, double durationA,
                                  const std::vector<double>& b, double durationB)
{
    std::vector<OnsetPair> pairs;
    if (a.empty() || b.empty())
        return pairs;

    if (a.size() == b.size())
    {
        for (std::size_t k = 0; k < a.size(); ++k)
            pairs.push_back({ a[k], b[k] });
        return pairs;
    }

    // Counts differ: greedy nearest by normalised time, non-conflicting, then
    // crossing pairs dropped so both warps remain monotonic.
    struct Candidate
    {
        double cost;
        std::size_t i;
        std::size_t j;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(a.size() * b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            candidates.push_back({ std::abs(a[i] / durationA - b[j] / durationB), i, j });
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) {
        if (x.cost != y.cost)
            return x.cost < y.cost;
        if (x.i != y.i)
            return x.i < y.i;
        return x.j < y.j;
    });
    std::vector<bool> usedA(a.size(), false), usedB(b.size(), false);
    for (const auto& c : candidates)
    {
        if (usedA[c.i] || usedB[c.j])
            continue;
        usedA[c.i] = true;
        usedB[c.j] = true;
        pairs.push_back({ a[c.i], b[c.j] });
    }
    std::sort(pairs.begin(), pairs.end(), [](const OnsetPair& x, const OnsetPair& y) { return x.timeA < y.timeA; });
    std::vector<OnsetPair> monotonic;
    for (const auto& p : pairs)
        if (monotonic.empty() || p.timeB > monotonic.back().timeB + kTimeEpsilon)
            monotonic.push_back(p);
    return monotonic;
}

struct Anchor
{
    double x;           // output time, seconds
    double y;           // source time, seconds
    int holdOf = -1;    // index of the onset anchor this hold anchor trails, or -1
    double hold = 0.0;  // hold length in seconds (hold anchors only)
};

// Build one source's warp: anchors at (t_k, tS_k) with attack-hold anchors, slope
// clamp by minimal source-time moves, then normalised float points.
AutomationCurve buildCurve(const std::vector<double>& targetSeconds,
                           const std::vector<double>& sourceSeconds,
                           double outputDuration,
                           double sourceDuration,
                           double attackHold,
                           double maxRatio)
{
    AutomationCurve curve;
    std::vector<Anchor> anchors;
    anchors.push_back({ 0.0, 0.0, -1, 0.0 });
    for (std::size_t k = 0; k < targetSeconds.size(); ++k)
    {
        const double x = targetSeconds[k];
        const double y = sourceSeconds[k];
        // Skip anchors that would coincide with an endpoint or break strict x order.
        if (x <= anchors.back().x + kTimeEpsilon || x >= outputDuration - kTimeEpsilon)
            continue;
        if (y < anchors.back().y || y >= sourceDuration - kTimeEpsilon)
            continue;
        anchors.push_back({ x, y, -1, 0.0 });
    }
    anchors.push_back({ outputDuration, sourceDuration, -1, 0.0 });

    // Attack hold: a second anchor `hold` seconds after each onset at slope 1,
    // shortened so it stays clear of the next anchor in both domains.
    if (attackHold > kTimeEpsilon)
    {
        std::vector<Anchor> withHolds;
        withHolds.reserve(anchors.size() * 2);
        for (std::size_t i = 0; i < anchors.size(); ++i)
        {
            withHolds.push_back(anchors[i]);
            if (i == 0 || i + 1 >= anchors.size())
                continue;
            const auto& next = anchors[i + 1];
            const double hold = std::min({ attackHold,
                                           0.5 * (next.x - anchors[i].x),
                                           0.5 * (next.y - anchors[i].y) });
            if (hold > kTimeEpsilon)
                withHolds.push_back({ anchors[i].x + hold, anchors[i].y + hold,
                                      static_cast<int>(withHolds.size()) - 1, hold });
        }
        anchors = std::move(withHolds);
    }

    // Slope clamp: alternate forward/backward projection of source times onto
    // [dx / R, dx * R] with the endpoints fixed; hold anchors re-synced to their
    // onset after each pass so the protected attack keeps slope 1.
    const std::size_t n = anchors.size();
    if (n > 2 && maxRatio > 1.0)
    {
        const auto resync = [&anchors]() {
            for (auto& anchor : anchors)
                if (anchor.holdOf >= 0)
                    anchor.y = anchors[static_cast<std::size_t>(anchor.holdOf)].y + anchor.hold;
        };
        for (int pass = 0; pass < 32; ++pass)
        {
            double moved = 0.0;
            for (std::size_t i = 1; i + 1 < n; ++i)
            {
                const double dx = anchors[i].x - anchors[i - 1].x;
                const double lo = anchors[i - 1].y + dx / maxRatio;
                const double hi = anchors[i - 1].y + dx * maxRatio;
                const double y = std::clamp(anchors[i].y, lo, hi);
                moved += std::abs(y - anchors[i].y);
                anchors[i].y = y;
            }
            resync();
            for (std::size_t i = n - 2; i >= 1; --i)
            {
                const double dx = anchors[i + 1].x - anchors[i].x;
                const double lo = anchors[i + 1].y - dx * maxRatio;
                const double hi = anchors[i + 1].y - dx / maxRatio;
                const double y = std::clamp(anchors[i].y, lo, hi);
                moved += std::abs(y - anchors[i].y);
                anchors[i].y = y;
            }
            resync();
            if (moved <= 1.0e-12)
                break;
        }
    }

    // Monotonic, in range.
    for (std::size_t i = 1; i < n; ++i)
        anchors[i].y = std::clamp(std::max(anchors[i].y, anchors[i - 1].y), 0.0, sourceDuration);

    bool identity = true;
    for (const auto& anchor : anchors)
    {
        const double x = anchor.x / outputDuration;
        const double y = anchor.y / sourceDuration;
        if (std::abs(x - y) > kTimeEpsilon)
            identity = false;
        curve.points.push_back({ static_cast<float>(x), static_cast<float>(y) });
    }
    if (identity)
        curve.points.clear();
    return curve;
}

} // namespace

OnsetAlignedWarp buildOnsetAlignedWarp(const std::vector<double>& onsetsASeconds,
                                       const std::vector<double>& onsetsBSeconds,
                                       const OnsetAlignedWarpParams& params)
{
    OnsetAlignedWarp result;
    const double durationA = params.durationA;
    const double durationB = params.durationB;
    if (!std::isfinite(durationA) || !std::isfinite(durationB) || durationA <= 0.0 || durationB <= 0.0)
        return result;

    const double alpha = std::isfinite(params.morphAmount) ? std::clamp(params.morphAmount, 0.0, 1.0) : 0.0;
    const double outputDuration = alpha * (durationB - durationA) + durationA;
    result.outputDuration = outputDuration;
    if (!(outputDuration > 0.0))
        return result;

    const double attackHold = (std::isfinite(params.attackHoldSeconds) && params.attackHoldSeconds > 0.0)
                                  ? params.attackHoldSeconds
                                  : 0.0;
    const double maxRatio = (std::isfinite(params.maxWarpRatio) && params.maxWarpRatio > 1.0)
                                ? params.maxWarpRatio
                                : 1.0;

    auto pairs = pairOnsets(cleanOnsets(onsetsASeconds, durationA), durationA,
                            cleanOnsets(onsetsBSeconds, durationB), durationB);
    if (pairs.size() > kMaxPairs)
        pairs.resize(kMaxPairs);
    if (pairs.empty())
        return result;

    std::vector<double> timesA, timesB;
    timesA.reserve(pairs.size());
    timesB.reserve(pairs.size());
    result.targetOnsetSeconds.reserve(pairs.size());
    for (const auto& pair : pairs)
    {
        timesA.push_back(pair.timeA);
        timesB.push_back(pair.timeB);
        result.targetOnsetSeconds.push_back(alpha * (pair.timeB - pair.timeA) + pair.timeA); // Hatch §4.2
    }
    result.pairedOnsets = static_cast<int>(pairs.size());

    result.warpA = buildCurve(result.targetOnsetSeconds, timesA, outputDuration, durationA, attackHold, maxRatio);
    result.warpB = buildCurve(result.targetOnsetSeconds, timesB, outputDuration, durationB, attackHold, maxRatio);
    return result;
}

} // namespace ats

#include "AtsMorphTransport.h"

#include <algorithm>
#include <cmath>

namespace ats
{

namespace
{

// Stable order on the perceptual axis: by position, then by caller index so the
// plan is deterministic regardless of input order and tie-breaks reproducibly.
bool transportPointLess(const TransportPoint& a, const TransportPoint& b)
{
    if (a.position != b.position)
        return a.position < b.position;
    return a.index < b.index;
}

std::vector<TransportPoint> collectAndNormalize(const std::vector<TransportPoint>& in,
                                                bool normalize, double& rawTotal)
{
    std::vector<TransportPoint> pts;
    pts.reserve(in.size());
    rawTotal = 0.0;
    for (const auto& p : in)
    {
        if (std::isfinite(p.position) && std::isfinite(p.mass) && p.mass > 0.0)
        {
            pts.push_back(p);
            rawTotal += p.mass;
        }
    }
    std::sort(pts.begin(), pts.end(), transportPointLess);

    if (normalize && rawTotal > 0.0)
    {
        const double inv = 1.0 / rawTotal;
        for (auto& p : pts)
            p.mass *= inv;
    }
    return pts;
}

} // namespace

TransportPlan buildOptimalTransportPlan(const std::vector<TransportPoint>& source,
                                        const std::vector<TransportPoint>& target,
                                        const TransportOptions& options)
{
    TransportPlan plan;

    double rawSource = 0.0;
    double rawTarget = 0.0;
    std::vector<TransportPoint> src =
        collectAndNormalize(source, options.normalizeMass, rawSource);
    std::vector<TransportPoint> tgt =
        collectAndNormalize(target, options.normalizeMass, rawTarget);

    plan.sourceMass = rawSource;
    plan.targetMass = rawTarget;

    if (src.empty() || tgt.empty())
        return plan;

    const double eps =
        (std::isfinite(options.massEpsilon) && options.massEpsilon > 0.0)
            ? options.massEpsilon
            : 0.0;

    // North-west-corner sweep of the two sorted distributions. Each step routes
    // as much mass as the smaller of the two current residuals, which is the
    // exact optimal coupling for any convex ground cost on the line.
    size_t i = 0;
    size_t j = 0;
    double sResidual = src[0].mass;
    double tResidual = tgt[0].mass;

    plan.couplings.reserve(src.size() + tgt.size());

    while (i < src.size() && j < tgt.size())
    {
        const double w = std::min(sResidual, tResidual);
        if (w > 0.0)
        {
            TransportCoupling c;
            c.sourceIndex = src[i].index;
            c.targetIndex = tgt[j].index;
            c.mass = w;
            c.sourcePosition = src[i].position;
            c.targetPosition = tgt[j].position;
            plan.couplings.push_back(c);

            plan.transportedMass += w;
            plan.transportCost += w * std::fabs(src[i].position - tgt[j].position);
        }

        sResidual -= w;
        tResidual -= w;

        if (sResidual <= eps)
        {
            if (++i < src.size())
                sResidual = src[i].mass;
        }
        if (tResidual <= eps)
        {
            if (++j < tgt.size())
                tResidual = tgt[j].mass;
        }
    }

    return plan;
}

} // namespace ats

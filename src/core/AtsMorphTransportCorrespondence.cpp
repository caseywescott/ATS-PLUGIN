#include "AtsMorphTransportCorrespondence.h"

#include "AtsMorphTransport.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ats
{

namespace
{

// Local copy of the correspondence-candidate predicate so this adapter does not
// link AtsMorphRecipe.cpp (which carries unrelated work). Mirrors
// isCorrespondenceCandidate + the freq>0 guard the greedy path uses, since the
// log-frequency axis needs a positive frequency.
bool isTransportCandidate(const MorphBandAssignment& a)
{
    return a.kind == MorphBandAssignmentKind::Assigned && a.bandIndex >= 0
           && std::isfinite(a.frequencyHz) && std::isfinite(a.amplitude)
           && a.frequencyHz > 0.0 && a.amplitude > 0.0;
}

double energyOf(const MorphBandAssignment& a) { return a.amplitude * a.amplitude; }

// Canonical within-band order so the result is independent of input order:
// ascending frequency, then partial/frame/amplitude as stable tie-breaks.
bool candidateLess(const MorphBandAssignment& a, const MorphBandAssignment& b)
{
    if (a.frequencyHz != b.frequencyHz) return a.frequencyHz < b.frequencyHz;
    if (a.partialIndex != b.partialIndex) return a.partialIndex < b.partialIndex;
    if (a.frameIndex != b.frameIndex) return a.frameIndex < b.frameIndex;
    return a.amplitude < b.amplitude;
}

void fillSourceFields(MorphTransportEdge& e, const MorphBandAssignment& a)
{
    e.sourcePartialIndex = a.partialIndex;
    e.sourceFrameIndex = a.frameIndex;
    e.sourceFrequencyHz = a.frequencyHz;
    e.sourceAmplitude = a.amplitude;
}

void fillTargetFields(MorphTransportEdge& e, const MorphBandAssignment& a)
{
    e.targetPartialIndex = a.partialIndex;
    e.targetFrameIndex = a.frameIndex;
    e.targetFrequencyHz = a.frequencyHz;
    e.targetAmplitude = a.amplitude;
}

} // namespace

MorphTransportCorrespondence buildOptimalTransportCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphTransportCorrespondenceOptions& options)
{
    MorphTransportCorrespondence result;

    // Group candidates by band, preserving deterministic insertion order within
    // each band (std::map keys are sorted; the per-band vectors keep input order).
    std::map<int, std::vector<MorphBandAssignment>> sourceByBand;
    std::map<int, std::vector<MorphBandAssignment>> targetByBand;
    for (const auto& a : sourceAssignments)
        if (isTransportCandidate(a))
        {
            sourceByBand[a.bandIndex].push_back(a);
            result.sourceMass += energyOf(a);
        }
    for (const auto& a : targetAssignments)
        if (isTransportCandidate(a))
        {
            targetByBand[a.bandIndex].push_back(a);
            result.targetMass += energyOf(a);
        }

    // Union of band indices, ascending.
    std::vector<int> bands;
    for (const auto& kv : sourceByBand)
        bands.push_back(kv.first);
    for (const auto& kv : targetByBand)
        if (sourceByBand.find(kv.first) == sourceByBand.end())
            bands.push_back(kv.first);
    std::sort(bands.begin(), bands.end());

    const double minFraction =
        (std::isfinite(options.minMassFraction) && options.minMassFraction > 0.0)
            ? options.minMassFraction
            : 0.0;

    for (int band : bands)
    {
        const auto srcIt = sourceByBand.find(band);
        const auto tgtIt = targetByBand.find(band);
        std::vector<MorphBandAssignment> src =
            srcIt != sourceByBand.end() ? srcIt->second : std::vector<MorphBandAssignment>{};
        std::vector<MorphBandAssignment> tgt =
            tgtIt != targetByBand.end() ? tgtIt->second : std::vector<MorphBandAssignment>{};
        std::sort(src.begin(), src.end(), candidateLess);
        std::sort(tgt.begin(), tgt.end(), candidateLess);

        double bandEnergy = 0.0;
        for (const auto& a : src) bandEnergy += energyOf(a);
        for (const auto& a : tgt) bandEnergy += energyOf(a);
        const double minMass = minFraction * bandEnergy;

        // Build transport points on the log-frequency axis with energy mass.
        std::vector<TransportPoint> srcPts;
        std::vector<TransportPoint> tgtPts;
        srcPts.reserve(src.size());
        tgtPts.reserve(tgt.size());
        for (size_t i = 0; i < src.size(); ++i)
            srcPts.push_back({ std::log(src[i].frequencyHz), energyOf(src[i]), static_cast<int>(i) });
        for (size_t j = 0; j < tgt.size(); ++j)
            tgtPts.push_back({ std::log(tgt[j].frequencyHz), energyOf(tgt[j]), static_cast<int>(j) });

        // Unbalanced (no normalization): only min(src,tgt) energy is transported;
        // the surplus on the heavier side is emitted below as fade-in/out so the
        // band's total energy is fully accounted for.
        TransportOptions opt;
        opt.normalizeMass = false;
        const TransportPlan plan = buildOptimalTransportPlan(srcPts, tgtPts, opt);

        std::vector<double> srcMatched(src.size(), 0.0);
        std::vector<double> tgtMatched(tgt.size(), 0.0);

        for (const auto& c : plan.couplings)
        {
            srcMatched[static_cast<size_t>(c.sourceIndex)] += c.mass;
            tgtMatched[static_cast<size_t>(c.targetIndex)] += c.mass;
            if (c.mass < minMass)
                continue;
            MorphTransportEdge e;
            e.bandIndex = band;
            e.kind = MorphCorrespondenceKind::Matched;
            fillSourceFields(e, src[static_cast<size_t>(c.sourceIndex)]);
            fillTargetFields(e, tgt[static_cast<size_t>(c.targetIndex)]);
            e.mass = c.mass;
            result.edges.push_back(e);
            result.transportedMass += c.mass;
            result.transportCost += c.mass * std::fabs(c.sourcePosition - c.targetPosition);
        }

        // Leftover source energy with no counterpart -> fade-out.
        for (size_t i = 0; i < src.size(); ++i)
        {
            const double leftover = energyOf(src[i]) - srcMatched[i];
            if (leftover < minMass)
                continue;
            MorphTransportEdge e;
            e.bandIndex = band;
            e.kind = MorphCorrespondenceKind::UnmatchedSource;
            fillSourceFields(e, src[i]);
            e.mass = leftover;
            result.edges.push_back(e);
        }

        // Leftover target energy with no counterpart -> fade-in.
        for (size_t j = 0; j < tgt.size(); ++j)
        {
            const double leftover = energyOf(tgt[j]) - tgtMatched[j];
            if (leftover < minMass)
                continue;
            MorphTransportEdge e;
            e.bandIndex = band;
            e.kind = MorphCorrespondenceKind::UnmatchedTarget;
            fillTargetFields(e, tgt[j]);
            e.mass = leftover;
            result.edges.push_back(e);
        }
    }

    return result;
}

std::vector<MorphCorrespondenceRecord> buildOptimalTransportRecords(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphTransportCorrespondenceOptions& options)
{
    const MorphTransportCorrespondence corr =
        buildOptimalTransportCorrespondence(sourceAssignments, targetAssignments, options);

    std::vector<MorphCorrespondenceRecord> records;
    records.reserve(corr.edges.size());
    for (const auto& e : corr.edges)
    {
        // Per-edge amplitude is sqrt of the transported energy so that a partial
        // split across several edges conserves energy (sum of amplitude^2 == mass
        // sum). Clamp the radicand to avoid a negative from rounding.
        const double edgeAmp = std::sqrt(std::max(0.0, e.mass));

        MorphCorrespondenceRecord r;
        r.bandIndex = e.bandIndex;
        r.kind = e.kind;
        if (e.kind == MorphCorrespondenceKind::Matched
            || e.kind == MorphCorrespondenceKind::UnmatchedSource)
        {
            r.sourcePartialIndex = e.sourcePartialIndex;
            r.sourceFrameIndex = e.sourceFrameIndex;
            r.sourceFrequencyHz = e.sourceFrequencyHz;
            r.sourceAmplitude = edgeAmp;
        }
        if (e.kind == MorphCorrespondenceKind::Matched
            || e.kind == MorphCorrespondenceKind::UnmatchedTarget)
        {
            r.targetPartialIndex = e.targetPartialIndex;
            r.targetFrameIndex = e.targetFrameIndex;
            r.targetFrequencyHz = e.targetFrequencyHz;
            r.targetAmplitude = edgeAmp;
        }
        records.push_back(r);
    }
    return records;
}

} // namespace ats

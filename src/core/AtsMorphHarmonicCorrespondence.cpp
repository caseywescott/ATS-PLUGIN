#include "AtsMorphHarmonicCorrespondence.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ats
{
namespace
{

constexpr double kMinimumFundamentalHz = 20.0;

bool isHarmonicCandidate(const MorphBandAssignment& assignment)
{
    return assignment.kind == MorphBandAssignmentKind::Assigned && assignment.bandIndex >= 0
           && std::isfinite(assignment.frequencyHz) && std::isfinite(assignment.amplitude)
           && assignment.frequencyHz > 0.0;
}

bool candidateLess(const MorphBandAssignment& a, const MorphBandAssignment& b)
{
    if (a.frequencyHz != b.frequencyHz)
        return a.frequencyHz < b.frequencyHz;
    if (a.partialIndex != b.partialIndex)
        return a.partialIndex < b.partialIndex;
    if (a.frameIndex != b.frameIndex)
        return a.frameIndex < b.frameIndex;
    return a.amplitude < b.amplitude;
}

std::vector<MorphBandAssignment> collectCandidates(const std::vector<MorphBandAssignment>& assignments)
{
    std::vector<MorphBandAssignment> candidates;
    candidates.reserve(assignments.size());
    for (const auto& assignment : assignments)
        if (isHarmonicCandidate(assignment))
            candidates.push_back(assignment);
    std::sort(candidates.begin(), candidates.end(), candidateLess);
    return candidates;
}

// Harmonic sieve over the candidate set, independent of how candidates are
// spread over frames (the host passes one entry per partial at its own peak
// frame, so a "lowest per frame" median lands mid-spectrum). Candidate f0s are
// f / k (k = 1..8) for the strongest partials and the lowest one; a candidate
// scores the amplitude of partials within 10% of a harmonic of it. Every
// subharmonic of the true f0 scores as well as the f0 itself, so the highest
// candidate within 90% of the best score is chosen. The result is refined by
// the amplitude-weighted mean of f / round(f / f0). 0 when no estimate is
// possible (fewer than two candidates).
double estimateFundamental(const std::vector<MorphBandAssignment>& candidates)
{
    if (candidates.size() < 2)
        return 0.0;

    constexpr double kMinF0 = 20.0;
    constexpr double kMaxF0 = 4000.0;
    constexpr double kTolerance = 0.1; // fraction of a harmonic spacing
    constexpr int kMaxDivisor = 8;
    constexpr size_t kSeeds = 8;

    std::vector<const MorphBandAssignment*> byAmplitude;
    byAmplitude.reserve(candidates.size());
    for (const auto& candidate : candidates)
        byAmplitude.push_back(&candidate);
    std::sort(byAmplitude.begin(), byAmplitude.end(),
              [](const MorphBandAssignment* a, const MorphBandAssignment* b) {
                  return a->amplitude != b->amplitude ? a->amplitude > b->amplitude
                                                      : a->frequencyHz < b->frequencyHz;
              });
    std::vector<double> seeds;
    for (size_t i = 0; i < byAmplitude.size() && i < kSeeds; ++i)
        seeds.push_back(byAmplitude[i]->frequencyHz);
    const auto lowest = std::min_element(candidates.begin(), candidates.end(),
                                         [](const MorphBandAssignment& a, const MorphBandAssignment& b) {
                                             return a.frequencyHz < b.frequencyHz;
                                         });
    seeds.push_back(lowest->frequencyHz);

    const auto score = [&](double f0) {
        double total = 0.0;
        for (const auto& candidate : candidates)
        {
            const double ratio = candidate.frequencyHz / f0;
            const double h = std::round(ratio);
            if (h < 1.0)
                continue;
            const double deviation = std::abs(ratio - h);
            if (deviation < kTolerance)
                total += std::max(0.0, candidate.amplitude) * (1.0 - deviation / kTolerance);
        }
        return total;
    };

    std::vector<std::pair<double, double>> scored; // (f0, score)
    for (const double seed : seeds)
        for (int k = 1; k <= kMaxDivisor; ++k)
        {
            const double f0 = seed / static_cast<double>(k);
            if (f0 >= kMinF0 && f0 <= kMaxF0)
                scored.emplace_back(f0, score(f0));
        }
    if (scored.empty())
        return 0.0;
    double best = 0.0;
    for (const auto& entry : scored)
        best = std::max(best, entry.second);
    if (!(best > 0.0))
        return 0.0;
    double initial = 0.0;
    for (const auto& entry : scored)
        if (entry.second >= 0.9 * best)
            initial = std::max(initial, entry.first);
    if (!(initial > 0.0))
        return 0.0;

    double weightedSum = 0.0;
    double weightTotal = 0.0;
    for (const auto& candidate : candidates)
    {
        const double h = std::round(candidate.frequencyHz / initial);
        if (h < 1.0)
            continue;
        const double weight = std::max(0.0, candidate.amplitude);
        weightedSum += weight * (candidate.frequencyHz / h);
        weightTotal += weight;
    }
    return weightTotal > 0.0 ? weightedSum / weightTotal : initial;
}

int harmonicLabel(double frequencyHz, double fundamentalHz)
{
    const double h = std::round(frequencyHz / fundamentalHz);
    return static_cast<int>(std::max(1.0, h));
}

void copySource(MorphCorrespondenceRecord& record, const MorphBandAssignment& assignment)
{
    record.sourcePartialIndex = assignment.partialIndex;
    record.sourceFrameIndex = assignment.frameIndex;
    record.sourceFrequencyHz = assignment.frequencyHz;
    record.sourceAmplitude = assignment.amplitude;
}

void copyTarget(MorphCorrespondenceRecord& record, const MorphBandAssignment& assignment)
{
    record.targetPartialIndex = assignment.partialIndex;
    record.targetFrameIndex = assignment.frameIndex;
    record.targetFrequencyHz = assignment.frequencyHz;
    record.targetAmplitude = assignment.amplitude;
}

struct LabelBucket
{
    std::vector<size_t> source;
    std::vector<size_t> target;
};

} // namespace

HarmonicCorrespondenceResult buildHarmonicLockedCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const HarmonicCorrespondenceOptions& options)
{
    HarmonicCorrespondenceResult result;
    const auto sourceCandidates = collectCandidates(sourceAssignments);
    const auto targetCandidates = collectCandidates(targetAssignments);

    const double threshold = (std::isfinite(options.ratioThreshold) && options.ratioThreshold > 1.0)
                                 ? options.ratioThreshold
                                 : 1.0;
    const double maxLogCost = std::log(threshold);

    // Fundamentals: explicit hint, else estimate; grid fallback when either is unreliable.
    const auto resolveFundamental = [](double hint, const std::vector<MorphBandAssignment>& candidates) {
        if (std::isfinite(hint) && hint > 0.0)
            return hint;
        return estimateFundamental(candidates);
    };
    double f0Source = resolveFundamental(options.fundamentalSourceHz, sourceCandidates);
    double f0Target = resolveFundamental(options.fundamentalTargetHz, targetCandidates);
    const bool sourceReliable = std::isfinite(f0Source) && f0Source >= kMinimumFundamentalHz;
    const bool targetReliable = std::isfinite(f0Target) && f0Target >= kMinimumFundamentalHz;
    if (!sourceReliable || !targetReliable)
    {
        const double grid = (std::isfinite(options.gridHz) && options.gridHz > 0.0) ? options.gridHz : 100.0;
        f0Source = grid;
        f0Target = grid;
        result.usedGrid = true;
    }
    result.fundamentalSourceHz = f0Source;
    result.fundamentalTargetHz = f0Target;

    // Labelling fundamentals and the pitch-normalisation factor applied to target
    // frequencies before comparing against the source. With octave wrap and an
    // octave or more between the two f0s, both sides are labelled on the lower
    // fundamental and compared at absolute frequency (Hatch: h of A vs h*2^k of B).
    double labelF0Source = f0Source;
    double labelF0Target = f0Target;
    double normalise = f0Source / f0Target; // f_B * f0_A / f0_B
    if (options.octaveWrap && !result.usedGrid)
    {
        // "An octave or more apart": the f0 ratio itself must reach 2 (a fifth,
        // ratio 1.5, must not round up to an octave).
        const double octaves = std::abs(std::log2(f0Target / f0Source));
        if (octaves >= 1.0 - 1.0e-6)
        {
            const double common = std::min(f0Source, f0Target);
            labelF0Source = common;
            labelF0Target = common;
            normalise = 1.0;
        }
    }

    std::map<int, LabelBucket> buckets;
    for (size_t i = 0; i < sourceCandidates.size(); ++i)
        buckets[harmonicLabel(sourceCandidates[i].frequencyHz, labelF0Source)].source.push_back(i);
    for (size_t j = 0; j < targetCandidates.size(); ++j)
        buckets[harmonicLabel(targetCandidates[j].frequencyHz, labelF0Target)].target.push_back(j);

    struct Pair
    {
        double cost;
        size_t sourceOffset;
        size_t targetOffset;
    };

    for (const auto& [label, bucket] : buckets)
    {
        std::vector<Pair> pairs;
        pairs.reserve(bucket.source.size() * bucket.target.size());
        for (size_t s = 0; s < bucket.source.size(); ++s)
        {
            const double fa = sourceCandidates[bucket.source[s]].frequencyHz;
            for (size_t t = 0; t < bucket.target.size(); ++t)
            {
                const double fb = targetCandidates[bucket.target[t]].frequencyHz * normalise;
                const double cost = std::abs(std::log(fb / fa));
                if (std::isfinite(cost) && cost <= maxLogCost)
                    pairs.push_back({cost, s, t});
            }
        }
        std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
            if (a.cost != b.cost)
                return a.cost < b.cost;
            if (a.sourceOffset != b.sourceOffset)
                return a.sourceOffset < b.sourceOffset;
            return a.targetOffset < b.targetOffset;
        });

        std::vector<bool> sourceUsed(bucket.source.size(), false);
        std::vector<bool> targetUsed(bucket.target.size(), false);
        std::vector<MorphCorrespondenceRecord> matched;
        for (const auto& pair : pairs)
        {
            if (sourceUsed[pair.sourceOffset] || targetUsed[pair.targetOffset])
                continue;
            sourceUsed[pair.sourceOffset] = true;
            targetUsed[pair.targetOffset] = true;

            const auto& source = sourceCandidates[bucket.source[pair.sourceOffset]];
            const auto& target = targetCandidates[bucket.target[pair.targetOffset]];
            MorphCorrespondenceRecord record;
            record.bandIndex = source.bandIndex;
            record.rank = label;
            record.kind = MorphCorrespondenceKind::Matched;
            copySource(record, source);
            copyTarget(record, target);
            record.hasCostDiagnostics = true;
            record.logFrequencyCost = pair.cost;
            record.totalCost = pair.cost;
            matched.push_back(record);
        }
        std::stable_sort(matched.begin(), matched.end(),
                         [](const MorphCorrespondenceRecord& a, const MorphCorrespondenceRecord& b) {
                             return a.sourceFrequencyHz < b.sourceFrequencyHz;
                         });
        result.matched += static_cast<int>(matched.size());
        result.records.insert(result.records.end(), matched.begin(), matched.end());

        for (size_t s = 0; s < bucket.source.size(); ++s)
        {
            if (sourceUsed[s])
                continue;
            const auto& source = sourceCandidates[bucket.source[s]];
            MorphCorrespondenceRecord record;
            record.bandIndex = source.bandIndex;
            record.rank = label;
            record.kind = MorphCorrespondenceKind::UnmatchedSource;
            copySource(record, source);
            result.records.push_back(record);
            ++result.unmatchedSource;
        }
        for (size_t t = 0; t < bucket.target.size(); ++t)
        {
            if (targetUsed[t])
                continue;
            const auto& target = targetCandidates[bucket.target[t]];
            MorphCorrespondenceRecord record;
            record.bandIndex = target.bandIndex;
            record.rank = label;
            record.kind = MorphCorrespondenceKind::UnmatchedTarget;
            copyTarget(record, target);
            result.records.push_back(record);
            ++result.unmatchedTarget;
        }
    }

    return result;
}

} // namespace ats

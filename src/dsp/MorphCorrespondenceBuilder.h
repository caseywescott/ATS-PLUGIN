#pragma once

#include "../core/AtsMorphHarmonicCorrespondence.h"
#include "../core/AtsMorphTransportCorrespondence.h"

namespace ats::dsp
{

class MorphCorrespondenceBuilder
{
public:
    [[nodiscard]] static std::vector<ats::MorphCorrespondenceRecord> rank(
        const std::vector<ats::MorphBandAssignment>& source,
        const std::vector<ats::MorphBandAssignment>& target, double amplitudeRankWeight = 0.0)
    { return ats::buildFrequencyRankCorrespondence(source, target, amplitudeRankWeight); }

    [[nodiscard]] static std::vector<ats::MorphCorrespondenceRecord> greedy(
        const std::vector<ats::MorphBandAssignment>& source,
        const std::vector<ats::MorphBandAssignment>& target,
        const ats::MorphGreedyNearestOptions& options = {})
    { return ats::buildGreedyNearestCorrespondence(source, target, options); }

    [[nodiscard]] static ats::MorphAssignmentMatrixResult assignment(
        const std::vector<ats::MorphBandAssignment>& source,
        const std::vector<ats::MorphBandAssignment>& target,
        const ats::MorphAssignmentMatrixOptions& options = {})
    { return ats::buildAssignmentMatrixCorrespondence(source, target, options); }

    [[nodiscard]] static ats::MorphTransportCorrespondence transport(
        const std::vector<ats::MorphBandAssignment>& source,
        const std::vector<ats::MorphBandAssignment>& target,
        const ats::MorphTransportCorrespondenceOptions& options = {})
    { return ats::buildOptimalTransportCorrespondence(source, target, options); }

    // Hatch (2004) §4.4 harmonic alignment; see AtsMorphHarmonicCorrespondence.h.
    [[nodiscard]] static ats::HarmonicCorrespondenceResult harmonic(
        const std::vector<ats::MorphBandAssignment>& source,
        const std::vector<ats::MorphBandAssignment>& target,
        const ats::HarmonicCorrespondenceOptions& options = {})
    { return ats::buildHarmonicLockedCorrespondence(source, target, options); }

    // Dispatch on the recipe-level strategy enum. AssignmentMatrix runs with the
    // advanced strategy enabled (otherwise it degrades to greedy); HarmonicLocked
    // uses the supplied harmonic options (default: estimated fundamentals).
    [[nodiscard]] static std::vector<ats::MorphCorrespondenceRecord> build(
        ats::MorphCorrespondenceStrategy strategy,
        const std::vector<ats::MorphBandAssignment>& source,
        const std::vector<ats::MorphBandAssignment>& target,
        double amplitudeRankWeight = 0.0,
        const ats::HarmonicCorrespondenceOptions& harmonicOptions = {})
    {
        switch (strategy)
        {
        case ats::MorphCorrespondenceStrategy::GreedyNearest:
        {
            ats::MorphGreedyNearestOptions options;
            options.amplitudeRankWeight = amplitudeRankWeight;
            return greedy(source, target, options);
        }
        case ats::MorphCorrespondenceStrategy::AssignmentMatrix:
        {
            ats::MorphAssignmentMatrixOptions options;
            options.advancedStrategyEnabled = true;
            options.amplitudeRankWeight = amplitudeRankWeight;
            return assignment(source, target, options).records;
        }
        case ats::MorphCorrespondenceStrategy::HarmonicLocked:
            return harmonic(source, target, harmonicOptions).records;
        case ats::MorphCorrespondenceStrategy::FrequencyRank:
        default:
            return rank(source, target, amplitudeRankWeight);
        }
    }
};

} // namespace ats::dsp

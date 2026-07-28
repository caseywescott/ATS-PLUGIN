#pragma once

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
};

} // namespace ats::dsp

#pragma once

#include "../core/AtsMorphRecipe.h"

namespace ats::dsp
{

class MorphTransientService
{
public:
    [[nodiscard]] static ats::MorphTransientAnalysisReport analyze(
        const std::vector<ats::MorphTransientMarker>& source,
        const std::vector<ats::MorphTransientMarker>& target,
        const ats::MorphTransientSettings& settings = {})
    { return ats::analyzeMorphTransientAlignment(source, target, settings); }
};

} // namespace ats::dsp

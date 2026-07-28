#pragma once

#include "../core/AtsMorphRecipe.h"

namespace ats::dsp
{

class MorphResidualService
{
public:
    [[nodiscard]] static ats::MorphResidualNoiseReport evaluate(
        const std::vector<ats::MorphResidualNoiseBand>& source,
        const std::vector<ats::MorphResidualNoiseBand>& target,
        const ats::MorphResidualNoiseOptions& options = {})
    { return ats::morphResidualNoiseBands(source, target, options); }
};

} // namespace ats::dsp

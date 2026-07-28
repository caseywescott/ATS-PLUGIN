#pragma once

#include "../core/AtsMorphRecipe.h"

namespace ats::dsp
{

class MorphPhaseService
{
public:
    [[nodiscard]] static ats::MorphPhaseResolution resolve(const ats::MorphPhaseInput& input,
                                                            double amount,
                                                            const ats::MorphPhaseSettings& settings = {})
    { return ats::resolveMorphPhase(input, amount, settings); }
};

} // namespace ats::dsp

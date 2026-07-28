#pragma once

#include "../core/AtsMorphRecipe.h"

namespace ats::dsp
{

class MorphEvaluator
{
public:
    [[nodiscard]] static ats::MorphRenderSnapshot evaluate(const ats::MorphRecipe& recipe,
                                                           const ats::MorphSnapshotOptions& options = {})
    { return ats::evaluateMorphRecipeSnapshot(recipe, options); }
};

} // namespace ats::dsp

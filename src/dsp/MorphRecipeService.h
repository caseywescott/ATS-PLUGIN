#pragma once

#include "../core/AtsMorphRecipe.h"

namespace ats::dsp
{

class MorphRecipeService
{
public:
    [[nodiscard]] static ats::MorphRecipe defaults() { return ats::makeDefaultMorphRecipe(); }
    [[nodiscard]] static ats::MorphCompatibilityReport validate(const ats::MorphRecipe& recipe)
    { return ats::validateMorphSources(recipe); }
    [[nodiscard]] static std::string serialize(const ats::MorphRecipe& recipe)
    { return ats::serializeMorphRecipe(recipe); }
    [[nodiscard]] static bool deserialize(const std::string& text, ats::MorphRecipe& recipe,
                                          std::string* error = nullptr)
    { return ats::deserializeMorphRecipe(text, recipe, error); }
};

} // namespace ats::dsp

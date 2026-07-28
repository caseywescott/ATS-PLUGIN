#pragma once

#include "../core/AtsMorphRecipe.h"

namespace ats::dsp
{

class MorphGridBuilder
{
public:
    [[nodiscard]] static ats::MorphFrequencyGrid bark(int bands, double lowest, double highest)
    { return ats::makeBarkFrequencyGrid(bands, lowest, highest); }
    [[nodiscard]] static ats::MorphFrequencyGrid mel(int bands, double lowest, double highest)
    { return ats::makeMelFrequencyGrid(bands, lowest, highest); }
    [[nodiscard]] static int band(const ats::MorphFrequencyGrid& grid, double frequencyHz)
    { return ats::findFrequencyBandIndex(grid, frequencyHz); }
};

} // namespace ats::dsp

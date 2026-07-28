#pragma once

#include "../core/AtsFrequencyShape.h"

namespace ats::dsp
{

class FrequencyShapeProcessor
{
public:
    [[nodiscard]] static double pivot(const ATS_SOUND* sound, const ats::FrequencyShapeSettings& settings)
    { return ats::resolveFrequencyShapePivotHz(sound, settings); }

    [[nodiscard]] static double apply(double frequencyHz, int harmonicRank,
                                      const ats::FrequencyShapeSettings& settings, double nyquistHz)
    { return ats::applyFrequencyShape(frequencyHz, harmonicRank, settings, nyquistHz); }

    static void applyToSound(ATS_SOUND* sound, const ats::FrequencyShapeSettings& settings)
    { ats::applyFrequencyShapeToSound(sound, settings); }
};

} // namespace ats::dsp

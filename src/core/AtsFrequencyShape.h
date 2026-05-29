#pragma once

#include <vector>

#include "AtsParams.h"

namespace ats
{

[[nodiscard]] const char* frequencyShapeModeName(FrequencyShapeMode mode);
[[nodiscard]] bool isFrequencyShapeModeEnabled(const FrequencyShapeSettings& settings);
[[nodiscard]] bool isFrequencyShapeActive(const FrequencyShapeSettings& settings);
[[nodiscard]] bool sameFrequencyShapeSettings(const FrequencyShapeSettings& a,
                                              const FrequencyShapeSettings& b,
                                              double tolerance = 1.0e-8);

[[nodiscard]] double resolveFrequencyShapePivotHz(const ATS_SOUND* sound,
                                                  const FrequencyShapeSettings& settings);
[[nodiscard]] FrequencyShapeSettings resolveFrequencyShapeSettingsForSound(
    const ATS_SOUND* sound,
    FrequencyShapeSettings settings);

/** Returns one-based harmonic rank per partial, sorted by mean active frequency. */
[[nodiscard]] std::vector<int> buildFrequencyShapeRanks(const ATS_SOUND* sound);

[[nodiscard]] double applyFrequencyShape(double frequencyHz,
                                         int harmonicRank,
                                         const FrequencyShapeSettings& settings,
                                         double nyquistHz);

/** Warps the mutable copy's partial frequency grid in place; residual bands are untouched. */
void applyFrequencyShapeToSound(ATS_SOUND* sound, const FrequencyShapeSettings& settings);

} // namespace ats

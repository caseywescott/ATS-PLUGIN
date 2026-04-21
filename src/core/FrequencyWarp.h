#pragma once

#include "SpectralEnvelope.h"

namespace ats
{

[[nodiscard]] bool isValidFrequencyAxis(const EnvelopeAxisSpec& axis);
[[nodiscard]] double axisMinFrequencyHz(const EnvelopeAxisSpec& axis);
[[nodiscard]] double axisMaxFrequencyHz(const EnvelopeAxisSpec& axis);
[[nodiscard]] double clampFrequencyToAxis(double frequencyHz, const EnvelopeAxisSpec& axis);

[[nodiscard]] double frequencyToWarped(double frequencyHz, const EnvelopeAxisSpec& axis, bool* ok = nullptr);
[[nodiscard]] double warpedToFrequency(double warpedValue, const EnvelopeAxisSpec& axis, bool* ok = nullptr);

[[nodiscard]] double normalizedPositionForFrequency(double frequencyHz,
                                                    const EnvelopeAxisSpec& axis,
                                                    bool* ok = nullptr);
[[nodiscard]] double frequencyForNormalizedPosition(double position,
                                                    const EnvelopeAxisSpec& axis,
                                                    bool* ok = nullptr);

} // namespace ats

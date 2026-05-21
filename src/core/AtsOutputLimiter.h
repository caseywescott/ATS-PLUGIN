#pragma once

#include <algorithm>
#include <cmath>

namespace ats
{

struct AtsSoftLimitConfig
{
    /** Full-scale ceiling. Set <= 0.0 to disable limiting while keeping finite/denormal safety. */
    double ceiling = 1.0;
    /** Samples at or below this absolute value pass through unchanged. */
    double kneeStart = 0.9;
    double denormalClamp = 1.0e-20;
};

inline float applyAtsSoftLimit(double value, const AtsSoftLimitConfig& config = {})
{
    if (!std::isfinite(value) || std::abs(value) < config.denormalClamp)
        return 0.0f;

    if (!std::isfinite(config.ceiling) || config.ceiling <= 0.0)
        return static_cast<float>(value);

    const double ceiling = std::max(1.0e-9, config.ceiling);
    const double rawKnee = std::isfinite(config.kneeStart) ? config.kneeStart : ceiling;
    const double knee = std::max(0.0, std::min(rawKnee, ceiling));
    const double absValue = std::abs(value);
    if (absValue <= knee)
        return static_cast<float>(value);

    if (knee >= ceiling)
        return static_cast<float>(std::copysign(ceiling, value));

    const double range = ceiling - knee;
    const double over = absValue - knee;
    const double shaped = knee + range * (1.0 - std::exp(-over / range));
    return static_cast<float>(std::copysign(std::min(shaped, ceiling), value));
}

} // namespace ats

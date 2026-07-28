#pragma once

#include "../core/TimbreMotionPlan.h"

namespace ats::dsp
{

class TimbreMotionService
{
public:
    using Plan = ats::TimbreMotionPlan;
    using Compiled = ats::CompiledTimbrePlan;

    [[nodiscard]] static Plan deserialize(const std::string& text)
    { return ats::deserializeTimbreMotionPlan(text); }
    [[nodiscard]] static std::string serialize(const Plan& plan)
    { return ats::serializeTimbreMotionPlan(plan); }
    [[nodiscard]] static Compiled compile(const Plan& plan, double chunkRateHz)
    { return Compiled(plan, chunkRateHz); }
};

} // namespace ats::dsp

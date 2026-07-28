#pragma once

#include "../core/AtsMathExpr.h"

namespace ats::dsp
{

class MathExpressionService
{
public:
    [[nodiscard]] static bool apply(ATS_SOUND* sound, const std::string& script,
                                    std::string& error, const std::function<bool()>& cancel = {})
    { return ats::applyMathExprScript(sound, script, error, cancel); }
};

} // namespace ats::dsp

#pragma once

#include <functional>
#include <string>

extern "C" {
#include "atsa.h"
}

namespace ats
{

/** Apply Qatsh-style partial math (see docs/MATHEXPR.md). Mutates `sound` under caller lock.
 *  If `shouldCancel` returns true, stops early and sets errorMessage to "Cancelled." */
bool applyMathExprScript(ATS_SOUND* sound,
                         const std::string& script,
                         std::string& errorMessage,
                         const std::function<bool()>& shouldCancel = {});

} // namespace ats

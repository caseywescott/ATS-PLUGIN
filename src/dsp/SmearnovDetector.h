#pragma once

#include "../core/AtsTransientSmear.h"

namespace ats::dsp
{

class SmearnovDetector
{
public:
    [[nodiscard]] static std::vector<ats::AtsOnset> detect(
        const ATS_SOUND* sound, const ats::SmearnovDetectParams& params,
        ats::SmearnovDetectionFunctions* functions = nullptr)
    { return ats::detectOnsets(sound, params, functions); }
};

} // namespace ats::dsp

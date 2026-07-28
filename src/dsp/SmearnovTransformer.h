#pragma once

#include "../core/AtsTransientSmear.h"

namespace ats::dsp
{

class SmearnovTransformer
{
public:
    [[nodiscard]] static bool apply(ATS_SOUND* sound, const std::vector<ats::AtsOnset>& onsets,
                                    const ats::SmearnovParams& params)
    { return ats::smearAttacks(sound, onsets, params); }
};

} // namespace ats::dsp

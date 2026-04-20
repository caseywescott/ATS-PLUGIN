#pragma once

#include <vector>

namespace ats
{

/** Mono linear resampler (MVP). */
void resampleLinearMono(const float* input,
                        int inputFrames,
                        double inputSr,
                        std::vector<float>& output,
                        double outputSr);

} // namespace ats

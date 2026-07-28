#pragma once

#include "../core/ResampleLinear.h"

namespace ats::dsp
{

class Resampler
{
public:
    static void renderMono(const float* input, int inputFrames, double inputSampleRate,
                           std::vector<float>& output, double outputSampleRate)
    { ats::resampleLinearMono(input, inputFrames, inputSampleRate, output, outputSampleRate); }
};

} // namespace ats::dsp

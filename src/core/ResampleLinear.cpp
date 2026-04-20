#include "ResampleLinear.h"

#include <cmath>

namespace ats
{

void resampleLinearMono(const float* input,
                        int inputFrames,
                        double inputSr,
                        std::vector<float>& output,
                        double outputSr)
{
    if (inputFrames <= 0 || inputSr <= 0.0 || outputSr <= 0.0)
    {
        output.clear();
        return;
    }

    if (std::abs(inputSr - outputSr) < 1.0)
    {
        output.assign(input, input + inputFrames);
        return;
    }

    const auto outFrames = static_cast<int>(std::ceil(static_cast<double>(inputFrames) * outputSr / inputSr));
    output.resize(static_cast<size_t>(outFrames));
    for (int i = 0; i < outFrames; ++i)
    {
        const double srcPos = static_cast<double>(i) * inputSr / outputSr;
        const int i0 = static_cast<int>(std::floor(srcPos));
        const int i1 = std::min(i0 + 1, inputFrames - 1);
        const double frac = srcPos - static_cast<double>(i0);
        const float s0 = input[std::max(0, std::min(i0, inputFrames - 1))];
        const float s1 = input[std::max(0, std::min(i1, inputFrames - 1))];
        output[static_cast<size_t>(i)] = static_cast<float>(s0 + static_cast<float>(frac) * (s1 - s0));
    }
}

} // namespace ats

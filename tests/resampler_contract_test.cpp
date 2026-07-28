#include "../src/dsp/Resampler.h"

#include <cassert>
#include <cmath>
#include <vector>

int main()
{
    const float ramp[] { 0.0f, 1.0f, 2.0f, 3.0f };
    std::vector<float> output;
    ats::dsp::Resampler::renderMono(ramp, 4, 4.0, output, 8.0);
    assert(output.size() == 8);
    assert(std::abs(output[0] - 0.0f) < 1.0e-6f);
    assert(std::abs(output[1] - 0.5f) < 1.0e-6f);
    assert(std::abs(output[6] - 3.0f) < 1.0e-6f);
    assert(std::abs(output[7] - 3.0f) < 1.0e-6f);

    ats::dsp::Resampler::renderMono(ramp, 4, 48000.0, output, 48000.5);
    assert(output.size() == 4);
    ats::dsp::Resampler::renderMono(nullptr, 0, 48000.0, output, 44100.0);
    assert(output.empty());
    return 0;
}

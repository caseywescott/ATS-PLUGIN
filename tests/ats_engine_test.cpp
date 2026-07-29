#include "AtsEngine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void require(bool condition, const char* expression)
{
    if (!condition)
    {
        std::cerr << "ats_engine_test failed: " << expression << '\n';
        std::abort();
    }
}
}

int main()
{
    constexpr int sampleRate = 44100;
    std::vector<double> input(32768);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 0.25 * std::sin(2.0 * 3.141592653589793 * 440.0 * static_cast<double>(i) / sampleRate);

    ats::CancellationToken live;
    std::vector<double> progress;
    const auto analysed = ats::AtsEngine::analyze({ 1 }, live, { input.data(), input.size(), sampleRate, {} },
        [&progress](double value) { progress.push_back(value); });
    require(analysed.status == ats::JobResult<ats::AnalysisOutput>::Status::success,
            "analysis status success");
    require(analysed.value.sound && !progress.empty(), "analysis model and progress");

    float left[8192] = {};
    float right[8192] = {};
    float* channels[] = { left, right };
    ats::AudioBufferView destination { channels, 2, 8192 };
    ats::SynthParams synth;
    const auto rendered = ats::AtsEngine::render({ 2 }, live, analysed.value.sound.get(), synth, destination);
    require(rendered.status == ats::JobResult<ats::RenderStats>::Status::success,
            "render status success");
    require(rendered.value.framesWritten > 0 && rendered.value.finite,
            "render frames and finite output");
    require(rendered.value.peak > 0.0f && left[0] == right[0],
            "render peak and channel equality");
    std::cout << "ats_engine_test: ATSA analysis/render contract ok\n";
}

#include "AtsEngine.h"

#include <cmath>
#include <cstdint>
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
    // Worker threads report concurrently; the engine must deliver them serially
    // and in order.
    for (std::size_t i = 1; i < progress.size(); ++i)
        require(progress[i] >= progress[i - 1], "progress is monotonic");

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
    // Clamp-to-source honours the analysis start: 1 s of silence then noise,
    // analysed from 1 s. Frame times are relative to the start, so a clamp that
    // read the file from sample 0 would cap the residual against the silence.
    {
        std::vector<double> padded(static_cast<std::size_t>(2 * sampleRate), 0.0);
        std::uint32_t state = 12345u;
        for (std::size_t i = sampleRate; i < padded.size(); ++i)
        {
            state = state * 1103515245u + 12345u;
            padded[i] = 0.2 * (static_cast<double>((state >> 8) & 0xffff) / 32768.0 - 1.0);
        }
        const auto residualEnergy = [&](bool clamp) {
            ats::AnalysisParams params;
            params.start = 1.0;
            params.duration = 0.5;
            params.clampResidual = clamp;
            const auto result = ats::AtsEngine::analyze({ 2 }, live,
                { padded.data(), padded.size(), sampleRate, params });
            require(result.status == ats::JobResult<ats::AnalysisOutput>::Status::success && result.value.sound,
                    "offset analysis success");
            const auto* s = result.value.sound.get();
            double total = 0.0;
            if (s->band_energy != nullptr)
                for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                    for (int f = 0; f < s->frames; ++f)
                        total += s->band_energy[b][f];
            return total;
        };
        const auto unclamped = residualEnergy(false);
        const auto clamped = residualEnergy(true);
        require(unclamped > 0.0, "noise leaves a residual");
        require(clamped > 0.5 * unclamped, "clamp reads the analysed span, not the leading silence");
    }
    std::cout << "ats_engine_test: ATSA analysis/render contract ok\n";
}

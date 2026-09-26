#include "../src/ui/SpectrumRenderer.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>

using namespace ats::ui;

int main()
{
    SpectralTraceSnapshot snapshot;
    snapshot.mode = SpectralTraceMode::partials;
    snapshot.frameCount = 720;
    snapshot.durationSeconds = 4.0;
    snapshot.minFrequencyHz = 20.0;
    snapshot.maxFrequencyHz = 20000.0;
    snapshot.partials.reserve(900);
    for (int partial = 0; partial < 900; ++partial)
    {
        SpectralTrace trace;
        trace.partialIndex = partial;
        trace.peakAmpNorm = 0.35f + 0.65f * static_cast<float>(partial % 17) / 16.0f;
        trace.points.reserve(720);
        for (std::uint32_t frame = 0; frame < 720; ++frame)
        {
            const auto t = static_cast<float>(frame) / 719.0f;
            const auto f = 0.02f + 0.96f * static_cast<float>(partial + 1) / 901.0f;
            const auto a = 0.35f + 0.55f * static_cast<float>((frame + partial) % 31) / 30.0f;
            trace.points.push_back({ frame, t, f, a, 20.0 + 19980.0 * f,
                                     20.0 + 19980.0 * f, a, true });
        }
        snapshot.partials.push_back(std::move(trace));
    }

    constexpr UiRect bounds { 0.0f, 0.0f, 1600.0f, 900.0f };
    SpectrumRenderOptions options;
    options.viewport.timeZoom = 1.0f;
    options.viewport.frequencyZoom = 1.0f;
    // Warm the instruction/cache path before measuring the steady-state render.
    (void) SpectrumRenderer::render(snapshot, bounds, options).size();
    std::vector<double> samples;
    samples.reserve(5);
    std::size_t commandCount = 0;
    for (int run = 0; run < 5; ++run)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto commands = SpectrumRenderer::render(snapshot, bounds, options);
        const auto finish = std::chrono::steady_clock::now();
        std::size_t pathCount = 0;
        for (const auto& command : commands)
        {
            assert(command.kind != SpectrumDrawKind::traceGlow);
            if (command.isPath())
                ++pathCount;
        }
        assert(pathCount == snapshot.partials.size());
        commandCount += commands.size();
        samples.push_back(std::chrono::duration<double, std::milli>(finish - start).count());
    }
    std::sort(samples.begin(), samples.end());
    const auto medianMs = samples[samples.size() / 2];
    std::cout << "spectrum_renderer 900x720 median_ms=" << medianMs
              << " commands=" << commandCount / samples.size() << '\n';
    assert(commandCount > 0);
    assert(medianMs <= 25.0);
    return 0;
}

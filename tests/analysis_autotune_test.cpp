#include "AnalysisAutoTune.h"

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
        std::cerr << "analysis_autotune_test failed: " << expression << '\n';
        std::abort();
    }
}
#define REQUIRE(x) require((x), #x)

constexpr int kSampleRate = 44100;
constexpr double kPi = 3.141592653589793;

// A decaying 220 Hz harmonic tone over light noise.
std::vector<double> tone()
{
    std::vector<double> x(kSampleRate); // 1 s
    unsigned state = 12345u;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double t = static_cast<double>(i) / kSampleRate;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += 0.2 / h * std::sin(2.0 * kPi * 220.0 * h * t);
        state = state * 1103515245u + 12345u;
        const double noise = (static_cast<double>((state >> 16) & 0x7fffu) / 32767.0 - 0.5) * 0.01;
        x[i] = v * std::exp(-2.0 * t) + noise;
    }
    return x;
}
}

int main()
{
    const auto x = tone();

    // Loss: zero for identical signals, larger for a worse match, gain-invariant.
    {
        const double same = ats::AnalysisAutoTune::spectralLoss(x.data(), x.size(), x.data(), x.size());
        REQUIRE(std::abs(same) < 1.0e-9);
        std::vector<double> louder(x), distorted(x);
        for (auto& v : louder) v *= 3.0;
        for (std::size_t i = 0; i < distorted.size(); ++i)
            distorted[i] += 0.05 * std::sin(2.0 * kPi * 3000.0 * static_cast<double>(i) / kSampleRate);
        const double gain = ats::AnalysisAutoTune::spectralLoss(x.data(), x.size(), louder.data(), louder.size());
        const double worse = ats::AnalysisAutoTune::spectralLoss(x.data(), x.size(), distorted.data(), distorted.size());
        // RMS normalisation makes level changes (nearly) free; the epsilon in
        // the normalisation leaves a small residue in near-silent bins.
        REQUIRE(gain < 0.01);
        REQUIRE(worse > 0.1 && worse > 50.0 * gain);
    }

    // Neighbours stay inside the parameter ranges and respect the cap.
    {
        ats::AnalysisParams start;
        start.lowest_freq = 100.0;
        start.highest_freq = 20000.0;
        start.win_cycles = 1;
        start.hop_size = 0.25;
        const auto all = ats::AnalysisAutoTune::neighbours(start, kSampleRate);
        REQUIRE(!all.empty());
        for (const auto& p : all)
        {
            REQUIRE(p.lowest_freq >= 1.0 && p.lowest_freq < p.highest_freq);
            REQUIRE(p.highest_freq <= 0.5 * kSampleRate);
            REQUIRE(p.win_cycles >= 1 && p.win_cycles <= 8);
        }
        for (const auto& p : ats::AnalysisAutoTune::neighbours(start, kSampleRate, 120.0))
            REQUIRE(p.lowest_freq <= 120.0);
    }

    // A run never ends worse than its start, keeps the fundamental, and reports progress.
    {
        ats::AutoTuneRequest request;
        request.samples = x.data();
        request.sampleCount = x.size();
        request.sampleRate = kSampleRate;
        request.budget = 10;
        ats::CancellationToken live;
        double lastProgress = 0.0;
        const auto result = ats::AnalysisAutoTune::run({ 1 }, live, request,
            [&lastProgress](double value) { lastProgress = value; });
        REQUIRE(result.status == ats::JobResult<ats::AutoTuneResult>::Status::success);
        REQUIRE(result.value.evaluations >= 1 && result.value.evaluations <= request.budget);
        REQUIRE(result.value.loss <= result.value.initialLoss);
        REQUIRE(!result.value.history.empty());
        REQUIRE(result.value.lowestFrequencyCap > 0.0);
        REQUIRE(result.value.lowestFrequencyCap < 220.0); // 0.9 x the 220 Hz fundamental
        REQUIRE(result.value.parameters.lowest_freq <= 220.0);
        REQUIRE(std::abs(lastProgress - 1.0) < 1.0e-9);
    }

    // Cancellation and invalid input.
    {
        ats::AutoTuneRequest request;
        request.samples = x.data();
        request.sampleCount = x.size();
        request.sampleRate = kSampleRate;
        ats::CancellationToken cancelled;
        cancelled.cancel();
        REQUIRE(ats::AnalysisAutoTune::run({ 2 }, cancelled, request).status
                == ats::JobResult<ats::AutoTuneResult>::Status::cancelled);
        ats::AutoTuneRequest empty;
        ats::CancellationToken live;
        REQUIRE(ats::AnalysisAutoTune::run({ 3 }, live, empty).status
                == ats::JobResult<ats::AutoTuneResult>::Status::failed);
    }

    std::cout << "analysis_autotune_test: ok\n";
    return 0;
}

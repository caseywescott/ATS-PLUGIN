// Analysis-by-synthesis auto-tune of the analysis resolution settings for a
// WAV file (see src/engine/AnalysisAutoTune.h).  Starts from the plugin's
// default analysis settings unless overridden, and prints every accepted step.
//
// Usage: ats_autotune input.wav [--budget N] [--lowest HZ] [--highest HZ]
//                               [--cycles N] [--hop RATIO]

#include "AnalysisAutoTune.h"

#include <sndfile.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s input.wav [--budget N] [--lowest HZ] [--highest HZ] "
                             "[--cycles N] [--hop RATIO]\n", argv[0]);
        return 2;
    }
    ats::AutoTuneRequest request;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string flag = argv[i];
        const double value = std::atof(argv[i + 1]);
        if (flag == "--budget") request.budget = static_cast<int>(value);
        else if (flag == "--lowest") request.start.lowest_freq = value;
        else if (flag == "--highest") request.start.highest_freq = value;
        else if (flag == "--cycles") request.start.win_cycles = static_cast<int>(value);
        else if (flag == "--hop") request.start.hop_size = value;
        else { std::fprintf(stderr, "unknown option %s\n", flag.c_str()); return 2; }
    }

    SF_INFO info {};
    SNDFILE* file = sf_open(argv[1], SFM_READ, &info);
    if (file == nullptr)
    {
        std::fprintf(stderr, "could not open %s: %s\n", argv[1], sf_strerror(nullptr));
        return 1;
    }
    std::vector<double> interleaved(static_cast<std::size_t>(info.frames * info.channels));
    sf_readf_double(file, interleaved.data(), info.frames);
    sf_close(file);
    std::vector<double> mono(static_cast<std::size_t>(info.frames), 0.0);
    for (sf_count_t i = 0; i < info.frames; ++i)
        for (int c = 0; c < info.channels; ++c)
            mono[static_cast<std::size_t>(i)] += interleaved[static_cast<std::size_t>(i * info.channels + c)]
                / info.channels;
    request.samples = mono.data();
    request.sampleCount = mono.size();
    request.sampleRate = info.samplerate;

    ats::CancellationToken cancellation;
    const auto result = ats::AnalysisAutoTune::run(ats::JobGeneration {}, cancellation, request);
    if (result.status != ats::JobResult<ats::AutoTuneResult>::Status::success)
    {
        std::fprintf(stderr, "auto-tune failed: %s\n", result.error.text.c_str());
        return 1;
    }
    const auto& tuned = result.value;
    for (const auto& step : tuned.history)
        std::printf("loss %.4f  partials %4d  lowest %.1f Hz  highest %.0f Hz  cycles %d  hop %.3f\n",
                    step.loss, step.partials, step.parameters.lowest_freq,
                    step.parameters.highest_freq, step.parameters.win_cycles,
                    step.parameters.hop_size);
    std::printf("lowest-frequency cap %.1f Hz\n", tuned.lowestFrequencyCap);
    std::printf("evaluations %d  loss %.4f -> %.4f (%+.1f%%)\n", tuned.evaluations, tuned.initialLoss,
                tuned.loss, 100.0 * (tuned.loss - tuned.initialLoss) / tuned.initialLoss);
    return 0;
}

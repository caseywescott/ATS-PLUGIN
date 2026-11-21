// AUTO-TUNE on the ANALYSIS page: the host searches the current model's
// resolution settings, writes them as one undoable change, and re-analyses.
// CANCEL stops it without touching the parameters.

#include "../src/host/HostAdapter.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
// A decaying 220 Hz harmonic tone: analysed at the 20 Hz default the window is
// 0.2 s, so a shorter one resynthesises the decay more closely.
std::filesystem::path writeWave()
{
    constexpr int rate = 44100;
    std::vector<std::int16_t> samples(rate);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const double t = static_cast<double>(i) / rate;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h)
            v += std::sin(2.0 * 3.141592653589793 * 220.0 * h * t) / h;
        samples[i] = static_cast<std::int16_t>(7000.0 * v * std::exp(-3.0 * t));
    }
    const auto path = std::filesystem::temp_directory_path() / "ats-host-autotune.wav";
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
    u32(rate); u32(rate * 2); u16(2); u16(16); out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
    return path;
}

struct Settings
{
    float lowest, highest, cycles, hop;
    bool operator==(const Settings& o) const
    { return lowest == o.lowest && highest == o.highest && cycles == o.cycles && hop == o.hop; }
};

Settings settings(ats::host::HostAdapter& host)
{
    return { host.parameters().read("analysisLowestFreq"), host.parameters().read("analysisHighestFreq"),
             host.parameters().read("analysisWinCycles"), host.parameters().read("analysisHopSize") };
}

// Polls until the auto-tune and the analysis it starts have both finished.
bool runToIdle(ats::host::HostAdapter& host, ats::Error& error)
{
    for (int i = 0; i < 120000; ++i)
    {
        ats::Error step;
        (void) host.pollAnalysis(step);
        if (step) { error = step; return false; }
        if (!host.editorSnapshot().telemetry.analysisRunning)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
}

int main()
{
    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2 }));
    ats::Error error;

    // No current source yet.
    assert(host.applyEditorAction(ats::ui::AutoTuneAnalysisAction {}, error));
    assert(error.code == "missing_current_source");

    const auto wav = writeWave();
    assert(host.applyEditorAction(ats::ui::ImportWavAction { { wav.u8string() }, 3 }, error) && !error);
    const auto before = settings(host);

    // CANCEL right after starting: no parameter changes, no model.
    assert(host.applyEditorAction(ats::ui::AutoTuneAnalysisAction {}, error) && !error);
    assert(host.editorSnapshot().telemetry.analysisRunning);
    // A second start while running is refused.
    assert(host.applyEditorAction(ats::ui::AutoTuneAnalysisAction {}, error));
    assert(error.code == "analysis_busy");
    error = {};
    assert(host.applyEditorAction(ats::ui::CancelJobsAction {}, error) && !error);
    assert(runToIdle(host, error));
    assert(settings(host) == before);
    assert(!host.editorSnapshot().telemetry.hasCurrentModel);

    // A full run: settings change, the follow-up analysis installs a model.
    assert(host.applyEditorAction(ats::ui::AutoTuneAnalysisAction {}, error) && !error);
    if (!runToIdle(host, error))
    {
        std::cerr << "auto-tune error: " << error.code << " " << error.text << '\n';
        return 1;
    }
    const auto tuned = settings(host);
    assert(!(tuned == before));
    assert(tuned.lowest <= 220.0f); // never above the 220 Hz fundamental
    assert(host.editorSnapshot().telemetry.hasCurrentModel);
    assert(host.editorSnapshot().telemetry.status.rfind("auto-tune:", 0) == 0);

    // One undo restores the previous settings.
    assert(host.applyEditorAction(ats::ui::HistoryAction { false }, error) && !error);
    assert(settings(host) == before);

    std::cout << "host_autotune_test: ok (" << tuned.lowest << " Hz, " << tuned.highest << " Hz, "
              << tuned.cycles << " cycles, hop " << tuned.hop << ")\n";
    return 0;
}

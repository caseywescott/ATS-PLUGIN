// A cancelled analysis must never be applied: CANCEL can land after the ATSA
// worker returns but before pollAnalysis collects the result, and the host
// then has to discard it rather than install the model the user cancelled.

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
std::filesystem::path writeWave(double seconds)
{
    constexpr int rate = 44100;
    std::vector<std::int16_t> samples(static_cast<std::size_t>(rate * seconds));
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<std::int16_t>(
            9000.0 * std::sin(2.0 * 3.141592653589793 * 330.0 * static_cast<double>(i) / rate));
    const auto path = std::filesystem::temp_directory_path() / "ats-analysis-cancel.wav";
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
    u32(rate); u32(rate * 2); u16(2); u16(16); out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
    return path;
}

bool waitForCompletion(ats::host::HostAdapter& host, ats::Error& error)
{
    for (int i = 0; i < 20000; ++i)
    {
        if (host.pollAnalysis(error))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
}

int main()
{
    const auto wav = writeWave(0.25);
    int cases = 0;
    // Cancel at increasing delays so some land mid-analysis and some after the
    // worker has already returned but before the result is polled.
    for (int delayMs : { 0, 1, 2, 5, 10, 20, 40, 80, 160, 320 })
    {
        ats::host::HostAdapter host;
        assert(host.initialize(ats::host::HostFormat::standalone));
        ats::Error error;
        assert(host.applyEditorAction(ats::ui::ImportWavAction { { wav.u8string() }, 3 }, error) && !error);
        assert(host.applyEditorAction(ats::ui::AnalyzeCurrentAction {}, error) && !error);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        assert(host.applyEditorAction(ats::ui::CancelJobsAction {}, error));
        const bool finished = waitForCompletion(host, error);
        assert(finished);
        const auto snapshot = host.editorSnapshot();
        if (snapshot.telemetry.hasCurrentModel)
        {
            std::cerr << "cancelled analysis applied (cancel after " << delayMs << " ms)\n";
            assert(false);
        }
        assert(snapshot.telemetry.state == ats::ui::UiState::readyToAnalyze);
        ++cases;
    }
    std::error_code ignored;
    std::filesystem::remove(wav, ignored);
    std::cout << "host_analysis_cancel_test: " << cases << " cancel timings never apply a result\n";
    return 0;
}

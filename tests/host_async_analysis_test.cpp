#include "../src/host/HostAdapter.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
void u16(std::ofstream& stream, std::uint16_t value)
{
    stream.put(static_cast<char>(value));
    stream.put(static_cast<char>(value >> 8));
}

void u32(std::ofstream& stream, std::uint32_t value)
{
    u16(stream, static_cast<std::uint16_t>(value));
    u16(stream, static_cast<std::uint16_t>(value >> 16));
}

void writeWave(const std::filesystem::path& path, std::uint32_t frames)
{
    constexpr std::uint32_t rate = 48000;
    const auto bytes = frames * 2;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream);
    stream.write("RIFF", 4); u32(stream, 36 + bytes); stream.write("WAVEfmt ", 8); u32(stream, 16);
    u16(stream, 1); u16(stream, 1); u32(stream, rate); u32(stream, rate * 2); u16(stream, 2); u16(stream, 16);
    stream.write("data", 4); u32(stream, bytes);
    for (std::uint32_t index = 0; index < frames; ++index)
        u16(stream, static_cast<std::uint16_t>(std::lround(
            7000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * index / rate))));
    assert(stream);
}

ats::Error waitForAnalysis(ats::host::HostAdapter& host)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    ats::Error result;
    while (std::chrono::steady_clock::now() < deadline)
    {
        ats::Error error;
        if (host.pollAnalysis(error))
            return error;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return { "timeout", "queued analysis did not complete" };
}
}

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "ats-host-async-analysis.wav";
    writeWave(path, 32768);

    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2, 0 }));
    ats::Error error;
    assert(host.importSource({ path.u8string() }, 0, ats::engine::SourceImportKind::audio, error));
    assert(host.queueAnalysisSource(0, error));
    assert(host.analysisRunning());
    assert(!host.queueAnalysisSource(0, error) && error.code == "analysis_busy");
    error = waitForAnalysis(host);
    assert(!error && !host.analysisRunning());
    assert(host.source(0) != nullptr && host.source(0)->sound);

    float left[512] {}, right[512] {};
    float* channels[] { left, right };
    assert(host.process({ channels, 2, 512 }, {}));
    bool audible = false;
    for (const auto sample : left)
        audible = audible || std::abs(sample) > 1.0e-6f;
    assert(audible);

    assert(host.queueAnalysisSource(0, error));
    host.cancelAnalysis();
    error = waitForAnalysis(host);
    assert(error.code == "cancelled");
    assert(!host.analysisRunning());

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

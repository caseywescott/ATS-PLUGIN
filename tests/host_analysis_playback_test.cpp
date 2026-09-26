#include "../src/host/HostAdapter.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace
{
void u16(std::ofstream& s, std::uint16_t v) { s.put(static_cast<char>(v)); s.put(static_cast<char>(v >> 8)); }
void u32(std::ofstream& s, std::uint32_t v) { u16(s, static_cast<std::uint16_t>(v)); u16(s, static_cast<std::uint16_t>(v >> 16)); }
void writeWave(const std::filesystem::path& path)
{
    constexpr std::uint32_t frames = 32768, rate = 48000, bytes = frames * 2;
    std::ofstream s(path, std::ios::binary | std::ios::trunc);
    assert(s); s.write("RIFF", 4); u32(s, 36 + bytes); s.write("WAVEfmt ", 8); u32(s, 16);
    u16(s, 1); u16(s, 1); u32(s, rate); u32(s, rate * 2); u16(s, 2); u16(s, 16);
    s.write("data", 4); u32(s, bytes);
    for (std::uint32_t i = 0; i < frames; ++i)
        u16(s, static_cast<std::uint16_t>(std::lround(7000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * i / rate))));
    assert(s);
}
}

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "ats-host-analysis.wav";
    writeWave(path);
    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2, 0 }));
    ats::Error error;
    assert(host.importSource({ path.u8string() }, 0, ats::engine::SourceImportKind::audio, error));
    assert(host.parameters().write("analysisLowestFreq", 100.0f));
    assert(host.parameters().write("analysisHighestFreq", 1000.0f));
    assert(host.analyzeSource(0, error));
    assert(host.source(0) != nullptr && host.source(0)->sound);
    const auto* sound = host.source(0)->sound.get();
    for (int partial = 0; partial < sound->partials; ++partial)
        for (int frame = 0; frame < sound->frames; ++frame)
            if (sound->amp[partial][frame] > 0.0)
                assert(sound->frq[partial][frame] >= 100.0 && sound->frq[partial][frame] <= 1000.0);
    float left[512] {}, right[512] {}; float* channels[] { left, right };
    assert(host.process({ channels, 2, 512 }, {}));
    bool audible = false;
    for (const auto sample : left) audible = audible || std::abs(sample) > 1.0e-6f;
    assert(!audible);

    assert(host.applySessionAction(ats::ui::PreviewAction { 0, 0, true }, error));
    std::fill(std::begin(left), std::end(left), 0.0f);
    std::fill(std::begin(right), std::end(right), 0.0f);
    assert(host.process({ channels, 2, 512 }, {}));
    audible = false;
    for (const auto sample : left) audible = audible || std::abs(sample) > 1.0e-6f;
    assert(audible);
    std::error_code ignored; std::filesystem::remove(path, ignored);
}

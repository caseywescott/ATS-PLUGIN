#include "../src/engine/AtsEngine.h"
#include "../src/engine/SourceImportService.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
void writeU16(std::ofstream& stream, std::uint16_t value)
{
    stream.put(static_cast<char>(value & 0xffu));
    stream.put(static_cast<char>((value >> 8u) & 0xffu));
}

void writeU32(std::ofstream& stream, std::uint32_t value)
{
    writeU16(stream, static_cast<std::uint16_t>(value & 0xffffu));
    writeU16(stream, static_cast<std::uint16_t>(value >> 16u));
}

void writeStereoWave(const std::filesystem::path& path)
{
    constexpr std::uint32_t sampleRate = 48000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bitsPerSample = 16;
    const std::int16_t samples[][2] = {
        { 1000, -1000 }, { 2000, 0 }, { -2000, 2000 }, { 4000, 4000 }
    };
    constexpr std::uint32_t dataBytes = sizeof(samples);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream);
    stream.write("RIFF", 4);
    writeU32(stream, 36 + dataBytes);
    stream.write("WAVEfmt ", 8);
    writeU32(stream, 16);
    writeU16(stream, 1);
    writeU16(stream, channels);
    writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * channels * bitsPerSample / 8);
    writeU16(stream, channels * bitsPerSample / 8);
    writeU16(stream, bitsPerSample);
    stream.write("data", 4);
    writeU32(stream, dataBytes);
    for (const auto& frame : samples)
        for (const auto sample : frame)
            writeU16(stream, static_cast<std::uint16_t>(sample));
    assert(stream);
}
}

int main()
{
    const auto folder = std::filesystem::temp_directory_path() / "ats-source-import-service-test";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder, error);
    assert(!error);

    const auto wav = folder / "source.WAV";
    writeStereoWave(wav);
    ats::CancellationToken live;
    const auto decoded = ats::engine::SourceImportService::import(
        { 1 }, live, { { wav.u8string() }, 0, ats::engine::SourceImportKind::audio });
    assert(decoded.status == ats::JobResult<ats::engine::ImportedSource>::Status::success);
    assert(decoded.value.slot == 0 && decoded.value.sampleRate == 48000);
    assert(decoded.value.monoSamples.size() == 4);
    assert(std::abs(decoded.value.monoSamples[0]) < 1.0e-12);
    assert(std::abs(decoded.value.monoSamples[1] - (1000.0 / 32768.0)) < 1.0e-7);

    std::vector<double> sine(32768);
    for (std::size_t index = 0; index < sine.size(); ++index)
        sine[index] = 0.25 * std::sin(2.0 * 3.141592653589793 * 440.0 *
                                      static_cast<double>(index) / 48000.0);
    const auto analysed = ats::AtsEngine::analyze({ 2 }, live,
        { sine.data(), sine.size(), 48000, {} });
    assert(analysed.status == ats::JobResult<ats::AnalysisOutput>::Status::success);
    const auto atsPath = folder / "source.ats";
    assert(ats_save(analysed.value.sound.get(), atsPath.string().c_str(), -120.0, 1) > 0);
    const auto loaded = ats::engine::SourceImportService::import(
        { 3 }, live, { { atsPath.u8string() }, 1, ats::engine::SourceImportKind::ats });
    assert(loaded.status == ats::JobResult<ats::engine::ImportedSource>::Status::success);
    assert(loaded.value.slot == 1 && loaded.value.sound);
    assert(loaded.value.sampleRate == 48000 && loaded.value.atsType == 1);

    const auto wrongKind = ats::engine::SourceImportService::import(
        { 4 }, live, { { wav.u8string() }, 0, ats::engine::SourceImportKind::ats });
    assert(wrongKind.status == ats::JobResult<ats::engine::ImportedSource>::Status::failed);
    assert(wrongKind.error.code == "unsupported_extension");
    ats::CancellationToken cancelled;
    cancelled.cancel();
    const auto cancelledResult = ats::engine::SourceImportService::import(
        { 5 }, cancelled, { { wav.u8string() }, 0, ats::engine::SourceImportKind::audio });
    assert(cancelledResult.status == ats::JobResult<ats::engine::ImportedSource>::Status::cancelled);

    std::filesystem::remove_all(folder, error);
    return 0;
}

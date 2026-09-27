// The ANALYSIS workflow targets the current model (slot 3): import there,
// AnalyzeCurrent analyses it, and SESSION's CURRENT preview and MATH edits act
// on the result. A regression routed AnalyzeCurrent to morph source A, so the
// preview reported missing_current_model and edits had no model (found by the
// RMJ-032 standalone dispatch matrix).

#include "../src/host/HostAdapter.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
std::filesystem::path writeWave()
{
    constexpr int rate = 44100;
    std::vector<std::int16_t> samples(rate / 2);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<std::int16_t>(
            9000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * static_cast<double>(i) / rate));
    const auto path = std::filesystem::temp_directory_path() / "ats-current-model-workflow.wav";
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
    u32(rate); u32(rate * 2); u16(2); u16(16); out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
    return path;
}
}

int main()
{
    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2 }));
    ats::Error error;

    // Nothing imported as current yet.
    assert(host.applyEditorAction(ats::ui::AnalyzeCurrentAction {}, error));
    assert(error.code == "missing_current_source");

    const auto wav = writeWave();
    assert(host.applyEditorAction(ats::ui::ImportWavAction { { wav.u8string() }, 3 }, error) && !error);
    assert(host.source(0) == nullptr); // morph source A untouched
    assert(host.applyEditorAction(ats::ui::AnalyzeCurrentAction {}, error) && !error);
    bool done = false;
    for (int i = 0; i < 30000 && !done && !error; ++i)
    {
        done = host.pollAnalysis(error);
        if (!done)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (error)
        std::cerr << "analysis error: " << error.code << " " << error.text << '\n';
    assert(done && !error);
    assert(host.source(0) == nullptr);
    assert(host.editorSnapshot().telemetry.hasCurrentModel);

    // SESSION CURRENT preview (source 3) is audible.
    assert(host.applySessionAction(ats::ui::PreviewAction { 3, 0, true }, error) && !error);
    std::vector<float> left(512), right(512);
    float* channels[] { left.data(), right.data() };
    bool audible = false;
    for (int block = 0; block < 40 && !audible; ++block)
    {
        assert(host.process({ channels, 2, 512 }, {}));
        for (const auto sample : left)
            audible = audible || std::abs(sample) > 1.0e-5f;
    }
    assert(audible);
    assert(host.applySessionAction(ats::ui::StopPreviewAction {}, error) && !error);

    // MATH edits the current model and is undoable.
    const auto before = host.captureState();
    assert(host.applyEditorAction(ats::ui::ApplyMathAction { "a=a*0.5\n" }, error) && !error);
    assert(host.captureState() != before);
    assert(host.editorSnapshot().telemetry.canUndo);

    std::error_code ignored;
    std::filesystem::remove(wav, ignored);
    std::cout << "host_current_model_workflow_test: import/analyse/preview/edit current ok\n";
    return 0;
}

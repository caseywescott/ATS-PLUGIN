// Release compatibility matrix (card 33, audit T-301):
//  1. Audio: 44.1/48/88.2/96 kHz x block sizes x stereo/mono output views x
//     in-block MIDI, rendering a real preview of an analysed model. Every
//     sample must be finite and bounded, and the output must be audible.
//  2. Parameters: each of the 81 parameters at both range edges must survive
//     captureState -> restoreState into a fresh host byte-identically.

#include "../src/host/HostAdapter.h"
#include "../src/engine/AtsEngine.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
[[noreturn]] void fail(const std::string& what)
{
    std::cerr << "release_matrix_test failed: " << what << '\n';
    std::abort();
}

void require(bool condition, const std::string& what)
{
    if (!condition)
        fail(what);
}

std::filesystem::path writeFixtureAts()
{
    constexpr int sampleRate = 44100;
    std::vector<double> input(32768);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        input[i] = 0.25 * std::sin(2.0 * 3.141592653589793 * 330.0 * t)
                 + 0.02 * (static_cast<double>((i * 7919) % 2001) / 1000.0 - 1.0);
    }
    ats::CancellationToken live;
    const auto analysed = ats::AtsEngine::analyze({ 1 }, live,
                                                  { input.data(), input.size(), sampleRate, {} }, {});
    require(analysed.status == ats::JobResult<ats::AnalysisOutput>::Status::success, "fixture analysis");
    const auto path = std::filesystem::temp_directory_path() / "ats-release-matrix.ats";
    require(ats_save(analysed.value.sound.get(), path.string().c_str(), -120.0, 4) > 0, "fixture save");
    return path;
}

void audioMatrix(const std::filesystem::path& fixture)
{
    constexpr std::array<double, 4> rates { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr std::array<std::uint32_t, 7> blocks { 1, 16, 64, 480, 512, 1024, 4096 };
    int cases = 0;
    for (const auto rate : rates)
        for (const auto block : blocks)
            for (const std::uint32_t channels : { 2u, 1u })
            {
                const auto label = std::to_string(static_cast<int>(rate)) + " Hz / block "
                                 + std::to_string(block) + " / " + std::to_string(channels) + " ch";
                ats::host::HostAdapter host;
                require(host.initialize(ats::host::HostFormat::vst3), label + ": initialize");
                require(host.prepare({ rate, block, 2 }), label + ": prepare");
                ats::Error error;
                require(host.applyEditorAction(ats::ui::ImportAtsAction { { fixture.u8string() }, 0 }, error)
                            && !error,
                        label + ": import");
                require(host.applySessionAction(ats::ui::PreviewAction { 0, 0, true }, error) && !error,
                        label + ": preview");

                std::vector<float> left(block), right(block);
                float* channelPointers[] { left.data(), right.data() };
                const auto totalFrames = static_cast<std::uint32_t>(rate * 0.25);
                bool audible = false;
                std::uint32_t rendered = 0;
                int blockIndex = 0;
                while (rendered < totalFrames)
                {
                    // A note on, a transposed note on and a note off land at
                    // varying offsets inside blocks as the render advances.
                    std::vector<ats::MidiEvent> events;
                    if (blockIndex % 7 == 3)
                        events.push_back({ static_cast<std::uint32_t>(blockIndex % block), ats::MidiEvent::Type::noteOn,
                                           static_cast<std::uint8_t>(48 + blockIndex % 24), 100 });
                    if (blockIndex % 11 == 5)
                        events.push_back({ block - 1, ats::MidiEvent::Type::noteOff, 60, 0 });
                    const bool ok = host.process({ channelPointers, channels, block },
                                                 { events.data(), static_cast<std::uint32_t>(events.size()) });
                    require(ok, label + ": process");
                    for (std::uint32_t c = 0; c < channels; ++c)
                        for (std::uint32_t i = 0; i < block; ++i)
                        {
                            const float sample = channelPointers[c][i];
                            require(std::isfinite(sample) && std::abs(sample) <= 4.0f,
                                    label + ": sample finite and bounded");
                            audible = audible || std::abs(sample) > 1.0e-5f;
                        }
                    rendered += block;
                    ++blockIndex;
                }
                require(audible, label + ": preview is audible");
                host.release();
                ++cases;
            }
    std::cout << "audio matrix: " << cases << " rate/block/channel cases ok\n";
}

void parameterEdges()
{
    int cases = 0;
    std::vector<std::string> lost;
    for (const auto& descriptor : ats::ParameterRegistry::descriptors())
        for (const float normalized : { 0.0f, 1.0f })
        {
            const auto label = std::string(descriptor.id) + " @ " + (normalized == 0.0f ? "min" : "max");
            ats::host::HostAdapter host;
            require(host.initialize(ats::host::HostFormat::vst3), label + ": initialize");
            require(host.setParameterNormalized(descriptor.hostNumericId, normalized), label + ": set");
            const auto expected = descriptor.snap(descriptor.denormalize(normalized));
            require(std::abs(host.parameters().read(descriptor.denseIndex) - expected) <= 1.0e-4f *
                        std::max(1.0f, std::abs(expected)),
                    label + ": read back");
            const auto state = host.captureState();

            ats::host::HostAdapter reopened;
            require(reopened.initialize(ats::host::HostFormat::vst3), label + ": reopen initialize");
            require(reopened.restoreState(state), label + ": restore");
            if (reopened.captureState() != state)
                lost.push_back(label + " (state not byte-identical after restore)");
            if (descriptor.participatesInState &&
                std::abs(reopened.parameters().read(descriptor.denseIndex) - expected) >
                    1.0e-4f * std::max(1.0f, std::abs(expected)))
                lost.push_back(label + " (restored " +
                               std::to_string(reopened.parameters().read(descriptor.denseIndex)) + ")");
            ++cases;
        }
    for (const auto& entry : lost)
        std::cerr << "not persisted: " << entry << '\n';
    require(lost.empty(), std::to_string(lost.size()) + " parameter edge(s) lost across save/restore");
    std::cout << "parameter edges: " << cases << " parameter/edge cases ok\n";
}
}

int main()
{
    require(ats::ParameterRegistry::descriptors().size() == 93, "93-parameter contract");
    const auto fixture = writeFixtureAts();
    audioMatrix(fixture);
    parameterEdges();
    std::error_code ignored;
    std::filesystem::remove(fixture, ignored);
    std::cout << "release_matrix_test: ok\n";
    return 0;
}

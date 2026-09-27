// Differential wrapper matrix (completion spec §6, audit T-205).
//
// The AU and standalone wrappers hand editor actions straight to
// HostAdapter::applyEditorAction. The VST3 wrapper instead encodes each action
// in the controller (handleEditorAction -> IMessage) and decodes it in the
// processor's notify(). This test drives the same action script through both
// paths and requires byte-identical captured state after every step, so any
// field the VST3 bridge drops or reinterprets shows up as a diff.
//
// The plugin translation unit is compiled in directly so the real controller
// and processor classes run in-process, connected like a host would connect
// them.

#define ATS_VST3_DIFFERENTIAL_TEST 1
#include "../../src/host/vst3/AtsVst3Plugin.cpp"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include "../../src/engine/AtsEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <utility>
#include <string>
#include <vector>

namespace
{
[[noreturn]] void fail(const std::string& what)
{
    std::cerr << "vst3_differential_test failed: " << what << '\n';
    std::abort();
}

void require(bool condition, const std::string& what)
{
    if (!condition)
        fail(what);
}

std::vector<std::byte> processorState(ats::vst3::AtsVst3Processor& processor)
{
    Steinberg::MemoryStream stream;
    require(processor.getState(&stream) == Steinberg::kResultTrue, "processor getState");
    const auto size = static_cast<std::size_t>(stream.getSize());
    std::vector<std::byte> bytes(size);
    Steinberg::int64 position = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, &position);
    Steinberg::int32 read = 0;
    stream.read(bytes.data(), static_cast<Steinberg::int32>(size), &read);
    bytes.resize(static_cast<std::size_t>(read));
    return bytes;
}

std::filesystem::path writeFixtureAts()
{
    constexpr int sampleRate = 44100;
    std::vector<double> input(32768);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        input[i] = 0.2 * std::sin(2.0 * 3.141592653589793 * 440.0 * t)
                 + 0.1 * std::sin(2.0 * 3.141592653589793 * 1320.0 * t)
                 + 0.02 * (static_cast<double>((i * 7919) % 2001) / 1000.0 - 1.0);
    }
    ats::CancellationToken live;
    const auto analysed = ats::AtsEngine::analyze({ 1 }, live,
                                                  { input.data(), input.size(), sampleRate, {} }, {});
    require(analysed.status == ats::JobResult<ats::AnalysisOutput>::Status::success,
            "fixture analysis");
    const auto path = std::filesystem::temp_directory_path() / "ats-vst3-differential.ats";
    // Type 4: amplitude, frequency, phase and residual noise.
    require(ats_save(analysed.value.sound.get(), path.string().c_str(), -120.0, 4) > 0,
            "fixture ats_save");
    return path;
}

// Editor-facing projection of the host: what the UI shows after an action.
// Revision counters are excluded; they are per-instance publication tokens.
std::string editorView(const ats::host::HostAdapter& host)
{
    const auto snapshot = host.editorSnapshot();
    std::ostringstream out;
    out.precision(9);
    out << "timeline:";
    for (const auto value : snapshot.timeline) out << value << ',';
    out << " timbre:";
    for (const auto value : snapshot.timbrePlan) out << value << ',';
    out << " onsets:";
    for (const auto value : snapshot.onsets) out << value << ',';
    for (const auto& [name, spectrum] : { std::pair { "spectrum", &snapshot.spectrum },
                                          std::pair { "overlay", &snapshot.spectrumOverlay } })
    {
        out << ' ' << name << ':';
        if (!spectrum->has_value())
            continue;
        const auto& trace = **spectrum;
        out << trace.partialCount << 'x' << trace.frameCount << " handles:";
        for (const auto& onset : trace.onsets)
            out << onset.norm << '/' << onset.preNorm << '/' << onset.peakNorm << '/'
                << onset.strength << ',';
    }
    return out.str();
}

std::string hexOffset(const std::vector<std::byte>& a, const std::vector<std::byte>& b)
{
    std::size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i])
        ++i;
    return "sizes " + std::to_string(a.size()) + " vs " + std::to_string(b.size())
         + ", first difference at byte " + std::to_string(i);
}
}

int main()
{
    using namespace ats::ui;
    const auto fixture = writeFixtureAts();

    // Reference: the AU / standalone path.
    ats::host::HostAdapter reference;
    require(reference.initialize(ats::host::HostFormat::vst3), "reference initialize");
    require(reference.prepare({ 48000.0, 512, 2, 0 }), "reference prepare");

    // VST3 path: real controller and processor, connected in-process.
    Steinberg::Vst::HostApplication hostApplication;
    Steinberg::IPtr<ats::vst3::AtsVst3Processor> processor(new ats::vst3::AtsVst3Processor(), false);
    Steinberg::IPtr<ats::vst3::AtsVst3Controller> controller(
        static_cast<ats::vst3::AtsVst3Controller*>(
            static_cast<Steinberg::Vst::IEditController*>(
                ats::vst3::AtsVst3Controller::createInstance(nullptr))),
        false);
    require(processor->initialize(&hostApplication) == Steinberg::kResultTrue, "processor initialize");
    require(controller->initialize(&hostApplication) == Steinberg::kResultTrue, "controller initialize");
    require(controller->connect(processor) == Steinberg::kResultTrue &&
                processor->connect(controller) == Steinberg::kResultTrue,
            "connect controller and processor");
    Steinberg::Vst::ProcessSetup setup { Steinberg::Vst::kRealtime, Steinberg::Vst::kSample32, 512, 48000.0 };
    require(processor->setupProcessing(setup) == Steinberg::kResultTrue, "processor setupProcessing");
    require(processor->setActive(true) == Steinberg::kResultTrue, "processor setActive");

    require(processorState(*processor) == reference.captureState(), "initial state differs");

    std::vector<SpectralOnset> handles {
        { 0.20f, 0.15f, 0.26f, 0.8f },
        { 0.61f, 0.55f, 0.70f, 0.5f } };
    const std::vector<std::pair<std::string, EditorAction>> script {
        { "import ats into slot 0", ImportAtsAction { { fixture.u8string() }, 0 } },
        { "import ats into slot 1", ImportAtsAction { { fixture.u8string() }, 1 } },
        { "typed morph timeline", SetMorphTimelineAction { {}, {
              { 0.0f, 0.1f, 0, 0 }, { 0.4f, 0.8f, 1, 2 }, { 1.0f, 0.3f, 2, 1 } } } },
        { "simple morph timeline", SetMorphTimelineAction { { 0.0f, 0.5f, 1.0f }, {} } },
        { "timbre plan json", SetTimbreMotionPlanAction {
              {}, R"({"version":1,"lanes":[{"id":"brightness","points":[]}]})" } },
        { "onset markers", EditSmearnovOnsetsAction { { 0.25f, 0.75f } } },
        { "onset handles", EditSmearnovOnsetHandlesAction { handles } },
        { "spectral view", SetSpectralViewAction { 1, 3 } },
        { "partial cell write", PartialEditAction { 0, 2, true, 0.25, 452.0 } },
        { "residual cell write", ResidualEditAction { 3, 2, 0.5f } },
        { "spectrum scale partials", SpectrumEditAction {
              SpectrumEditCommand::scalePartials, { 0 }, 0, 4, 0.5f, {} } },
        { "spectrum draw partial frequency", SpectrumEditAction {
              SpectrumEditCommand::drawPartialFrequency, { 0 }, 1, 3, 0.0f,
              { { 1, 445.0f }, { 3, 447.0f } } } },
        { "spectrum mute residual", SpectrumEditAction {
              SpectrumEditCommand::muteResidual, { 2, 4 }, 0, 5, 0.0f, {} } },
        { "math script", ApplyMathAction { "a=a*0.9\n" } },
        { "factory preset", ApplyFactoryPresetAction { 3 } },
        { "morph variation store", MorphVariationAction { 1, true } },
        { "undo", HistoryAction { false } },
        { "redo", HistoryAction { true } },
    };

    int failures = 0;
    for (const auto& [name, action] : script)
    {
        const auto before = reference.captureState();
        ats::Error error;
        (void) reference.applyEditorAction(action, error);
        controller->handleEditorAction(action);
        const auto expected = reference.captureState();
        const auto actual = processorState(*processor);
        const bool changed = expected != before;
        const auto expectedView = editorView(reference);
        const auto actualView = editorView(processor->hostForTest());
        const bool same = expected == actual && expectedView == actualView;
        std::cout << (same ? "same " : "DIFF ") << (changed ? "        " : "(no-op) ") << name
                  << (error ? "  [reference error: " + error.code + "]" : std::string()) << '\n';
        if (!same)
        {
            ++failures;
            if (expected != actual)
                std::cerr << "  " << name << " state: " << hexOffset(expected, actual) << '\n';
            if (expectedView != actualView)
                std::cerr << "  " << name << " editor view:\n    direct: " << expectedView
                          << "\n    vst3:   " << actualView << '\n';
        }
    }

    processor->setActive(false);
    controller->disconnect(processor);
    processor->disconnect(controller);
    controller->terminate();
    processor->terminate();
    std::error_code ignored;
    std::filesystem::remove(fixture, ignored);

    if (failures != 0)
        fail(std::to_string(failures) + " action(s) produced different state via the VST3 bridge");
    std::cout << "vst3_differential_test: VST3 bridge state matches the direct host path\n";
    return 0;
}

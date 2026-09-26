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

void writeWave(const std::filesystem::path& path, double frequency = 440.0)
{
    constexpr std::uint32_t frames = 32768;
    constexpr std::uint32_t rate = 48000;
    constexpr std::uint32_t bytes = frames * 2;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream);
    stream.write("RIFF", 4); u32(stream, 36 + bytes); stream.write("WAVEfmt ", 8); u32(stream, 16);
    u16(stream, 1); u16(stream, 1); u32(stream, rate); u32(stream, rate * 2); u16(stream, 2); u16(stream, 16);
    stream.write("data", 4); u32(stream, bytes);
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto sample = std::lround(9000.0 * std::sin(2.0 * 3.141592653589793 * frequency * frame / rate));
        u16(stream, static_cast<std::uint16_t>(static_cast<std::int16_t>(sample)));
    }
    assert(stream);
}

bool waitForSessionWork(ats::host::HostAdapter& host, ats::Error& error)
{
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        if (host.pollAnalysis(error))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
}

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "ats-host-session.wav";
    const auto pathB = std::filesystem::temp_directory_path() / "ats-host-session-b.wav";
    writeWave(path);
    writeWave(pathB, 880.0);

    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2, 0 }));
    ats::Error error;
    assert(host.importSource({ path.u8string() }, 0, ats::engine::SourceImportKind::audio, error));
    assert(host.analyzeSource(0, error));
    assert(host.importSource({ pathB.u8string() }, 1, ats::engine::SourceImportKind::audio, error));
    assert(host.analyzeSource(1, error));

    // The spectrum snapshot and its A/B overlay must publish the active
    // frequency-shape settings, including the resolved pivot used by the
    // analyzed sound rather than leaving the UI at its default Linear state.
    assert(host.applyEditorAction(ats::ui::SetSpectralViewAction { 1, 0 }, error));
    assert(!error);
    assert(host.parameters().write("freqShapeMode", 3.0f));
    assert(host.parameters().write("freqShapeAmount", 1.35f));
    assert(host.parameters().write("freqShapePivotHz", 600.0f));
    const auto shapedEditorSnapshot = host.editorSnapshot();
    assert(shapedEditorSnapshot.spectrum.has_value());
    assert(shapedEditorSnapshot.spectrumOverlay.has_value());
    for (const auto* spectrum : { &*shapedEditorSnapshot.spectrum,
                                  &*shapedEditorSnapshot.spectrumOverlay })
    {
        assert(spectrum->frequencyShapeMode == ats::ui::FrequencyShapeMode::Harmonic);
        assert(std::abs(spectrum->frequencyShapeAmount - 1.35) < 1.0e-6);
        assert(std::abs(spectrum->frequencyShapePivotHz - 600.0) < 1.0e-6);
        assert(std::abs(spectrum->resolvedFrequencyShapePivotHz - 600.0) < 1.0e-6);
    }

    // Detection publishes both curves alongside the onset handles so the
    // spectrum overlay can render the actual adaptive threshold.
    assert(host.applyEditorAction(ats::ui::DetectSmearnovAction {}, error));
    assert(!error);
    assert(waitForSessionWork(host, error));
    assert(!error);
    const auto detectedEditorSnapshot = host.editorSnapshot();
    assert(detectedEditorSnapshot.spectrum.has_value());
    assert(!detectedEditorSnapshot.spectrum->smearnovDetectionCurve.empty());
    assert(detectedEditorSnapshot.spectrum->smearnovDetectionCurve.size()
           == detectedEditorSnapshot.spectrum->smearnovThresholdCurve.size());

    // A gain edit changes the trace values without changing the display ruler;
    // otherwise the edited content immediately renormalizes back into view.
    const auto beforeGain = host.editorSnapshot();
    assert(beforeGain.spectrum.has_value() && !beforeGain.spectrum->partials.empty());
    const auto gainPartial = beforeGain.spectrum->partials.front().partialIndex;
    const auto beforeReference = beforeGain.spectrum->ampDisplayReferencePeak;
    const auto beforeAmplitude = beforeGain.spectrum->partials.front().points.front().ampNorm;
    assert(host.applyEditorAction(ats::ui::SpectrumEditAction {
        ats::ui::SpectrumEditCommand::scalePartials, { gainPartial }, 0,
        static_cast<int>(beforeGain.spectrum->frameCount) - 1, 2.0f, {} }, error));
    assert(!error);
    const auto afterGain = host.editorSnapshot();
    assert(afterGain.spectrum.has_value());
    assert(std::abs(afterGain.spectrum->ampDisplayReferencePeak - beforeReference) < 1.0e-12);
    assert(afterGain.spectrum->partials.front().points.front().ampNorm > beforeAmplitude);

    assert(host.parameters().write("morphAmount", 0.5f));
    assert(host.applySessionAction(ats::ui::PreviewAction { 2, 0, true }, error));
    assert(!error);
    float morphLeft[512] {}, morphRight[512] {};
    float* morphChannels[] { morphLeft, morphRight };
    assert(host.process({ morphChannels, 2, 512 }, {}));
    bool morphAudible = false;
    for (const auto sample : morphLeft) morphAudible = morphAudible || std::abs(sample) > 1.0e-6f;
    assert(morphAudible);

    assert(host.applySessionAction(ats::ui::PreviewAction { 0, 0, true }, error));
    assert(!error);
    auto session = host.sessionSnapshot();
    assert(session.previewPlaying && session.previewSource == 0 && session.previewLayer == 0);

    float left[512] {}, right[512] {};
    float* channels[] { left, right };
    assert(host.process({ channels, 2, 512 }, {}));
    bool audible = false;
    for (const auto sample : left) audible = audible || std::abs(sample) > 1.0e-6f;
    assert(audible);
    assert(host.sessionSnapshot().normalizedPlayhead > 0.0f);

    assert(host.applySessionAction(ats::ui::SeekPreviewAction { 0.5f }, error));
    assert(!error && host.sessionSnapshot().normalizedPlayhead == 0.5f);
    assert(host.process({ channels, 2, 512 }, {}));
    assert(host.sessionSnapshot().normalizedPlayhead != 0.5f);
    assert(host.applySessionAction(ats::ui::SetMorphTimelineAction { { 0.0f, 0.35f, 1.0f } }, error));
    assert(!error && host.sessionSnapshot().timelineRevision == 1);
    assert(host.applySessionAction(ats::ui::SetTimbreMotionPlanAction { { 0.0f, 0.8f, 1.0f } }, error));
    assert(!error && host.sessionSnapshot().timbreRevision == 1);

    ats::ui::SetMorphTimelineAction typedTimeline;
    typedTimeline.points = {
        { 0.0f, 0.0f, 0, 0 }, { 0.5f, 0.65f, 0, 3 }, { 1.0f, 1.0f, 0, 0 },
        { 0.0f, 0.0f, 1, 0 }, { 0.5f, 0.8f, 1, 2 }, { 1.0f, 0.0f, 1, 0 },
        { 0.0f, 0.0f, 2, 0 }, { 0.5f, 0.7f, 2, 1 }, { 1.0f, 0.0f, 2, 0 }
    };
    assert(host.applySessionAction(typedTimeline, error));
    assert(!error && host.sessionSnapshot().timelineRevision == 2);

    ats::TimbreMotionPlan fullTimbre;
    ats::TimbreTargetCurve centroidCurve;
    centroidCurve.descriptorId = "combined.centroid_axis";
    centroidCurve.mode = ats::TimbreTargetMode::AbsoluteTarget;
    centroidCurve.points = { { 0.0, 0.2, 0 }, { 0.5, 0.8, 3 }, { 1.0, 0.4, 0 } };
    fullTimbre.targetCurves.push_back(std::move(centroidCurve));
    ats::ui::SetTimbreMotionPlanAction typedTimbre;
    typedTimbre.serializedPlan = ats::serializeTimbreMotionPlan(fullTimbre);
    assert(host.applySessionAction(typedTimbre, error));
    assert(!error && host.sessionSnapshot().timbreRevision == 2);

    assert(host.applySessionAction(ats::ui::PreviewAction { 2, 0, true }, error));
    assert(!error);
    const auto bouncePath = std::filesystem::temp_directory_path() / "ats-host-morph-bounce.wav";
    assert(host.applyEditorAction(ats::ui::BounceMorphTimelineAction { { bouncePath.u8string() }, 0 }, error));
    assert(!error && std::filesystem::file_size(bouncePath) > 44);
    assert(host.applySessionAction(ats::ui::SetPreviewSmearAction { true }, error));
    assert(!error && host.sessionSnapshot().liveSmearEnabled);
    assert(host.applySessionAction(ats::ui::PreviewAction { 0, 0, true }, error));
    assert(!error);
    assert(host.parameters().write("morphAmount", 0.73f));
    assert(host.applySessionAction(ats::ui::MorphVariationAction { 1, true }, error));
    assert(!error && host.sessionSnapshot().variationStored[1]);
    assert(host.parameters().write("morphAmount", 0.12f));
    assert(host.applySessionAction(ats::ui::MorphVariationAction { 1, false }, error));
    assert(!error && std::abs(host.parameters().read("morphAmount") - 0.73f) < 1.0e-6f);

    assert(host.applySessionAction(ats::ui::EditSmearnovOnsetsAction { { 0.25f } }, error));
    assert(!error);
    assert(host.parameters().write("smearnovBlurMs", 12.0f));
    assert(host.parameters().write("smearnovExaggerateDb", 3.0f));
    assert(host.applySessionAction(ats::ui::ApplySmearnovAction {}, error));
    assert(!error && host.sessionSnapshot().smearnovRunning);
    assert(waitForSessionWork(host, error));
    assert(!error && host.sessionSnapshot().smearedSourceAvailable);

    assert(host.applySessionAction(ats::ui::StopPreviewAction {}, error));
    assert(!error);
    std::fill(std::begin(left), std::end(left), 1.0f);
    std::fill(std::begin(right), std::end(right), 1.0f);
    assert(host.process({ channels, 2, 512 }, {}));
    for (const auto sample : left) assert(sample == 0.0f);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(pathB, ignored);
    std::filesystem::remove(bouncePath, ignored);
    return 0;
}

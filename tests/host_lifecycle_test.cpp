#include "../src/host/HostAdapter.h"
#include "../src/ui/HeadlessEditor.h"

#include <cassert>
#include <cstddef>
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

void writeMonoWave(const std::filesystem::path& path)
{
    constexpr std::int16_t samples[] = { 0, 3000, -3000, 0 };
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream);
    stream.write("RIFF", 4); writeU32(stream, 36 + sizeof(samples)); stream.write("WAVEfmt ", 8);
    writeU32(stream, 16); writeU16(stream, 1); writeU16(stream, 1); writeU32(stream, 44100);
    writeU32(stream, 88200); writeU16(stream, 2); writeU16(stream, 16);
    stream.write("data", 4); writeU32(stream, sizeof(samples));
    for (const auto sample : samples) writeU16(stream, static_cast<std::uint16_t>(sample));
    assert(stream);
}
}

int main()
{
    ats::host::HostAdapter host;
    assert(host.state() == ats::host::HostState::cold);
    assert(host.identity().productName == "ATS Resynth");
    assert(host.identity().company == "ATSPlugin");
    assert(host.identity().bundleId == "com.atsplugin.resynth");
    assert(host.identity().vst3ManufacturerCode == "AtPl");
    assert(host.identity().vst3ProductCode == "Atr1");
    assert(host.identity().latencySeconds == 0.0);
    assert(host.identity().tailSeconds == 0.0);
    assert(host.identity().midiInput && !host.identity().midiOutput);
    const auto parameterInfo = host.parameterInfo();
    assert(parameterInfo.size() == 81);
    assert(parameterInfo.front().hostNumericId == 1);
    assert(parameterInfo.front().id == "partGain");
    assert(host.setParameterNormalized(parameterInfo.front().hostNumericId, 0.5f));
    assert(host.parameters().read("partGain") > 0.0f);
    assert(host.setParameterText("partGain", "2"));
    assert(host.parameterText("partGain") == "2");
    assert(!host.setParameterText("missing", "1"));

    const auto stateHeader = ats::AtsStateCodec::encodeHeader({ 17 });
    ats::host::HostStateChunk stateChunk = stateHeader;
    stateChunk.push_back(static_cast<std::byte>(0x2a));
    assert(host.stateChunk().empty());
    assert(host.restoreState(stateChunk));
    assert(host.stateChunk() == stateChunk);
    const auto preservedState = host.stateChunk();
    auto truncatedState = stateChunk;
    truncatedState.resize(7);
    assert(!host.restoreState(truncatedState));
    assert(host.stateChunk() == preservedState);
    auto badVersionState = stateChunk;
    badVersionState[4] = static_cast<std::byte>(0xff);
    assert(!host.restoreState(badVersionState));
    assert(host.stateChunk() == preservedState);
    std::vector<std::byte> oversizedState(ats::host::HostAdapter::kMaximumStateChunkBytes + 1);
    assert(!host.restoreState(oversizedState));
    assert(host.stateChunk() == preservedState);
    host.clearState();
    assert(host.stateChunk().empty());

    assert(host.initialize(ats::host::HostFormat::vst3));
    assert(host.format() == ats::host::HostFormat::vst3);
    const auto wav = std::filesystem::temp_directory_path() / "ats-host-adapter-import.wav";
    writeMonoWave(wav);
    ats::Error importError;
    assert(host.importSource({ wav.u8string() }, 0, ats::engine::SourceImportKind::audio, importError));
    assert(!importError);
    assert(host.source(0) != nullptr && host.source(0)->monoSamples.size() == 4);
    assert(!host.importSource({ wav.u8string() }, 2, ats::engine::SourceImportKind::audio, importError));
    assert(importError.code == "invalid_slot");
    std::error_code filesystemError;
    std::filesystem::remove(wav, filesystemError);
    const bool invalidPrepare = host.prepare({ 0.0, 64, 2 });
    assert(!invalidPrepare);
    const bool inputBusPrepare = host.prepare({ 48000.0, 64, 2, 2 });
    assert(!inputBusPrepare);
    const bool prepared = host.prepare({ 48000.0, 64, 2 });
    assert(prepared);
    assert(host.busLayout().inputChannels == 0);
    assert(host.busLayout().outputChannels == 2);
    assert(host.attachEditor(2.0f, 1280.0f, 940.0f));
    assert(host.editorAttached() && host.editorScale() == 2.0f);
    assert(host.setEditorVisible(true));
    assert(host.setEditorLogicalSize(960.0f, 680.0f));
    assert(host.editorLogicalSize()[0] == 960.0f);

    // The headless object verifies the supplemental contract. The native
    // Visage probe separately proves that the same calls reach a real window.
    ats::ui::HeadlessEditor editor;
    assert(host.attachEditor(editor, reinterpret_cast<void*>(1), 1.25f, 640.0f, 480.0f));
    assert(host.editorAttached() && !editor.isShowing());
    assert(host.setEditorVisible(false) && !editor.isShowing());
    assert(host.setEditorVisible(true) && editor.isShowing());
    ats::ui::HeadlessEditor replacementEditor;
    assert(host.attachEditor(replacementEditor, reinterpret_cast<void*>(1), 1.0f, 800.0f, 600.0f));
    assert(!editor.isShowing());
    assert(host.editorAttached() && !replacementEditor.isShowing());
    assert(host.setEditorVisible(true) && replacementEditor.isShowing());
    assert(host.initialize(ats::host::HostFormat::au));
    assert(!host.editorAttached() && !host.editorVisible());
    assert(!editor.isShowing() && !replacementEditor.isShowing());
    assert(host.initialize(ats::host::HostFormat::vst3));
    assert(host.prepare({ 48000.0, 64, 2 }));
    host.detachEditor();
    assert(!editor.isShowing());

    assert(host.attachEditor(2.0f, 1280.0f, 940.0f));
    assert(host.setEditorVisible(true));
    assert(host.setEditorLogicalSize(960.0f, 680.0f));
    const auto slot = host.acquirePlaybackWriteSlot();
    assert(slot != ats::PlaybackBufferPool::kNoBuffer);
    auto write = host.playbackWriteView(slot);
    write.clear();
    write.channels[0][0] = 0.5f;
    write.channels[1][0] = -0.5f;
    const bool published = host.publishPlayback(slot, { 1 });
    assert(published);

    float left[4] {}, right[4] {};
    float* channels[] { left, right };
    const ats::MidiEvent events[] {{ 0, ats::MidiEvent::Type::noteOn, 72, 100 }};
    const bool processed = host.process({ channels, 2, 4 }, { events, 1 });
    assert(processed);
    assert(host.semitoneOffset() == 12);
    // Legacy processBlock only rendered while previewPlaying was set, so a
    // published buffer stays silent until preview starts. Audible playback
    // after PreviewAction is covered by host_analysis_playback_test.
    for (int frame = 0; frame < 4; ++frame)
        assert(left[frame] == 0.0f && right[frame] == 0.0f);
    host.release();
    assert(host.state() == ats::host::HostState::released);
    host.detachEditor();
    assert(!host.editorAttached() && !host.editorVisible());
    const bool processedAfterRelease = host.process({ channels, 2, 4 }, {});
    assert(!processedAfterRelease);
    return 0;
}

#include "../src/host/HostAdapter.h"
#include "../src/ui/HeadlessEditor.h"

#include <cassert>
#include <cstddef>
#include <vector>

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
    assert(host.initialize(ats::host::HostFormat::au));
    assert(!host.editorAttached() && !host.editorVisible() && !editor.isShowing());
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
    assert(left[0] > 0.49f && right[0] < -0.49f);
    assert(left[1] == 0.0f && right[1] == 0.0f);
    host.release();
    assert(host.state() == ats::host::HostState::released);
    host.detachEditor();
    assert(!host.editorAttached() && !host.editorVisible());
    const bool processedAfterRelease = host.process({ channels, 2, 4 }, {});
    assert(!processedAfterRelease);
    return 0;
}

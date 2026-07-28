#include "../src/host/HostAdapter.h"
#include "../src/ui/HeadlessEditor.h"

#include <cassert>

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
    assert(host.initialize(ats::host::HostFormat::vst3));
    assert(host.format() == ats::host::HostFormat::vst3);
    const bool invalidPrepare = host.prepare({ 0.0, 64, 2 });
    assert(!invalidPrepare);
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

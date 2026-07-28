#include "../src/host/HostAdapter.h"

#include <cassert>

int main()
{
    ats::host::HostAdapter host;
    assert(host.state() == ats::host::HostState::cold);
    const bool invalidPrepare = host.prepare({ 0.0, 64, 2 });
    assert(!invalidPrepare);
    const bool prepared = host.prepare({ 48000.0, 64, 2 });
    assert(prepared);
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
    const bool processedAfterRelease = host.process({ channels, 2, 4 }, {});
    assert(!processedAfterRelease);
    return 0;
}

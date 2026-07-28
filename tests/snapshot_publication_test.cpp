#include "../src/audio/AudioBufferPool.h"
#include "../src/engine/SnapshotPublisher.h"

#include <cassert>

int main()
{
    ats::engine::SnapshotPublisher publisher;
    ats::engine::PlaybackSnapshot first;
    first.modelRevision = 7;
    first.renderGeneration = 11;
    first.playheadFrame = 1234;
    first.peak = 0.75f;
    first.rms = 0.25f;
    first.playing = true;
    assert(publisher.publish(first));

    const auto* read = publisher.beginAudioRead();
    assert(read != nullptr && read->modelRevision == 7 && read->playheadFrame == 1234);
    publisher.endAudioRead();

    ats::engine::PlaybackSnapshot copy;
    assert(publisher.read(copy) && copy.renderGeneration == 11 && copy.playing);

    ats::audio::AudioBufferPool pool;
    pool.prepare(1, 8);
    assert(pool.tryAcquireWriteSlot() >= 0);
    return 0;
}

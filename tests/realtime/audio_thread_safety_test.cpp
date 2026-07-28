#include "../../src/audio/PlaybackTelemetryPublisher.h"

#include <atomic>
#include <cassert>
#include <thread>

int main()
{
    ats::audio::PlaybackTelemetryPublisher publisher;
    std::atomic<bool> readerValid { true };

    std::thread reader([&] {
        for (std::uint64_t i = 0; i < 50000; ++i)
        {
            ats::audio::PlaybackTelemetrySnapshot snapshot;
            if (!publisher.read(snapshot))
                continue;
            if (snapshot.meter.revision != snapshot.playhead.revision
                || snapshot.playback.renderGeneration != snapshot.playback.modelRevision + 1)
                readerValid.store(false, std::memory_order_release);
        }
    });

    for (std::uint64_t revision = 1; revision <= 10000; ++revision)
    {
        ats::engine::PlaybackSnapshot playback;
        playback.modelRevision = revision;
        playback.renderGeneration = revision + 1;
        playback.playheadFrame = revision;
        const bool published = publisher.publishAudioRt(
            playback, 0.5f, (revision & 1u) != 0, 0.5f, 0.25f, 128, revision, true);
        if (!published)
            std::this_thread::yield();
    }

    reader.join();
    assert(readerValid.load(std::memory_order_acquire));
    return 0;
}

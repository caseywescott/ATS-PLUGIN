#include "../src/audio/PlaybackTelemetryPublisher.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

int main()
{
    ats::audio::PlaybackTelemetryPublisher publisher;
    ats::engine::PlaybackSnapshot initial;
    initial.modelRevision = 1;
    initial.renderGeneration = 2;
    initial.playheadFrame = 10;
    initial.peak = 0.5f;
    initial.rms = 0.25f;
    initial.playing = true;
    assert(publisher.publishAudioRt(initial, 0.25f, true, 0.5f, 0.25f, 64, 1, true));

    ats::audio::PlaybackTelemetrySnapshot snapshot;
    assert(publisher.read(snapshot));
    assert(snapshot.playback.modelRevision == 1 && snapshot.playback.renderGeneration == 2);
    assert(snapshot.meter.frameCount == 64 && snapshot.meter.revision == 1);
    assert(snapshot.playhead.normalizedTime == 0.25f && snapshot.playhead.playing);

    std::atomic<bool> done { false };
    std::atomic<bool> valid { true };
    std::thread reader([&] {
        for (int i = 0; i < 100000; ++i)
        {
            ats::audio::PlaybackTelemetrySnapshot value;
            if (!publisher.read(value)) continue;
            if (value.playback.renderGeneration != value.playback.modelRevision + 1
                || value.playback.playheadFrame != value.playback.modelRevision * 10
                || value.meter.revision != value.playback.modelRevision
                || value.playhead.revision != value.playback.modelRevision)
                valid.store(false, std::memory_order_release);
        }
        done.store(true, std::memory_order_release);
    });
    std::uint64_t published = 0;
    for (std::uint64_t revision = 2; revision < 10000; ++revision)
    {
        ats::engine::PlaybackSnapshot value;
        value.modelRevision = revision;
        value.renderGeneration = revision + 1;
        value.playheadFrame = revision * 10;
        value.peak = 0.5f;
        value.rms = 0.25f;
        value.playing = (revision & 1u) != 0;
        if (publisher.publishAudioRt(value, 0.5f, value.playing, 0.5f, 0.25f, 128, revision, true))
            ++published;
        else
            std::this_thread::yield();
        if (done.load(std::memory_order_acquire)) break;
    }
    reader.join();
    assert(published > 100);
    assert(valid.load(std::memory_order_acquire));
    return 0;
}

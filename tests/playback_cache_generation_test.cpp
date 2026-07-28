#include "../src/audio/PlaybackCache.h"
#include "../src/engine/RenderInvalidationCoordinator.h"

#include <cassert>

int main()
{
    using Clock = ats::MonotonicClock;
    const auto start = Clock::now();
    ats::engine::RenderInvalidationCoordinator coordinator;
    const auto first = coordinator.request(ats::engine::InvalidationKind::frequencyShape, start);
    const auto second = coordinator.request(ats::engine::InvalidationKind::frequencyShape,
                                            start + std::chrono::milliseconds(20));
    assert(first != second && coordinator.pending(ats::engine::InvalidationKind::frequencyShape));
    assert(!coordinator.isCurrent(ats::engine::InvalidationKind::frequencyShape, first));
    assert(coordinator.poll(start + std::chrono::milliseconds(99)).empty());
    const auto ready = coordinator.poll(start + std::chrono::milliseconds(100));
    assert(ready.size() == 1 && ready[0].generation == second);
    assert(coordinator.isCurrent(ats::engine::InvalidationKind::frequencyShape, second));

    const auto morph = coordinator.request(ats::engine::InvalidationKind::morphDraft, start);
    assert(coordinator.poll(start + std::chrono::milliseconds(119)).empty());
    assert(coordinator.poll(start + std::chrono::milliseconds(120)).size() == 1);
    assert(coordinator.isCurrent(ats::engine::InvalidationKind::morphDraft, morph));

    ats::audio::PlaybackCache cache;
    cache.prepare(1, 4);
    float samples[] { 1, 2, 3, 4 };
    float* channels[] { samples };
    const ats::audio::PlaybackCacheKey key { 1, 2, 3, 4, 48000, 4, 0, true, false };
    assert(cache.publish(key, { channels, 1, 4 }, second));
    const auto* hit = cache.lookup(key);
    assert(hit != nullptr && hit->channel(0)[2] == 3.0f && cache.generation(key) == second);
    cache.invalidate();
    assert(cache.lookup(key) == nullptr);
    return 0;
}

#include "../src/engine/PlaybackEngine.h"

#include <cassert>
#include <cstdlib>
#include <new>

// Flag set while process() is on the call stack. Any operator new fired here
// is an RT safety violation; we abort immediately so the test fails loudly.
static bool g_in_process = false;

void* operator new(std::size_t size)
{
    if (g_in_process)
        std::abort();
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}

void* operator new[](std::size_t size)
{
    if (g_in_process)
        std::abort();
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc{};
    return p;
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

int main()
{
    ats::engine::PlaybackEngine engine;
    assert(engine.prepare({ 48000.0, 512, 2 }));

    ats::ParameterStore parameters;
    assert(parameters.write("partGain", 0.5f));
    assert(parameters.write("resGain", 0.75f));
    assert(parameters.write("outputTrim", 1.0f));

    float left[512] {}, right[512] {};
    float* channels[] { left, right };

    // Publish a combined buffer so process() has data to mix.
    const auto slot = engine.tryAcquireWriteSlot(ats::engine::PlaybackLayer::combined);
    assert(slot != ats::PlaybackBufferPool::kNoBuffer);
    auto view = engine.writeView(ats::engine::PlaybackLayer::combined, slot);
    view.clear();
    view.channels[0][0] = 1.0f;
    view.channels[1][0] = 1.0f;
    assert(engine.publish(ats::engine::PlaybackLayer::combined, slot, { 1 }));

    g_in_process = true;
    const bool ok = engine.process({ channels, 2, 512 }, {}, parameters);
    g_in_process = false;

    assert(ok);
    // outputTrim=1 applied to combined buffer sample 0 → 1.0 ramped from reset(1).
    assert(left[0] > 0.0f);
    assert(right[0] > 0.0f);

    return 0;
}

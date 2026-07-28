#include "../../src/engine/PlaybackEngine.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::engine::PlaybackEngine engine;
    assert(engine.prepare({ 48000.0, 8, 2 }));

    const auto slot = engine.tryAcquireWriteSlot(ats::engine::PlaybackLayer::combined);
    assert(slot != ats::PlaybackBufferPool::kNoBuffer);
    auto buffer = engine.writeView(ats::engine::PlaybackLayer::combined, slot);
    buffer.clear();
    const float leftGolden[] { 0.25f, 0.5f, -0.25f, -0.5f, 0.0f, 0.125f, -0.125f, 0.0f };
    const float rightGolden[] { -0.25f, 0.25f, 0.5f, -0.5f, 0.125f, 0.0f, 0.0f, -0.125f };
    for (int frame = 0; frame < 8; ++frame)
    {
        buffer.channels[0][frame] = leftGolden[frame];
        buffer.channels[1][frame] = rightGolden[frame];
    }
    assert(engine.publish(ats::engine::PlaybackLayer::combined, slot, { 7 }));

    ats::ParameterStore parameters;
    assert(parameters.write("partGain", 1.0f));
    assert(parameters.write("resGain", 1.0f));
    assert(parameters.write("outputTrim", 1.0f));
    float left[8] {}, right[8] {};
    float* channels[] { left, right };
    assert(engine.process({ channels, 2, 8 }, {}, parameters));
    for (int frame = 0; frame < 8; ++frame)
    {
        assert(std::abs(left[frame] - leftGolden[frame]) < 1.0e-5f);
        assert(std::abs(right[frame] - rightGolden[frame]) < 1.0e-5f);
        assert(std::isfinite(left[frame]) && std::isfinite(right[frame]));
    }
    engine.release();
    return 0;
}

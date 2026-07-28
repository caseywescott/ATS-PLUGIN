#include "../src/engine/PlaybackEngine.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::engine::PlaybackEngine engine;
    assert(engine.prepare({ 48000.0, 8, 2 }));

    const auto partialSlot = engine.tryAcquireWriteSlot(ats::engine::PlaybackLayer::partials);
    assert(partialSlot != ats::PlaybackBufferPool::kNoBuffer);
    auto partial = engine.writeView(ats::engine::PlaybackLayer::partials, partialSlot);
    partial.clear();
    partial.channels[0][0] = 1.0f;
    partial.channels[1][0] = -1.0f;
    assert(engine.publish(ats::engine::PlaybackLayer::partials, partialSlot, { 1 }));

    const auto combinedSlot = engine.tryAcquireWriteSlot(ats::engine::PlaybackLayer::combined);
    auto combined = engine.writeView(ats::engine::PlaybackLayer::combined, combinedSlot);
    combined.clear();
    combined.channels[0][0] = 2.0f;
    combined.channels[1][0] = 2.0f;
    assert(engine.publish(ats::engine::PlaybackLayer::combined, combinedSlot, { 2 }));
    assert(engine.activeGeneration(ats::engine::PlaybackLayer::combined).value == 2);

    ats::ParameterStore parameters;
    assert(parameters.write("partGain", 1.0f));
    assert(parameters.write("resGain", 1.0f));
    assert(parameters.write("outputTrim", 1.0f));
    float left[8] {}, right[8] {};
    float* channels[] { left, right };

    assert(engine.process({ channels, 2, 8 }, {}, parameters));
    assert(std::fabs(left[0] - 2.0f) < 1.0e-5f);
    assert(std::fabs(right[0] - 2.0f) < 1.0e-5f);

    engine.setLayerMix(true);
    assert(engine.process({ channels, 2, 8 }, {}, parameters));
    assert(std::fabs(left[0] - 1.0f) < 1.0e-5f);
    assert(std::fabs(right[0] + 1.0f) < 1.0e-5f);

    engine.setPreviewMode(ats::engine::PreviewMode::partialsOnly);
    assert(engine.process({ channels, 2, 8 }, {}, parameters));
    assert(std::fabs(left[0] - 1.0f) < 1.0e-5f);
    assert(engine.semitoneOffset() == 0);
    engine.release();
    assert(!engine.process({ channels, 2, 8 }, {}, parameters));
    return 0;
}

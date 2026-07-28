#include "PlaybackBuffers.h"

#include <cassert>
#include <iostream>

int main()
{
    ats::PlaybackBufferPool pool;
    pool.prepare(2, 4);
    const auto firstRead = pool.beginAudioRead();
    assert(firstRead == 0);
    const auto writer = pool.tryAcquireWriteSlot();
    assert(writer == 1);
    auto output = pool.writeView(writer);
    assert(output.valid());
    output.channels[0][0] = 0.25f;
    output.channels[1][0] = -0.25f;
    assert(pool.publish(writer, { 7 }));
    const auto staleWriter = pool.tryAcquireWriteSlot();
    assert(staleWriter != ats::PlaybackBufferPool::kNoBuffer);
    assert(!pool.publish(staleWriter, { 6 }));
    // The reader holds the old buffer until the callback releases it.
    assert(pool.readBuffer(firstRead)->channel(0)[0] == 0.0f);
    pool.endAudioRead(firstRead);
    const auto secondRead = pool.beginAudioRead();
    assert(secondRead == writer);
    assert(pool.readBuffer(secondRead)->channel(0)[0] == 0.25f);
    assert(pool.generation(secondRead).value == 7);
    assert(pool.activeGeneration().value == 7);
    pool.endAudioRead(secondRead);

    ats::GenerationGate gate;
    gate.publish({ 9 });
    assert(gate.isCurrent({ 9 }));
    assert(!gate.isCurrent({ 8 }));
    std::cout << "playback_buffer_test: publication and stale gate ok\n";
}

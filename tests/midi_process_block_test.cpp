#include "../src/engine/PlaybackEngine.h"
#include "../src/host/HostAdapter.h"

#include <cassert>

int main()
{
    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::vst3));
    assert(host.prepare({ 48000.0, 8, 2, 0 }));

    float left[8] {}, right[8] {};
    float* channels[] { left, right };
    const ats::MidiEvent noteOn { 3, ats::MidiEvent::Type::noteOn, 67, 100 };
    const ats::MidiEvent noteOff { 7, ats::MidiEvent::Type::noteOff, 67, 0 };
    const ats::MidiEvent events[] { noteOn, noteOff };
    const ats::MidiEventList list { events, 2 };
    assert(list.begin()->sampleOffset == 3);
    assert((list.end() - 1)->sampleOffset == 7);
    assert(host.process({ channels, 2, 8 }, list));
    assert(host.semitoneOffset() == 0);

    const ats::MidiEvent zeroVelocity { 0, ats::MidiEvent::Type::noteOn, 72, 0 };
    assert(host.process({ channels, 2, 8 }, { &zeroVelocity, 1 }));
    assert(host.semitoneOffset() == 0);

    host.release();
    return 0;
}

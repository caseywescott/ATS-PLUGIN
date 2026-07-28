#include "MidiNoteRouter.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    ats::MidiNoteRouter router;
    router.consume({ 13, ats::MidiEvent::Type::noteOn, 72, 100 });
    assert(router.semitoneOffset() == 12);
    assert(std::fabs(router.frequencyFactor(1.5) - 3.0) < 1.0e-12);
    router.consume({ 19, ats::MidiEvent::Type::noteOn, 72, 0 });
    assert(router.semitoneOffset() == 0);
    router.consume({ 0, ats::MidiEvent::Type::noteOn, 48, 100 });
    assert(router.semitoneOffset() == -12);
    router.consume({ 127, ats::MidiEvent::Type::noteOff, 48, 0 });
    assert(router.semitoneOffset() == 0);
    router.consume({ 0, ats::MidiEvent::Type::allNotesOff, 0, 0 });
    assert(router.semitoneOffset() == 0);
    std::cout << "midi_note_router_test: offset and sample-offset contract ok\n";
}

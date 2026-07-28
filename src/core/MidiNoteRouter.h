#pragma once

#include "AtsPrimitives.h"

#include <cmath>

namespace ats
{

class MidiNoteRouter
{
public:
    void reset() noexcept { semitoneOffset_ = 0; }

    void consume(const MidiEvent& event) noexcept
    {
        const bool zeroVelocityNoteOn = event.type == MidiEvent::Type::noteOn && event.velocity == 0;
        if (event.type == MidiEvent::Type::allNotesOff || event.type == MidiEvent::Type::noteOff || zeroVelocityNoteOn)
        {
            semitoneOffset_ = 0;
            return;
        }
        if (event.type == MidiEvent::Type::noteOn && event.velocity > 0)
            semitoneOffset_ = static_cast<int>(event.note) - 60;
    }

    [[nodiscard]] int semitoneOffset() const noexcept { return semitoneOffset_; }
    [[nodiscard]] double frequencyFactor(double baseFactor) const noexcept
    { return baseFactor * std::pow(2.0, static_cast<double>(semitoneOffset_) / 12.0); }

private:
    int semitoneOffset_ = 0;
};

} // namespace ats

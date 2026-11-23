#include "AtsTransposeCharacter.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
// 10 harmonics of 150 Hz shaped by a formant bump at 600 Hz: amp = exp(-((f-600)/150)^2) + 0.1.
ats::AtsSoundPtr makeFormant(int frames = 4)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, frames, frames * 0.01, 10, 1);
    for (int f = 0; f < frames; ++f)
    {
        s->time[0][f] = f * 0.01;
        for (int p = 0; p < 10; ++p)
        {
            const auto frq = 150.0 * (p + 1);
            s->frq[p][f] = frq;
            s->amp[p][f] = std::exp(-std::pow((frq - 600.0) / 150.0, 2.0)) + 0.1;
            s->pha[p][f] = 1.0;
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = 0.001 * (b + 1);
    }
    return ats::AtsSoundPtr(s);
}

// Two partials at 250 and 4000 Hz with equal amplitude.
ats::AtsSoundPtr makePair()
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, 2, 0.02, 2, 0);
    for (int f = 0; f < 2; ++f)
    {
        s->time[0][f] = f * 0.01;
        s->frq[0][f] = 250.0; s->amp[0][f] = 0.5;
        s->frq[1][f] = 4000.0; s->amp[1][f] = 0.5;
    }
    return ats::AtsSoundPtr(s);
}

int loudest(const ATS_SOUND* s, int frame)
{
    int best = 0;
    for (int p = 1; p < s->partials; ++p)
        if (s->amp[p][frame] > s->amp[best][frame])
            best = p;
    return best;
}
double db(double a) { return 20.0 * std::log10(a); }
}

int main()
{
    const auto src = makeFormant();
    const auto* s = src.get();
    assert(loudest(s, 1) == 3); // 600 Hz is the peak of the source

    ats::TransposeCharacterParams up;
    up.frequencyFactor = 2.0;
    up.preserveEnvelope = true;
    assert(up.active());
    const auto kept = ats::applyTransposeCharacter(s, up);
    assert(kept);
    const auto* k = kept.get();
    assert(k->partials == 10 && k->frames == 4);
    for (int p = 0; p < 10; ++p)
    {
        assert(std::abs(k->frq[p][1] - 2.0 * s->frq[p][1]) < 1.0e-9);
        assert(k->pha[p][1] == 0.0);
    }
    // With envelope preservation the loudest partial is the one now sitting at 600 Hz (partial 1: 300 -> 600).
    assert(loudest(k, 1) == 1);
    assert(k->amp[1][1] > k->amp[3][1]);
    assert(k->has_slopes == 0);
    assert(std::abs(k->band_energy[4][2] - s->band_energy[4][2]) < 1.0e-12);

    // Without it the peak simply moves to 1200 Hz (partial 3).
    ats::TransposeCharacterParams plain = up;
    plain.preserveEnvelope = false;
    plain.ceilingHz = 10000.0;
    const auto moved = ats::applyTransposeCharacter(s, plain);
    assert(moved && loudest(moved.get(), 1) == 3);
    for (int p = 0; p < 10; ++p)
        assert(std::abs(moved.get()->amp[p][1] - s->amp[p][1]) < 1.0e-12);

    // Tilt: -3 dB/oct per octave of transposition, factor 2 -> -3 dB/oct about the pivot.
    const auto pair = makePair();
    ats::TransposeCharacterParams tilt;
    tilt.frequencyFactor = 2.0;
    tilt.preserveEnvelope = false;
    tilt.tiltDbPerOctavePerOctave = -3.0;
    tilt.ceilingHz = 20000.0;
    const auto tilted = ats::applyTransposeCharacter(pair.get(), tilt);
    assert(tilted);
    const auto diff = db(tilted.get()->amp[1][0]) - db(tilted.get()->amp[0][0]);
    const auto expected = -3.0 * std::log2(4000.0 / 250.0); // -12 dB
    assert(std::abs(diff - expected) < 0.5);
    // Partial at the pivot after transposition (500 -> 1000) would be unchanged; check the sign at 8 kHz.
    assert(tilted.get()->amp[1][0] < pair.get()->amp[1][0]);

    // Ceiling: 4000 -> 8000 Hz is more than 0.25 octave above a 6 kHz ceiling -> silent.
    ats::TransposeCharacterParams ceil;
    ceil.frequencyFactor = 2.0;
    ceil.preserveEnvelope = false;
    ceil.ceilingHz = 6000.0;
    const auto capped = ats::applyTransposeCharacter(pair.get(), ceil);
    assert(capped && capped.get()->amp[1][0] == 0.0 && capped.get()->amp[0][0] == 0.5);
    // Half way through the fade: 8000 Hz vs ceiling 8000 / 2^0.125.
    ceil.ceilingHz = 8000.0 / std::pow(2.0, 0.125);
    const auto half = ats::applyTransposeCharacter(pair.get(), ceil);
    assert(half && std::abs(half.get()->amp[1][0] - 0.25) < 1.0e-6);

    // Factor 1 -> nullptr; factor 2 with nothing to do -> nullptr.
    ats::TransposeCharacterParams identity;
    assert(!identity.active() && !ats::applyTransposeCharacter(s, identity));
    ats::TransposeCharacterParams nothing;
    nothing.frequencyFactor = 2.0;
    nothing.preserveEnvelope = false;
    assert(!nothing.active() && !ats::applyTransposeCharacter(s, nothing));
    assert(!ats::applyTransposeCharacter(nullptr, up));

    std::cout << "ats_transpose_character_test: ok\n";
    return 0;
}

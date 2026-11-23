#include "AtsSpectralSmoothness.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
// 8 harmonics of 200 Hz, amp 1/h, with partial index `boost` (0-based) raised by boostDb.
ats::AtsSoundPtr make(int boost, double boostDb, int frames = 3)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, frames, frames * 0.01, 8, 0);
    for (int f = 0; f < frames; ++f)
    {
        s->time[0][f] = f * 0.01;
        for (int p = 0; p < 8; ++p)
        {
            s->frq[p][f] = 200.0 * (p + 1);
            s->amp[p][f] = 1.0 / (p + 1);
            if (p == boost)
                s->amp[p][f] *= std::pow(10.0, boostDb / 20.0);
        }
    }
    return ats::AtsSoundPtr(s);
}
double db(double a) { return 20.0 * std::log10(a); }
}

int main()
{
    const auto src = make(4, 20.0); // harmonic 5 boosted 20 dB
    ats::SpectralSmoothnessParams params;
    const auto out = ats::applySpectralSmoothness(src.get(), params);
    assert(out);
    const auto* o = out.get();
    assert(o->partials == 8 && o->frames == 3);
    for (int f = 0; f < 3; ++f)
    {
        // The boosted harmonic is pulled back to within 3 dB of the 1/h line.
        assert(std::abs(db(o->amp[4][f]) - db(1.0 / 5.0)) < 3.0);
        // Everything else changes by less than 0.5 dB.
        for (int p = 0; p < 8; ++p)
        {
            if (p == 4) continue;
            assert(std::abs(db(o->amp[p][f]) - db(src.get()->amp[p][f])) < 0.5);
            assert(o->frq[p][f] == src.get()->frq[p][f]);
        }
    }

    // A clean 1/h series is a fixed point.
    const auto clean = make(-1, 0.0);
    const auto same = ats::applySpectralSmoothness(clean.get(), params);
    assert(same);
    for (int p = 0; p < 8; ++p)
        assert(std::abs(db(same.get()->amp[p][1]) - db(clean.get()->amp[p][1])) < 1.0e-6);

    // Strength 0 -> nullptr; half strength halves the dB correction (approximately, in linear terms).
    params.strength = 0.0;
    assert(!ats::applySpectralSmoothness(src.get(), params));
    params.strength = 0.5;
    const auto half = ats::applySpectralSmoothness(src.get(), params);
    assert(half && half.get()->amp[4][1] > o->amp[4][1] && half.get()->amp[4][1] < src.get()->amp[4][1]);

    // maxReductionDb caps the change.
    params.strength = 1.0;
    params.maxReductionDb = 6.0;
    const auto capped = ats::applySpectralSmoothness(src.get(), params);
    assert(capped);
    assert(std::abs(db(src.get()->amp[4][1]) - db(capped.get()->amp[4][1]) - 6.0) < 1.0e-6);

    // Explicit fundamental gives the same answer as the estimate.
    params.maxReductionDb = 24.0;
    params.fundamentalHz = 200.0;
    const auto explicitF0 = ats::applySpectralSmoothness(src.get(), params);
    assert(explicitF0 && std::abs(explicitF0.get()->amp[4][1] - o->amp[4][1]) < 1.0e-12);

    assert(!ats::applySpectralSmoothness(nullptr, params).get());
    std::cout << "ats_spectral_smoothness_test: ok\n";
    return 0;
}

#include "AtsSubharmonics.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
// 5 harmonics of 220 Hz, amp 1/h, 20 frames, with residual bands.
ats::AtsSoundPtr make(int frames = 20)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, frames, frames * 0.01, 5, 1);
    for (int f = 0; f < frames; ++f)
    {
        s->time[0][f] = f * 0.01;
        for (int p = 0; p < 5; ++p)
        {
            s->frq[p][f] = 220.0 * (p + 1);
            s->amp[p][f] = 1.0 / (p + 1);
            s->pha[p][f] = 0.5;
            s->smr[p][f] = 30.0;
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = 0.001 * (b + 1) + f * 1.0e-5;
    }
    return ats::AtsSoundPtr(s);
}
double db(double a) { return 20.0 * std::log10(a); }
}

int main()
{
    const auto src = make();
    const auto* s = src.get();

    ats::SubharmonicParams rough;
    const auto r = ats::applySubharmonics(s, rough);
    assert(r.sound && r.addedPartials == 5);
    const auto* o = r.sound.get();
    assert(o->partials == 10 && o->frames == 20 && o->srate == 44100 && o->has_slopes == 0);
    assert(std::abs(o->dur - s->dur) < 1.0e-12);
    for (int f = 0; f < 20; ++f)
    {
        assert(o->time[0][f] == s->time[0][f]);
        for (int p = 0; p < 5; ++p)
        {
            // Originals untouched.
            assert(o->frq[p][f] == s->frq[p][f] && o->amp[p][f] == s->amp[p][f]);
            assert(o->pha[p][f] == s->pha[p][f] && o->smr[p][f] == s->smr[p][f]);
            // Copies at f / 2 within +/- 3 %, -6 dB +/- 3 dB.
            const auto q = o->frq[5 + p][f];
            const auto a = o->amp[5 + p][f];
            const auto target = 110.0 * (p + 1);
            assert(std::abs(q - target) <= 0.03 * target + 1.0e-9);
            assert(std::abs(db(a) - (db(s->amp[p][f]) - 6.0)) <= 3.0 + 1.0e-9);
            assert(o->pha[5 + p][f] == 0.0);
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            assert(o->band_energy[b][f] == s->band_energy[b][f]);
    }
    // The jitter actually moves things, and is deterministic.
    bool moved = false;
    for (int f = 0; f < 20 && !moved; ++f)
        moved = std::abs(o->frq[5][f] - 110.0) > 1.0e-6;
    assert(moved);
    const auto again = ats::applySubharmonics(s, rough);
    for (int f = 0; f < 20; ++f)
        for (int p = 5; p < 10; ++p)
            assert(again.sound.get()->frq[p][f] == o->frq[p][f] && again.sound.get()->amp[p][f] == o->amp[p][f]);
    ats::SubharmonicParams otherSeed = rough;
    otherSeed.seed = 99;
    const auto different = ats::applySubharmonics(s, otherSeed);
    bool differs = false;
    for (int f = 0; f < 20 && !differs; ++f)
        differs = different.sound.get()->frq[5][f] != o->frq[5][f];
    assert(differs);
    // av filled for the new partials.
    assert(o->av != nullptr && std::abs(o->av[5].frq - 110.0) < 0.03 * 110.0);

    // No jitter -> exact halves at exactly -6 dB.
    ats::SubharmonicParams exact = rough;
    exact.periodJitterPercent = 0.0;
    exact.ampJitterDb = 0.0;
    const auto e = ats::applySubharmonics(s, exact);
    assert(e.sound && std::abs(e.sound.get()->frq[7][3] - 330.0) < 1.0e-9);
    assert(std::abs(db(e.sound.get()->amp[7][3]) - (db(1.0 / 3.0) - 6.0)) < 1.0e-9);

    // maxHz limits which partials are copied; divisor 3 -> f / 3.
    ats::SubharmonicParams limited = exact;
    limited.maxHz = 500.0;
    limited.divisor = 3;
    const auto l = ats::applySubharmonics(s, limited);
    assert(l.sound && l.addedPartials == 2);
    assert(std::abs(l.sound.get()->frq[5][0] - 220.0 / 3.0) < 1.0e-9);

    // Growl order 4: 3 partials per harmonic gap, 4 gaps.
    ats::SubharmonicParams growl;
    growl.mode = ats::SubharmonicMode::growl;
    growl.growlOrder = 4;
    const auto g = ats::applySubharmonics(s, growl);
    assert(g.sound && g.addedPartials == 12);
    const auto* go = g.sound.get();
    assert(go->partials == 17);
    for (int p = 0; p < 5; ++p)
        assert(go->frq[p][4] == s->frq[p][4] && go->amp[p][4] == s->amp[p][4]);
    for (int m = 1; m < 5; ++m)
        for (int k = 1; k < 4; ++k)
        {
            const auto idx = 5 + (m - 1) * 3 + (k - 1);
            const auto expectedF = 220.0 * (m + k / 4.0);
            assert(std::abs(go->frq[idx][4] - expectedF) < 1.0e-9);
            const auto lo = db(1.0 / m), hi = db(1.0 / (m + 1));
            const auto expectedDb = lo + (k / 4.0) * (hi - lo) - 6.0;
            assert(std::abs(db(go->amp[idx][4]) - expectedDb) < 1.0e-9);
        }
    for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        assert(go->band_energy[b][7] == s->band_energy[b][7]);

    // Empty input.
    assert(!ats::applySubharmonics(nullptr, rough).sound);

    std::cout << "ats_subharmonics_test: ok\n";
    return 0;
}

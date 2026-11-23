#include "AtsVibrato.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
constexpr int kPartials = 6;
constexpr int kFrames = 400; // 4 s at 100 frames/s
constexpr double kF0 = 220.0;
constexpr double kRate = 5.5;
constexpr double kDepthCents = 40.0;

/** Harmonic model with sinusoidal vibrato of +-depthCents at kRate Hz plus 20% amplitude ripple. */
ats::AtsSoundPtr makeModel(double depthCents)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, kFrames, kFrames * 0.01, kPartials, 1);
    for (int f = 0; f < kFrames; ++f)
    {
        const double t = f * 0.01;
        s->time[0][f] = t;
        const double cents = depthCents * std::sin(2.0 * 3.141592653589793 * kRate * t);
        const double f0 = kF0 * std::pow(2.0, cents / 1200.0);
        const double ripple = depthCents > 0.0 ? 1.0 + 0.2 * std::sin(2.0 * 3.141592653589793 * kRate * t + 0.7) : 1.0;
        for (int p = 0; p < kPartials; ++p)
        {
            s->amp[p][f] = ripple * 0.5 / (p + 1);
            s->frq[p][f] = f0 * (p + 1);
            s->pha[p][f] = 0.1;
            s->smr[p][f] = s->res[p][f] = 0.0;
            s->frq_slope[p][f] = s->amp_slope[p][f] = 0.0;
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = 0.0;
    }
    s->has_slopes = 1;
    return ats::AtsSoundPtr(s);
}

void print(const char* label, const ats::VibratoAnalysis& a)
{
    std::cout << label << ": detected " << a.detected << ", rate " << a.rateHz << " Hz, depth " << a.depthCents
              << " cents, extent " << a.extentCents << " cents, prominence " << a.prominenceDb << " dB, f0 "
              << a.fundamentalHz << " Hz\n";
}
}

int main()
{
    const auto model = makeModel(kDepthCents);
    const auto* s = model.get();

    const auto a = ats::analyseVibrato(s);
    print("vibrato", a);
    assert(a.detected);
    assert(std::abs(a.rateHz - kRate) < 0.3);
    assert(std::abs(a.depthCents - kDepthCents) < 8.0);
    assert(std::abs(a.extentCents - 2.0 * kDepthCents) < 12.0);
    assert(std::abs(a.fundamentalHz - kF0) < 0.03 * kF0);
    assert(a.pitchCents.size() == static_cast<std::size_t>(kFrames) && a.carrierCents.size() == a.pitchCents.size());

    // Kaminskyj's stricter band still accepts 5.5 Hz; a 7-9 Hz band rejects it.
    {
        ats::VibratoAnalysisParams strict;
        strict.minRateHz = 5.0;
        strict.maxRateHz = 8.0;
        assert(ats::analyseVibrato(s, strict).detected);
        ats::VibratoAnalysisParams off;
        off.minRateHz = 7.0;
        off.maxRateHz = 9.0;
        assert(!ats::analyseVibrato(s, off).detected);
    }

    // A steady model is not detected.
    const auto steady = makeModel(0.0);
    const auto none = ats::analyseVibrato(steady.get());
    print("steady", none);
    assert(!none.detected && none.extentCents < 1.0);
    assert(!ats::analyseVibrato(nullptr).detected);

    // Inactive params -> nullptr.
    ats::VibratoParams identity;
    assert(!identity.active());
    assert(!ats::applyVibrato(s, identity));

    // depth 0 removes the vibrato (and the amplitude ripple).
    {
        ats::VibratoParams p;
        p.depth = 0.0;
        assert(p.active());
        const auto flat = ats::applyVibrato(s, p);
        assert(flat);
        const auto r = ats::analyseVibrato(flat.get());
        print("depth 0", r);
        assert(r.extentCents < 8.0);
        assert(!r.detected);
        assert(flat.get()->has_slopes == 0);
        double ampMin = 1.0e9, ampMax = 0.0;
        for (int f = 20; f < kFrames - 20; ++f)
        {
            ampMin = std::min(ampMin, flat.get()->amp[0][f]);
            ampMax = std::max(ampMax, flat.get()->amp[0][f]);
            assert(flat.get()->pha[0][f] == 0.0);
        }
        std::cout << "depth 0 amp range of partial 0: " << ampMin << " .. " << ampMax << "\n";
        assert((ampMax - ampMin) / ampMax < 0.1); // ripple of 20% removed

        // Keeping the amplitude modulation leaves the ripple in place.
        p.scaleAmplitudeModulation = false;
        const auto keepAmp = ats::applyVibrato(s, p);
        for (int f = 0; f < kFrames; ++f)
            assert(keepAmp.get()->amp[0][f] == s->amp[0][f]);
    }

    // depth 2 doubles the extent.
    {
        ats::VibratoParams p;
        p.depth = 2.0;
        const auto wide = ats::applyVibrato(s, p);
        const auto r = ats::analyseVibrato(wide.get());
        print("depth 2", r);
        assert(r.detected);
        assert(std::abs(r.extentCents - 4.0 * kDepthCents) < 25.0);
        assert(std::abs(r.rateHz - kRate) < 0.3);
    }

    // rateScale 2 doubles the rate.
    {
        ats::VibratoParams p;
        p.rateScale = 2.0;
        const auto fast = ats::applyVibrato(s, p);
        const auto r = ats::analyseVibrato(fast.get());
        print("rateScale 2", r);
        assert(r.detected);
        assert(std::abs(r.rateHz - 2.0 * kRate) < 1.0);
        assert(std::abs(r.extentCents - 2.0 * kDepthCents) < 20.0);
    }

    // Deterministic.
    {
        ats::VibratoParams p;
        p.depth = 1.5;
        p.rateScale = 0.75;
        const auto x = ats::applyVibrato(s, p), y = ats::applyVibrato(s, p);
        for (int q = 0; q < kPartials; ++q)
            for (int f = 0; f < kFrames; ++f)
                assert(x.get()->frq[q][f] == y.get()->frq[q][f] && x.get()->amp[q][f] == y.get()->amp[q][f]);
        const auto r = ats::analyseVibrato(x.get());
        print("depth 1.5 / rateScale 0.75", r);
        assert(std::abs(r.rateHz - 0.75 * kRate) < 0.5);
    }

    std::cout << "ats_vibrato_test: ok\n";
    return 0;
}

#include "AtsResidualFixes.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr int kRate = 44100;
constexpr int kHop = 441;
constexpr int kWindow = 2048;
constexpr int kFrames = 100;

// Deterministic uniform noise in [-a, a) (splitmix64); variance a^2 / 3.
double noiseAt(std::uint64_t i)
{
    std::uint64_t z = (i + 0x9E3779B97F4A7C15ULL) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 30)) * 0x94D049BB133111EBULL;
    z = (z ^ (z >> 27)) * 0x2545F4914F6CDD1DULL;
    z ^= z >> 31;
    return (double) (z >> 11) / 9007199254740992.0 * 2.0 - 1.0;
}

// Expected ATSA band_energy for white noise of variance var: M * var * bandwidth / sr.
double expectedBandEnergy(int band, double var)
{
    const double bw = ATSA_CRITICAL_BAND_EDGES[band + 1] - ATSA_CRITICAL_BAND_EDGES[band];
    return kWindow * var * bw / kRate;
}

ats::AtsSoundPtr makeModel(int partials = 2)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, kRate, kHop, kWindow, kFrames, kFrames * (double) kHop / kRate, partials, 1);
    for (int f = 0; f < kFrames; ++f)
    {
        s->time[0][f] = (double) f * kHop / kRate;
        for (int p = 0; p < partials; ++p)
        {
            s->amp[p][f] = 0.01 * (f + 1) / (p + 1);
            s->frq[p][f] = 220.0 * (p + 1) + f;
            s->pha[p][f] = 0.1 * f;
            s->smr[p][f] = 3.0 + p;
            s->res[p][f] = 1.0e-3 * f;
            s->frq_slope[p][f] = 0.5 * f - p;
            s->amp_slope[p][f] = -0.25 * f + p;
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = 1.0e-3 * (b + 1) * (1.0 + 0.01 * f);
    }
    s->has_slopes = 1;
    return ats::AtsSoundPtr(s);
}
} // namespace

int main()
{
    // (1) clamp against a white-noise source of known level.
    {
        const double a = 0.5;
        const double var = a * a / 3.0;
        std::vector<double> src(kRate);
        for (int n = 0; n < kRate; ++n)
            src[(size_t) n] = a * noiseAt((std::uint64_t) n);

        auto model = makeModel();
        ATS_SOUND* s = model.get();
        const double exp5 = expectedBandEnergy(5, var);   // 510-630 Hz, 10x too hot
        const double exp20 = expectedBandEnergy(20, var); // 6400-7700 Hz, 0.5x -> untouched
        for (int f = 0; f < kFrames; ++f)
        {
            // Filler well under the per-frame source minimum (~0.1x in the 9-bin bands).
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                s->band_energy[b][f] = 0.05 * expectedBandEnergy(b, var);
            s->band_energy[5][f] = 10.0 * exp5;
            s->band_energy[20][f] = 0.5 * exp20;
        }
        ats::AtsSoundPtr before(copy_sound(s));

        const auto r = ats::clampResidualToSource(s, src.data(), src.size(), kRate);
        std::cout << "clamp: framesChanged " << r.framesChanged << " bandsChanged " << r.bandsChanged
                  << " removed " << r.energyRemovedFraction << '\n';
        assert(r.framesChanged == kFrames);
        assert(r.bandsChanged >= kFrames);
        // In units of M*var/sr: total = 0.05*18580 + 1200 + 650 = 2779, band 5 drops
        // from 1200 to ~105 -> ~0.39 removed.
        assert(r.energyRemovedFraction > 0.3 && r.energyRemovedFraction < 0.5);

        // Interior frames (block fully inside the signal): band 5 clamped to ~source,
        // band 20 untouched, other bands untouched.
        double sum5 = 0.0;
        int count = 0;
        for (int f = 3; f < kFrames - 3; ++f)
        {
            assert(s->band_energy[5][f] < 10.0 * exp5);
            // 11 Hann bins -> ~15 dof chi-square per frame (rel. std ~0.37): wide bracket.
            assert(s->band_energy[5][f] > 0.1 * exp5 && s->band_energy[5][f] < 5.0 * exp5);
            sum5 += s->band_energy[5][f];
            ++count;
            assert(s->band_energy[20][f] == before.get()->band_energy[20][f]);
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                if (b != 5)
                    assert(s->band_energy[b][f] == before.get()->band_energy[b][f]);
        }
        const double mean5 = sum5 / count;
        std::cout << "band 5 mean " << mean5 << " expected " << exp5 << '\n';
        assert(std::abs(mean5 - exp5) < 0.2 * exp5); // analysis-like convention holds

        // Headroom scales the bound; a second pass with headroom 1 is a no-op.
        ats::AtsSoundPtr half(copy_sound(before.get()));
        const auto rh = ats::clampResidualToSource(half.get(), src.data(), src.size(), kRate, 0.5);
        assert(rh.bandsChanged >= r.bandsChanged);
        for (int f = 3; f < kFrames - 3; ++f)
            assert(std::abs(half.get()->band_energy[5][f] - 0.5 * s->band_energy[5][f]) < 1.0e-12 * exp5);
        const auto r2 = ats::clampResidualToSource(s, src.data(), src.size(), kRate);
        assert(r2.bandsChanged == 0 && r2.framesChanged == 0 && r2.energyRemovedFraction == 0.0);

        // Invalid input: nothing happens.
        const auto r3 = ats::clampResidualToSource(s, nullptr, 0, kRate);
        assert(r3.bandsChanged == 0);
    }

    // (2) high-pass taper.
    {
        auto model = makeModel();
        ATS_SOUND* s = model.get();
        ats::AtsSoundPtr before(copy_sound(s));
        assert(ats::highPassResidual(s, 300.0));
        for (int f = 0; f < kFrames; ++f)
        {
            assert(s->band_energy[0][f] == 0.0);  // 50 Hz centre: below 150 Hz stop
            assert(s->band_energy[1][f] == 0.0);  // 150 Hz centre: exactly at the stop edge
            assert(s->band_energy[2][f] > 0.0 && s->band_energy[2][f] < before.get()->band_energy[2][f]);
            assert(s->band_energy[3][f] == before.get()->band_energy[3][f]); // 350 Hz: passed
            assert(s->band_energy[20][f] == before.get()->band_energy[20][f]);
        }
        // Monotone taper: band 2 gain is the raised cosine at x = log2(250/150).
        const double x = std::log2(250.0 / 150.0);
        const double gain = 0.5 - 0.5 * std::cos(3.141592653589793 * x);
        assert(std::abs(s->band_energy[2][0] - gain * before.get()->band_energy[2][0]) < 1.0e-15);

        // Idempotence where the transition region carries no energy: a second call
        // reports no change (fully stopped bands stay 0, passed bands stay put).
        auto sparse = makeModel();
        for (int f = 0; f < kFrames; ++f)
            sparse.get()->band_energy[2][f] = 0.0;
        assert(ats::highPassResidual(sparse.get(), 300.0));
        assert(!ats::highPassResidual(sparse.get(), 300.0));
        for (int f = 0; f < kFrames; ++f)
            assert(sparse.get()->band_energy[20][f] == before.get()->band_energy[20][f]);

        // Invalid cutoff / no residual: no change.
        assert(!ats::highPassResidual(s, 0.0));
        assert(!ats::highPassResidual(nullptr, 300.0));
    }

    // (3) reverseFrames twice is the identity.
    {
        auto model = makeModel();
        const ATS_SOUND* s = model.get();
        const auto rev = ats::reverseFrames(s);
        assert(rev && rev.get()->frames == s->frames && rev.get()->partials == s->partials);
        const ATS_SOUND* r = rev.get();
        for (int f = 0; f < kFrames; ++f)
        {
            const int g = kFrames - 1 - f;
            for (int p = 0; p < s->partials; ++p)
            {
                assert(r->frq[p][f] == s->frq[p][g]);
                assert(r->amp[p][f] == s->amp[p][g]);
                assert(r->smr[p][f] == s->smr[p][g]);
                assert(r->res[p][f] == s->res[p][g]);
                assert(r->pha[p][f] == 0.0);
                assert(r->frq_slope[p][f] == -s->frq_slope[p][g]);
                assert(r->amp_slope[p][f] == -s->amp_slope[p][g]);
            }
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                assert(r->band_energy[b][f] == s->band_energy[b][g]);
            if (f > 0)
                assert(r->time[0][f] > r->time[0][f - 1]);
            assert(std::abs(r->time[0][f] - s->time[0][f]) < 1.0e-12);
        }
        assert(r->has_slopes == s->has_slopes);

        const auto twice = ats::reverseFrames(r);
        const ATS_SOUND* t = twice.get();
        assert(t->frames == s->frames);
        for (int f = 0; f < kFrames; ++f)
        {
            for (int p = 0; p < s->partials; ++p)
            {
                assert(t->frq[p][f] == s->frq[p][f]);
                assert(t->amp[p][f] == s->amp[p][f]);
                assert(t->frq_slope[p][f] == s->frq_slope[p][f]);
            }
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                assert(t->band_energy[b][f] == s->band_energy[b][f]);
        }
        assert(!ats::reverseFrames(nullptr));
    }

    // (4) reversedSamples twice is the identity.
    {
        std::vector<double> x(1001);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = noiseAt(i);
        const auto r = ats::reversedSamples(x.data(), x.size());
        assert(r.size() == x.size() && r.front() == x.back() && r.back() == x.front());
        const auto rr = ats::reversedSamples(r.data(), r.size());
        assert(rr == x);
        assert(ats::reversedSamples(nullptr, 0).empty());
    }

    std::cout << "ats_residual_fixes_test: ok\n";
    return 0;
}

#include "AtsMicroModulation.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr int kPartials = 3;
constexpr int kFrames = 500; // 5 s at 100 frames/s
constexpr int kSilentTail = 50; // last 50 frames inactive

ats::AtsSoundPtr makeModel()
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, kFrames, kFrames * 0.01, kPartials, 1);
    for (int f = 0; f < kFrames; ++f)
    {
        s->time[0][f] = f * 0.01;
        const bool on = f < kFrames - kSilentTail;
        for (int p = 0; p < kPartials; ++p)
        {
            s->amp[p][f] = on ? 0.5 / (p + 1) : 0.0;
            s->frq[p][f] = on ? 220.0 * (p + 1) : 0.0;
            s->pha[p][f] = 0.25 * p;
            s->smr[p][f] = s->res[p][f] = 0.0;
            s->frq_slope[p][f] = s->amp_slope[p][f] = 0.0;
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = 0.0;
    }
    return ats::AtsSoundPtr(s);
}

struct Stats { double rms = 0.0; double mean = 0.0; };

/** Deviation statistics of out vs in for partial p over active frames; dev(x) maps the ratio. */
template <typename Dev>
Stats deviation(const ATS_SOUND* in, const ATS_SOUND* out, int p, double** ATS_SOUND::*field, Dev dev, std::vector<double>* devs = nullptr)
{
    Stats st;
    double sq = 0.0, sum = 0.0, ref = 0.0;
    int n = 0;
    for (int f = 0; f < in->frames; ++f)
    {
        if (in->amp[p][f] <= 0.0) continue;
        const double a = (in->*field)[p][f], b = (out->*field)[p][f];
        const double d = dev(b / a);
        if (devs) devs->push_back(d);
        sq += d * d;
        sum += b;
        ref += a;
        ++n;
    }
    st.rms = std::sqrt(sq / n);
    st.mean = sum / ref; // ratio of means
    return st;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
    double ma = 0.0, mb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= a.size(); mb /= b.size();
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(saa * sbb);
}

const auto dbDev = [](double ratio) { return 20.0 * std::log10(ratio); };
const auto centsDev = [](double ratio) { return 1200.0 * std::log2(ratio); };
}

int main()
{
    const auto model = makeModel();
    const auto* s = model.get();

    ats::MicroModulationParams p;
    assert(!p.active());
    assert(!ats::applyMicroModulation(s, p)); // inactive -> nullptr

    p.shimmerDb = 2.0;
    p.jitterCents = 10.0;
    p.lowPartialEmphasis = 0.0; // all three partials at full depth for the calibration check
    assert(p.active());
    assert(!ats::applyMicroModulation(nullptr, p));
    const auto out = ats::applyMicroModulation(s, p);
    assert(out);
    const auto* o = out.get();
    assert(o->frames == s->frames && o->partials == s->partials);

    for (int q = 0; q < kPartials; ++q)
    {
        const auto amp = deviation(s, o, q, &ATS_SOUND::amp, dbDev);
        const auto frq = deviation(s, o, q, &ATS_SOUND::frq, centsDev);
        std::cout << "partial " << q << ": amp rms " << amp.rms << " dB (mean ratio " << amp.mean << "), frq rms "
                  << frq.rms << " cents (mean ratio " << frq.mean << ")\n";
        assert(std::abs(amp.rms / 2.0 - 1.0) < 0.4);
        assert(std::abs(frq.rms / 10.0 - 1.0) < 0.4);
        assert(std::abs(amp.mean - 1.0) < 0.02);
        assert(std::abs(frq.mean - 1.0) < 0.02);
        // Inactive frames stay zero; phases untouched.
        for (int f = kFrames - kSilentTail; f < kFrames; ++f)
            assert(o->amp[q][f] == 0.0 && o->frq[q][f] == 0.0);
        for (int f = 0; f < kFrames; ++f)
            assert(o->pha[q][f] == s->pha[q][f]);
    }

    // Deterministic.
    const auto again = ats::applyMicroModulation(s, p);
    for (int q = 0; q < kPartials; ++q)
        for (int f = 0; f < kFrames; ++f)
            assert(again.get()->amp[q][f] == o->amp[q][f] && again.get()->frq[q][f] == o->frq[q][f]);

    // Low-partial emphasis: with 3 emphasised partials the 4th gets (1 - emphasis) of the depth.
    {
        auto* big = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
        init_sound(big, 44100, 441, 1764, kFrames, kFrames * 0.01, 4, 1);
        for (int f = 0; f < kFrames; ++f)
        {
            big->time[0][f] = f * 0.01;
            for (int q = 0; q < 4; ++q)
            {
                big->amp[q][f] = 0.1;
                big->frq[q][f] = 220.0 * (4 - q); // descending so rank != index
                big->pha[q][f] = big->smr[q][f] = big->res[q][f] = 0.0;
                big->frq_slope[q][f] = big->amp_slope[q][f] = 0.0;
            }
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b) big->band_energy[b][f] = 0.0;
        }
        ats::AtsSoundPtr bigOwner(big);
        auto e = p;
        e.lowPartialEmphasis = 0.75;
        const auto r = ats::applyMicroModulation(big, e);
        const auto high = deviation(big, r.get(), 0, &ATS_SOUND::amp, dbDev); // 880 Hz: highest -> rank 3
        const auto low = deviation(big, r.get(), 3, &ATS_SOUND::amp, dbDev);  // 220 Hz: rank 0
        std::cout << "emphasis: highest partial rms " << high.rms << " dB, lowest " << low.rms << " dB\n";
        assert(std::abs(high.rms / (2.0 * 0.25) - 1.0) < 0.4);
        assert(std::abs(low.rms / 2.0 - 1.0) < 0.4);
    }

    // correlation 1: identical normalised modulation across partials.
    {
        auto c1 = p;
        c1.correlation = 1.0;
        const auto r = ats::applyMicroModulation(s, c1);
        std::vector<double> d0, d1, d2;
        deviation(s, r.get(), 0, &ATS_SOUND::amp, dbDev, &d0);
        deviation(s, r.get(), 1, &ATS_SOUND::amp, dbDev, &d1);
        deviation(s, r.get(), 2, &ATS_SOUND::frq, centsDev, &d2);
        for (std::size_t i = 0; i < d0.size(); ++i)
            assert(std::abs(d0[i] - d1[i]) < 1.0e-9);
        std::vector<double> f0, f1;
        deviation(s, r.get(), 0, &ATS_SOUND::frq, centsDev, &f0);
        deviation(s, r.get(), 1, &ATS_SOUND::frq, centsDev, &f1);
        for (std::size_t i = 0; i < f0.size(); ++i)
            assert(std::abs(f0[i] - f1[i]) < 1.0e-9);
        std::cout << "correlation 1: amp/frq streams corr " << correlation(d0, f0) << " (independent channels)\n";
    }
    // correlation 0: weakly correlated partials.
    {
        auto c0 = p;
        c0.correlation = 0.0;
        const auto r = ats::applyMicroModulation(s, c0);
        std::vector<double> d0, d1, d2;
        deviation(s, r.get(), 0, &ATS_SOUND::amp, dbDev, &d0);
        deviation(s, r.get(), 1, &ATS_SOUND::amp, dbDev, &d1);
        deviation(s, r.get(), 2, &ATS_SOUND::amp, dbDev, &d2);
        const double c01 = correlation(d0, d1), c02 = correlation(d0, d2), c12 = correlation(d1, d2);
        std::cout << "correlation 0: pairwise corr " << c01 << ", " << c02 << ", " << c12 << "\n";
        assert(std::abs(c01) < 0.3 && std::abs(c02) < 0.3 && std::abs(c12) < 0.3);
    }

    std::cout << "ats_micro_modulation_test: ok\n";
    return 0;
}

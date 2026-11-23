#include "AtsResidualSynth.h"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr int kBand = 10; // 1270..1480 Hz
constexpr int kFrames = 200; // 2 s at 100 frames/s
constexpr double kEnergy = 0.5;
constexpr int kWindow = 1764;

ats::AtsSoundPtr makeModel()
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, kWindow, kFrames, kFrames * 0.01, 1, 1);
    for (int f = 0; f < kFrames; ++f)
    {
        s->time[0][f] = f * 0.01;
        s->amp[0][f] = 0.0;
        s->frq[0][f] = 0.0;
        s->pha[0][f] = s->smr[0][f] = s->res[0][f] = 0.0;
        s->frq_slope[0][f] = s->amp_slope[0][f] = 0.0;
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = b == kBand ? kEnergy : 0.0;
    }
    return ats::AtsSoundPtr(s);
}

std::vector<double> render(const ATS_SOUND* s, int density, std::uint64_t seed = 0x5EED, double timeFactor = 1.0)
{
    ats::ResidualSynthParams p;
    p.carriersPerBand = density;
    p.seed = seed;
    p.timeFactor = timeFactor;
    std::vector<double> out;
    const auto n = ats::renderResidualBands(s, p, out);
    assert(n == out.size());
    return out;
}

double rms(const std::vector<double>& v, std::size_t begin = 0, std::size_t end = 0)
{
    if (end == 0) end = v.size();
    double acc = 0.0;
    for (std::size_t i = begin; i < end; ++i)
        acc += v[i] * v[i];
    return std::sqrt(acc / static_cast<double>(end - begin));
}

/** std/mean of 10 ms block RMS values. */
double envelopeFluctuation(const std::vector<double>& v, double sr)
{
    const auto block = static_cast<std::size_t>(sr * 0.010);
    std::vector<double> blocks;
    for (std::size_t i = 0; i + block <= v.size(); i += block)
        blocks.push_back(rms(v, i, i + block));
    double mean = 0.0;
    for (double b : blocks) mean += b;
    mean /= static_cast<double>(blocks.size());
    double var = 0.0;
    for (double b : blocks) var += (b - mean) * (b - mean);
    return std::sqrt(var / static_cast<double>(blocks.size())) / mean;
}

void fft(std::vector<std::complex<double>>& x)
{
    const std::size_t n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.141592653589793 / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Out-of-band / in-band energy ratio in dB; the band is [1270, 1480] Hz +- 10%. */
double outOfBandDb(const std::vector<double>& v, double sr)
{
    const std::size_t n = 65536;
    assert(v.size() >= n);
    std::vector<std::complex<double>> x(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 * (1.0 - std::cos(2.0 * 3.141592653589793 * static_cast<double>(i) / static_cast<double>(n)));
        x[i] = std::complex<double>(v[i] * w, 0.0);
    }
    fft(x);
    const double lo = ATSA_CRITICAL_BAND_EDGES[kBand] * 0.9, hi = ATSA_CRITICAL_BAND_EDGES[kBand + 1] * 1.1;
    double in = 0.0, out = 0.0;
    for (std::size_t k = 1; k < n / 2; ++k)
    {
        const double f = static_cast<double>(k) * sr / static_cast<double>(n);
        const double e = std::norm(x[k]);
        if (f >= lo && f <= hi) in += e; else out += e;
    }
    return 10.0 * std::log10(out / in);
}

/** Welch PSD (4096-point Hann, half overlap) spectral flatness inside [1270, 1480] Hz:
    geometric / arithmetic mean of the bin powers, 1 = flat. */
double inBandFlatness(const std::vector<double>& v, double sr)
{
    const std::size_t n = 4096;
    std::vector<double> psd(n / 2, 0.0);
    std::vector<std::complex<double>> x(n);
    int segments = 0;
    for (std::size_t start = 0; start + n <= v.size(); start += n / 2, ++segments)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 * (1.0 - std::cos(2.0 * 3.141592653589793 * static_cast<double>(i) / static_cast<double>(n)));
            x[i] = std::complex<double>(v[start + i] * w, 0.0);
        }
        fft(x);
        for (std::size_t k = 0; k < n / 2; ++k) psd[k] += std::norm(x[k]);
    }
    double logSum = 0.0, sum = 0.0;
    int bins = 0;
    for (std::size_t k = 1; k < n / 2; ++k)
    {
        const double f = static_cast<double>(k) * sr / static_cast<double>(n);
        if (f < ATSA_CRITICAL_BAND_EDGES[kBand] || f > ATSA_CRITICAL_BAND_EDGES[kBand + 1]) continue;
        const double p = psd[k] / segments;
        logSum += std::log(p);
        sum += p;
        ++bins;
    }
    return std::exp(logSum / bins) / (sum / bins);
}
}

int main()
{
    const auto model = makeModel();
    const auto* s = model.get();
    const double reference = ats::bandReferenceRms(kEnergy, kWindow);
    assert(std::abs(reference - std::sqrt(kEnergy / (kWindow * 0.04)) / 3.0) < 1.0e-12);

    const int densities[] = { 1, 4, 16 };
    std::vector<double> fluctuation, leakage, flatness;
    for (int d : densities)
    {
        const auto out = render(s, d);
        assert(out.size() == static_cast<std::size_t>(2.0 * 44100.0));
        const double r = rms(out);
        fluctuation.push_back(envelopeFluctuation(out, 44100.0));
        leakage.push_back(outOfBandDb(out, 44100.0));
        flatness.push_back(inBandFlatness(out, 44100.0));
        std::cout << "density " << d << ": rms " << r << " (reference " << reference << ", ratio " << r / reference
                  << "), 10 ms envelope std/mean " << fluctuation.back() << ", out-of-band " << leakage.back()
                  << " dB, in-band flatness " << flatness.back() << "\n";
        assert(std::abs(r / reference - 1.0) < 0.15); // (a)
    }
    // (b) density 16 leaks less than -20 dB outside the band.
    assert(leakage[2] < -20.0);
    // (c) Density fills the band progressively: leakage falls monotonically from 1 to 16
    // and the band is flatter at 16 than at 1 (density 4 shows a resolvable 52 Hz comb
    // ripple, so flatness is only compared at the ends).  The 10 ms block-RMS fluctuation
    // is NOT a usable density metric: any signal filling a 210 Hz band is bounded below by
    // the Gaussian value ~1/(2 sqrt(B T)) ~ 0.34 at B T ~ 2, and ATSA's single randi
    // carrier already sits there, so all densities land within [0.25, 0.45].
    assert(leakage[0] > leakage[1] && leakage[1] > leakage[2]);
    assert(flatness[2] > flatness[0]);
    for (double fl : fluctuation)
        assert(fl > 0.25 && fl < 0.45);

    // (d) deterministic; a different seed changes the output.
    const auto a = render(s, 4), b = render(s, 4), c = render(s, 4, 0xBEEF);
    assert(a == b);
    double diff = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = std::max(diff, std::abs(a[i] - c[i]));
    assert(diff > 1.0e-3);

    // (e) timeFactor 2 doubles the length and keeps the level.
    const auto slow = render(s, 4, 0x5EED, 2.0);
    assert(slow.size() == 2 * a.size());
    assert(std::abs(rms(slow) / reference - 1.0) < 0.15);

    // Zero duration range and noiseless model render nothing.
    {
        ats::ResidualSynthParams p;
        p.startTime = 5.0;
        std::vector<double> out;
        assert(ats::renderResidualBands(s, p, out) == 0 && out.empty());
        assert(ats::renderResidualBands(nullptr, p, out) == 0);
    }

    // skipBandsWithPartials silences the band when a partial sits in it.
    {
        auto withPartial = makeModel();
        for (int f = 0; f < kFrames; ++f) { withPartial.get()->amp[0][f] = 0.1; withPartial.get()->frq[0][f] = 1350.0; }
        ats::ResidualSynthParams p;
        p.skipBandsWithPartials = true;
        std::vector<double> out;
        ats::renderResidualBands(withPartial.get(), p, out);
        assert(rms(out) < 1.0e-12);
        p.skipBandsWithPartials = false;
        out.clear();
        ats::renderResidualBands(withPartial.get(), p, out);
        assert(rms(out) > 0.5 * reference);
    }

    std::cout << "ats_residual_synth_test: ok\n";
    return 0;
}

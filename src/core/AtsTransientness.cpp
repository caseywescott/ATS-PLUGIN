#include "AtsTransientness.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ats
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279;

int nextPow2(int v)
{
    int n = 1;
    while (n < v && n < (1 << 29))
        n <<= 1;
    return n;
}

// In-place iterative radix-2 complex FFT (forward, unscaled). n must be a power of two.
void fftInPlace(std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    if (n < 2)
        return;
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len;
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

struct Scratch
{
    std::vector<double> block;
    std::vector<double> window;
    std::vector<double> wavelet;
    std::vector<double> waveletTmp;
    std::vector<double> cosine;
    std::vector<std::complex<double>> fft;
};

// Orthonormal DCT-II of `block` (length N, power of two) via a 2N-point FFT:
// C_k = s_k * Re( e^{-i pi k / (2N)} * sum_n x_n e^{-i 2 pi k n / (2N)} ).
void dctII(const std::vector<double>& block, std::vector<double>& out, std::vector<std::complex<double>>& fft)
{
    const std::size_t n = block.size();
    fft.assign(2 * n, std::complex<double>(0.0, 0.0));
    for (std::size_t i = 0; i < n; ++i)
        fft[i] = std::complex<double>(block[i], 0.0);
    fftInPlace(fft);
    out.resize(n);
    const double s0 = std::sqrt(1.0 / (double) n);
    const double sk = std::sqrt(2.0 / (double) n);
    for (std::size_t k = 0; k < n; ++k)
    {
        const double ang = -kPi * (double) k / (2.0 * (double) n);
        const std::complex<double> rot(std::cos(ang), std::sin(ang));
        out[k] = (k == 0 ? s0 : sk) * (fft[k] * rot).real();
    }
}

// Full-depth periodic Daubechies-4 DWT, in place. Each level maps the first
// `len` entries to [approximation | detail]; recursion stops at length 4 (the
// filter length), below which the periodised filters are no longer orthogonal.
void dwtDaubechies4(std::vector<double>& data, std::vector<double>& tmp)
{
    constexpr double kSqrt3 = 1.7320508075688772935274463415059;
    constexpr double kDen = 4.0 * 1.4142135623730950488016887242097;
    const double h0 = (1.0 + kSqrt3) / kDen;
    const double h1 = (3.0 + kSqrt3) / kDen;
    const double h2 = (3.0 - kSqrt3) / kDen;
    const double h3 = (1.0 - kSqrt3) / kDen;
    const double g0 = h3, g1 = -h2, g2 = h1, g3 = -h0;

    std::size_t n = data.size();
    tmp.resize(n);
    while (n >= 4)
    {
        const std::size_t half = n / 2;
        for (std::size_t i = 0; i < half; ++i)
        {
            const std::size_t j = 2 * i;
            const double x0 = data[j];
            const double x1 = data[(j + 1) % n];
            const double x2 = data[(j + 2) % n];
            const double x3 = data[(j + 3) % n];
            tmp[i] = h0 * x0 + h1 * x1 + h2 * x2 + h3 * x3;
            tmp[half + i] = g0 * x0 + g1 * x1 + g2 * x2 + g3 * x3;
        }
        std::copy(tmp.begin(), tmp.begin() + (long) n, data.begin());
        n = half;
    }
}

// Estimated expansion "size" N̂ = 2^D * N, D = (1/N) Σ log2(e_n), e_n normalised.
// Returns 0 when the block carries no energy.
double estimatedSize(const std::vector<double>& coeffs, double floorEnergy)
{
    double total = 0.0;
    for (const double c : coeffs)
        total += c * c;
    if (!(total > 0.0) || !std::isfinite(total))
        return 0.0;
    const double floorE = (std::isfinite(floorEnergy) && floorEnergy > 0.0) ? floorEnergy : 1e-12;
    double logSum = 0.0;
    for (const double c : coeffs)
        logSum += std::log2(std::max(c * c / total, floorE));
    const double n = (double) coeffs.size();
    return std::exp2(logSum / n) * n;
}

// Index for a block centred at sample `centre` (zero-padded at the edges).
double blockIndex(const double* samples, std::size_t sampleCount, long long centre, int blockSize,
                  const TransientnessParams& params, Scratch& s)
{
    const std::size_t n = (std::size_t) blockSize;
    s.block.assign(n, 0.0);
    const long long start = centre - (long long) (n / 2);
    for (std::size_t i = 0; i < n; ++i)
    {
        const long long idx = start + (long long) i;
        if (idx >= 0 && (std::size_t) idx < sampleCount)
        {
            const double v = samples[(std::size_t) idx];
            s.block[i] = std::isfinite(v) ? v * s.window[i] : 0.0;
        }
    }

    dctII(s.block, s.cosine, s.fft);
    s.wavelet = s.block;
    dwtDaubechies4(s.wavelet, s.waveletTmp);

    const double sizeCos = estimatedSize(s.cosine, params.floorEnergy);
    const double sizeWav = estimatedSize(s.wavelet, params.floorEnergy);
    const double denom = sizeCos + sizeWav;
    if (!(denom > 0.0) || !std::isfinite(denom))
        return 0.0;
    return std::clamp(sizeCos / denom, 0.0, 1.0);
}

void prepareWindow(Scratch& s, int blockSize)
{
    s.window.resize((std::size_t) blockSize);
    for (int i = 0; i < blockSize; ++i) // sine window
        s.window[(std::size_t) i] = std::sin(kPi * ((double) i + 0.5) / (double) blockSize);
}

TransientnessResult finish(TransientnessResult r)
{
    double sum = 0.0;
    for (const double v : r.index)
        sum += v;
    r.mean = r.index.empty() ? 0.0 : sum / (double) r.index.size();
    return r;
}
} // namespace

TransientnessResult transientnessIndex(const double* samples,
                                       std::size_t sampleCount,
                                       int sampleRate,
                                       const ATS_SOUND* sound,
                                       const TransientnessParams& params)
{
    TransientnessResult result;
    if (sound == nullptr || sound->frames <= 0 || sampleRate <= 0)
        return result;

    const double rateScale = sound->srate > 0 ? (double) sampleRate / (double) sound->srate : 1.0;
    int block = params.blockSize > 0 ? params.blockSize
                                     : (int) std::lround(std::max(sound->window_size, 2) * rateScale);
    block = nextPow2(std::max(block, 2));

    Scratch s;
    prepareWindow(s, block);
    result.index.resize((std::size_t) sound->frames, 0.0);
    const bool haveTime = sound->time != nullptr && sound->time[0] != nullptr;
    const double hopSamples = (double) std::max(sound->frame_size, 1) * rateScale;
    for (int f = 0; f < sound->frames; ++f)
    {
        double t = haveTime ? sound->time[0][f] : -1.0;
        const long long centre = (haveTime && std::isfinite(t) && t >= 0.0)
                                     ? std::llround(t * (double) sampleRate)
                                     : std::llround((double) f * hopSamples);
        if (samples == nullptr || sampleCount == 0)
            continue;
        result.index[(std::size_t) f] = blockIndex(samples, sampleCount, centre, block, params, s);
    }
    return finish(std::move(result));
}

TransientnessResult transientnessIndex(const double* samples,
                                       std::size_t sampleCount,
                                       int hopSamples,
                                       int blockSize,
                                       const TransientnessParams& params)
{
    TransientnessResult result;
    if (samples == nullptr || sampleCount == 0 || hopSamples <= 0 || blockSize <= 0)
        return result;

    const int block = nextPow2(std::max(blockSize, 2));
    const std::size_t frames = (sampleCount + (std::size_t) hopSamples - 1) / (std::size_t) hopSamples;
    Scratch s;
    prepareWindow(s, block);
    result.index.resize(frames, 0.0);
    for (std::size_t f = 0; f < frames; ++f)
        result.index[f] = blockIndex(samples, sampleCount, (long long) f * hopSamples, block, params, s);
    return finish(std::move(result));
}

} // namespace ats

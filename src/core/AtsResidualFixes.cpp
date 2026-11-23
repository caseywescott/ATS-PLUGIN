#include "AtsResidualFixes.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ats
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279;

// ATSA's residual analysis (qatsh/src/atsa/residual-analysis.c) constants.
constexpr int kResMinFftSize = 4096; // ATSA_RES_MIN_FFT_SIZE
constexpr int kResPadFactor = 2;     // ATSA_RES_PAD_FACTOR

bool finitePositive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

// Smallest power of two >= v (ATSA's ppp2 semantics as used by residual_get_N).
int ceilPow2(int v)
{
    int n = 1;
    while (n < v && n < (1 << 29))
        n <<= 1;
    return n;
}

// Mirrors residual_get_N(M, ATSA_RES_MIN_FFT_SIZE, ATSA_RES_PAD_FACTOR).
int residualFftSize(int windowSize)
{
    int n = kResPadFactor * windowSize;
    while (n < kResMinFftSize)
        n = ceilPow2(n + 1);
    if ((n & (n - 1)) != 0)
        n = ceilPow2(n + 1);
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

double hopSeconds(const ATS_SOUND* s)
{
    if (s->time != nullptr && s->time[0] != nullptr && s->frames > 1)
    {
        const double dt = s->time[0][1] - s->time[0][0];
        if (finitePositive(dt))
            return dt;
    }
    if (s->srate > 0 && s->frame_size > 0)
        return (double) s->frame_size / (double) s->srate;
    return 0.0;
}

double bandCentreHz(int b)
{
    return 0.5 * (ATSA_CRITICAL_BAND_EDGES[b] + ATSA_CRITICAL_BAND_EDGES[b + 1]);
}

template <typename T>
void reverseRow(T* row, int frames)
{
    if (row != nullptr && frames > 1)
        std::reverse(row, row + frames);
}
} // namespace

/*
 * Band-energy convention (apples-to-apples with ATSA):
 *
 *   ATSA (residual_analysis / residual_get_band_energy) places M = window_size
 *   residual samples, *unwindowed*, into an N-point zero-padded FFT with
 *   N = residual_get_N(M, 4096, 2), bins the one-sided spectrum with
 *   limits[k] = floor(edge_k / (sr / N)), and stores
 *
 *       band_energy[b][f] = (1/N) * sum_{k = limits[b]}^{limits[b+1]-1} |X_k|^2.
 *
 *   For a stationary noise of variance s^2 this is, in expectation,
 *   M * s^2 * (bins in band) / N  ~=  M * s^2 * bandwidth / sr   (independent of N).
 *
 * Here the source block is Hann-windowed (w), which scales every E|X_k|^2 by
 * sum(w^2) / M relative to the rectangular window; we therefore multiply by
 * M / sum(w^2) so a white-noise source gives the same expected band_energy as
 * ATSA would have produced had the whole signal been the residual. The block
 * start (time[0][f]*sr - floor((M-1)/2)) and N follow ATSA exactly.
 */
ResidualClampResult clampResidualToSource(ATS_SOUND* sound,
                                          const double* samples,
                                          std::size_t sampleCount,
                                          int sampleRate,
                                          double headroom)
{
    ResidualClampResult result;
    if (sound == nullptr || sound->band_energy == nullptr || samples == nullptr || sampleCount == 0
        || sound->frames <= 0 || sampleRate <= 0 || sound->window_size <= 0)
        return result;
    if (!std::isfinite(headroom) || headroom < 0.0)
        headroom = 1.0;

    const double rateScale = sound->srate > 0 ? (double) sampleRate / (double) sound->srate : 1.0;
    const int M = std::max(2, (int) std::lround(sound->window_size * rateScale));
    const int N = residualFftSize(M);
    const int M2 = (M - 1) / 2;
    const double binHz = (double) sampleRate / (double) N;
    const int nyquistBin = N / 2;

    int limits[ATSA_CRITICAL_BANDS + 1];
    for (int k = 0; k <= ATSA_CRITICAL_BANDS; ++k)
        limits[k] = (int) std::floor(ATSA_CRITICAL_BAND_EDGES[k] / binHz);

    std::vector<double> window((std::size_t) M);
    double windowEnergy = 0.0;
    for (int i = 0; i < M; ++i)
    {
        window[(std::size_t) i] = 0.5 * (1.0 - std::cos(2.0 * kPi * (double) i / (double) M));
        windowEnergy += window[(std::size_t) i] * window[(std::size_t) i];
    }
    const double windowGain = windowEnergy > 0.0 ? (double) M / windowEnergy : 1.0;

    const bool haveTime = sound->time != nullptr && sound->time[0] != nullptr;
    const double hopSamples = (double) std::max(sound->frame_size, 1) * rateScale;

    std::vector<std::complex<double>> fft;
    double totalBefore = 0.0;
    double removed = 0.0;
    for (int f = 0; f < sound->frames; ++f)
    {
        const double t = haveTime ? sound->time[0][f] : -1.0;
        const long long centre = (haveTime && std::isfinite(t) && t >= 0.0)
                                     ? std::llround(t * (double) sampleRate)
                                     : std::llround((double) f * hopSamples);
        const long long start = centre - M2;

        fft.assign((std::size_t) N, std::complex<double>(0.0, 0.0));
        for (int i = 0; i < M; ++i)
        {
            const long long idx = start + i;
            if (idx >= 0 && (std::size_t) idx < sampleCount)
            {
                const double v = samples[(std::size_t) idx];
                fft[(std::size_t) i] = std::complex<double>(std::isfinite(v) ? v * window[(std::size_t) i] : 0.0, 0.0);
            }
        }
        fftInPlace(fft);

        bool frameChanged = false;
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        {
            if (sound->band_energy[b] == nullptr)
                continue;
            const int lo = std::max(0, limits[b]);
            const int hi = std::min(limits[b + 1], nyquistBin);
            double sum = 0.0;
            for (int k = lo; k < hi; ++k)
                sum += std::norm(fft[(std::size_t) k]);
            const double source = sum / (double) N * windowGain;

            double& e = sound->band_energy[b][f];
            if (!std::isfinite(e) || e <= 0.0)
                continue;
            totalBefore += e;
            const double bound = headroom * source;
            if (e > bound)
            {
                removed += e - bound;
                e = bound;
                ++result.bandsChanged;
                frameChanged = true;
            }
        }
        if (frameChanged)
            ++result.framesChanged;
    }

    result.energyRemovedFraction = totalBefore > 0.0 ? removed / totalBefore : 0.0;
    return result;
}

bool highPassResidual(ATS_SOUND* sound, double cutoffHz, double transitionOctaves)
{
    if (sound == nullptr || sound->band_energy == nullptr || sound->frames <= 0
        || !finitePositive(cutoffHz))
        return false;
    if (!std::isfinite(transitionOctaves) || transitionOctaves <= 0.0)
        transitionOctaves = 1.0e-6; // degenerate: brick wall at cutoffHz

    const double logCut = std::log2(cutoffHz);
    const double logStop = logCut - transitionOctaves;

    bool changed = false;
    for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
    {
        if (sound->band_energy[b] == nullptr)
            continue;
        const double fc = bandCentreHz(b);
        const double x = std::clamp((std::log2(fc) - logStop) / transitionOctaves, 0.0, 1.0);
        if (x >= 1.0)
            continue; // fully passed
        const double gain = 0.5 - 0.5 * std::cos(kPi * x); // raised cosine, 0 at stop, 1 at cutoff
        for (int f = 0; f < sound->frames; ++f)
        {
            double& e = sound->band_energy[b][f];
            if (!std::isfinite(e))
                continue;
            const double next = e * gain;
            if (next != e)
            {
                e = next;
                changed = true;
            }
        }
    }
    return changed;
}

AtsSoundPtr reverseFrames(const ATS_SOUND* sound)
{
    if (sound == nullptr)
        return AtsSoundPtr();
    ATS_SOUND* out = copy_sound(const_cast<ATS_SOUND*>(sound));
    if (out == nullptr)
        return AtsSoundPtr();

    const int frames = out->frames;
    const int partials = std::max(out->partials, 0);
    for (int p = 0; p < partials; ++p)
    {
        if (out->frq != nullptr) reverseRow(out->frq[p], frames);
        if (out->amp != nullptr) reverseRow(out->amp[p], frames);
        if (out->smr != nullptr) reverseRow(out->smr[p], frames);
        if (out->res != nullptr) reverseRow(out->res[p], frames);
        if (out->pha != nullptr && out->pha[p] != nullptr)
            std::fill(out->pha[p], out->pha[p] + frames, 0.0);
        if (out->frq_slope != nullptr && out->frq_slope[p] != nullptr)
        {
            reverseRow(out->frq_slope[p], frames);
            for (int f = 0; f < frames; ++f)
                out->frq_slope[p][f] = -out->frq_slope[p][f];
        }
        if (out->amp_slope != nullptr && out->amp_slope[p] != nullptr)
        {
            reverseRow(out->amp_slope[p], frames);
            for (int f = 0; f < frames; ++f)
                out->amp_slope[p][f] = -out->amp_slope[p][f];
        }
    }
    if (out->band_energy != nullptr)
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            reverseRow(out->band_energy[b], frames);

    if (out->time != nullptr && out->time[0] != nullptr && frames > 0)
    {
        const double hop = hopSeconds(sound);
        const double t0 = (sound->time != nullptr && sound->time[0] != nullptr && std::isfinite(sound->time[0][0]))
                              ? sound->time[0][0]
                              : 0.0;
        for (int f = 0; f < frames; ++f)
            out->time[0][f] = t0 + hop * (double) f;
    }
    return AtsSoundPtr(out);
}

std::vector<double> reversedSamples(const double* samples, std::size_t sampleCount)
{
    std::vector<double> out;
    if (samples == nullptr || sampleCount == 0)
        return out;
    out.assign(samples, samples + sampleCount);
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace ats

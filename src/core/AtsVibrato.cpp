#include "AtsVibrato.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>

namespace ats
{

namespace
{
constexpr double kPi = 3.141592653589793;

double hopSeconds(const ATS_SOUND* s) noexcept
{
    if (s->frame_size > 0 && s->srate > 0)
        return static_cast<double>(s->frame_size) / s->srate;
    if (s->dur > 0.0 && s->frames > 0)
        return s->dur / s->frames;
    return 0.01;
}

/** Odd frame count >= 3 covering `seconds`. */
int oddFrames(double seconds, double hop)
{
    int n = static_cast<int>(std::ceil(std::max(0.0, seconds) / hop));
    if (n % 2 == 0)
        ++n;
    return std::max(3, n);
}

double medianOf(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    double m = *mid;
    if (v.size() % 2 == 0)
    {
        const auto lower = std::max_element(v.begin(), mid);
        m = 0.5 * (m + *lower);
    }
    return m;
}

double percentile(std::vector<double> v, double q)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const double pos = std::clamp(q, 0.0, 1.0) * static_cast<double>(v.size() - 1);
    const auto i = static_cast<std::size_t>(std::floor(pos));
    const double t = pos - static_cast<double>(i);
    return i + 1 < v.size() ? v[i] + t * (v[i + 1] - v[i]) : v[i];
}

/** Moving median over a compact sequence; the window shrinks at the edges. */
std::vector<double> movingMedian(const std::vector<double>& v, int window)
{
    const int n = static_cast<int>(v.size());
    const int half = std::max(1, window) / 2;
    std::vector<double> out(v.size());
    std::vector<double> buf;
    for (int i = 0; i < n; ++i)
    {
        const int lo = std::max(0, i - half);
        const int hi = std::min(n - 1, i + half);
        buf.assign(v.begin() + lo, v.begin() + hi + 1);
        out[static_cast<std::size_t>(i)] = medianOf(std::move(buf));
    }
    return out;
}

/** Moving average with a fractional window of `length` samples (sample j
    covers [j - 0.5, j + 0.5]); the window shrinks and renormalises at the
    edges.  With length = one vibrato period it nulls the vibrato fundamental
    and all its harmonics. */
std::vector<double> movingMeanFractional(const std::vector<double>& v, double length)
{
    const int n = static_cast<int>(v.size());
    std::vector<double> out(v.size());
    const double half = 0.5 * std::max(1.0, length);
    for (int i = 0; i < n; ++i)
    {
        const double lo = i - half, hi = i + half;
        const int jlo = std::max(0, static_cast<int>(std::floor(lo + 0.5)));
        const int jhi = std::min(n - 1, static_cast<int>(std::ceil(hi - 0.5)));
        double acc = 0.0, wsum = 0.0;
        for (int j = jlo; j <= jhi; ++j)
        {
            const double w = std::min(hi, j + 0.5) - std::max(lo, j - 0.5);
            if (w > 0.0)
            {
                acc += w * v[static_cast<std::size_t>(j)];
                wsum += w;
            }
        }
        out[static_cast<std::size_t>(i)] = wsum > 0.0 ? acc / wsum : v[static_cast<std::size_t>(i)];
    }
    return out;
}

/** Carrier: moving median over `medianWindow` frames, then (when a rate is
    known) a one-period moving mean that removes the vibrato the short
    median leaks through. */
std::vector<double> carrierOf(const std::vector<double>& v, int medianWindow, double rateHz, double hop)
{
    auto c = movingMedian(v, medianWindow);
    if (rateHz > 0.0 && hop > 0.0)
        c = movingMeanFractional(c, 1.0 / (rateHz * hop));
    return c;
}

/** In-place iterative radix-2 FFT; size must be a power of two. */
void fft(std::vector<std::complex<double>>& x)
{
    const std::size_t n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = x[i + k];
                const auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct PitchTrack
{
    std::vector<int> frames;    // active frame indices
    std::vector<double> cents;  // pitch in cents relative to the median pitch, per active frame
    double f0 = 0.0;
};

PitchTrack buildPitchTrack(const ATS_SOUND* s)
{
    PitchTrack track;
    std::vector<double> lows;
    for (int f = 0; f < s->frames; ++f)
    {
        double low = 0.0;
        for (int p = 0; p < s->partials; ++p)
            if (s->amp[p][f] > 0.0 && s->frq[p][f] > 0.0 && (low == 0.0 || s->frq[p][f] < low))
                low = s->frq[p][f];
        if (low > 0.0)
            lows.push_back(low);
    }
    if (lows.empty())
        return track;
    track.f0 = medianOf(lows);

    std::vector<double> hz;
    for (int f = 0; f < s->frames; ++f)
    {
        double num = 0.0, den = 0.0;
        for (int p = 0; p < s->partials; ++p)
        {
            const double a = s->amp[p][f], q = s->frq[p][f];
            if (a <= 0.0 || q <= 0.0)
                continue;
            const double h = std::round(q / track.f0);
            if (h < 1.0)
                continue;
            num += a * q / h;
            den += a;
        }
        if (den > 0.0)
        {
            track.frames.push_back(f);
            hz.push_back(num / den);
        }
    }
    const double ref = medianOf(hz);
    track.cents.resize(hz.size());
    for (std::size_t i = 0; i < hz.size(); ++i)
        track.cents[i] = ref > 0.0 ? 1200.0 * std::log2(hz[i] / ref) : 0.0;
    return track;
}

struct ModulationSpectrum
{
    double rateHz = 0.0;
    double depthCents = 0.0;
    double prominenceDb = 0.0;
    bool dominant = false; // the in-range peak is the largest peak of the whole modulation spectrum (>= 1 Hz)
};

ModulationSpectrum analyseModulation(const std::vector<double>& mod, double fps, const VibratoAnalysisParams& params)
{
    ModulationSpectrum r;
    const std::size_t L = mod.size();
    if (L < 4 || fps <= 0.0)
        return r;
    std::size_t N = 1024;
    while (N < 4 * L)
        N <<= 1;
    std::vector<std::complex<double>> x(N, std::complex<double>(0.0, 0.0));
    double sumW2 = 0.0;
    for (std::size_t n = 0; n < L; ++n)
    {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(n) / static_cast<double>(L - 1)));
        x[n] = std::complex<double>(mod[n] * w, 0.0);
        sumW2 += w * w;
    }
    fft(x);
    const std::size_t half = N / 2;
    std::vector<double> mag(half + 1);
    for (std::size_t k = 0; k <= half; ++k)
        mag[k] = std::abs(x[k]);
    const double binHz = fps / static_cast<double>(N);
    const auto kLo = static_cast<std::size_t>(std::max(1.0, std::ceil(params.minRateHz / binHz)));
    const auto kHi = static_cast<std::size_t>(std::min(static_cast<double>(half - 1), std::floor(params.maxRateHz / binHz)));
    if (kLo >= kHi)
        return r;
    // Largest local maximum inside the range (a skirt of a peak outside the range is not a rate).
    std::size_t kp = 0;
    for (std::size_t k = kLo; k <= kHi; ++k)
        if (mag[k] > mag[k - 1] && mag[k] >= mag[k + 1] && (kp == 0 || mag[k] > mag[kp]))
            kp = k;
    if (kp == 0)
        return r;
    const double peak = mag[kp];
    if (peak < 1.0e-9)
        return r;
    const auto kMed = static_cast<std::size_t>(std::max(1.0, std::ceil(1.0 / binHz)));
    r.dominant = kMed < half && peak >= *std::max_element(mag.begin() + static_cast<std::ptrdiff_t>(kMed), mag.end());

    // Parabolic interpolation on dB magnitudes.
    const double alpha = 20.0 * std::log10(std::max(mag[kp - 1], 1.0e-30));
    const double beta = 20.0 * std::log10(peak);
    const double gamma = 20.0 * std::log10(std::max(mag[kp + 1], 1.0e-30));
    const double denom = alpha - 2.0 * beta + gamma;
    const double delta = std::abs(denom) > 1.0e-12 ? std::clamp(0.5 * (alpha - gamma) / denom, -1.0, 1.0) : 0.0;
    r.rateHz = (static_cast<double>(kp) + delta) * binHz;

    // Prominence over the median magnitude of all bins >= 1 Hz.
    std::vector<double> rest(mag.begin() + static_cast<std::ptrdiff_t>(std::min(kMed, half)), mag.end());
    const double med = medianOf(rest);
    r.prominenceDb = med > 1.0e-12 ? 20.0 * std::log10(peak / med) : 120.0;

    // Depth: rms of the modulation in the Hann main lobe (+-2 unpadded bins), peak = rms * sqrt(2).
    const std::size_t lobe = 2 * (N / L);
    const std::size_t a = kp > lobe ? kp - lobe : 1;
    const std::size_t b = std::min(half, kp + lobe);
    double energy = 0.0;
    for (std::size_t k = a; k <= b; ++k)
        energy += mag[k] * mag[k];
    if (sumW2 > 0.0)
        r.depthCents = std::sqrt(2.0 * (2.0 * energy / static_cast<double>(N)) / sumW2);
    return r;
}
} // namespace

VibratoAnalysis analyseVibrato(const ATS_SOUND* sound, const VibratoAnalysisParams& params)
{
    VibratoAnalysis result;
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0)
        return result;
    const int frames = sound->frames;
    const double hop = hopSeconds(sound);
    const double fps = 1.0 / hop;
    result.pitchCents.assign(static_cast<std::size_t>(frames), 0.0);
    result.carrierCents.assign(static_cast<std::size_t>(frames), 0.0);

    const auto track = buildPitchTrack(sound);
    result.fundamentalHz = track.f0;
    if (track.frames.size() < 8)
        return result;

    // Pass 1: rate from the median-only carrier; pass 2: carrier refined by a one-period mean.
    const int win = oddFrames(params.carrierWindowSeconds, hop);
    auto carrier = carrierOf(track.cents, win, 0.0, hop);
    std::vector<double> mod(track.cents.size());
    for (std::size_t i = 0; i < mod.size(); ++i)
        mod[i] = track.cents[i] - carrier[i];
    auto spectrum = analyseModulation(mod, fps, params);
    if (spectrum.rateHz > 0.0)
    {
        carrier = carrierOf(track.cents, win, spectrum.rateHz, hop);
        for (std::size_t i = 0; i < mod.size(); ++i)
            mod[i] = track.cents[i] - carrier[i];
        spectrum = analyseModulation(mod, fps, params);
    }

    result.rateHz = spectrum.rateHz;
    result.depthCents = spectrum.depthCents;
    result.prominenceDb = spectrum.prominenceDb;
    result.extentCents = percentile(mod, 0.95) - percentile(mod, 0.05);
    result.detected = spectrum.dominant
                   && result.extentCents >= params.minExtentCents
                   && result.rateHz >= params.minRateHz && result.rateHz <= params.maxRateHz
                   && result.prominenceDb >= params.minProminenceDb;

    // Per-frame tracks: active frames filled, inactive frames hold the nearest active value.
    std::vector<unsigned char> set(static_cast<std::size_t>(frames), 0);
    for (std::size_t i = 0; i < track.frames.size(); ++i)
    {
        const auto f = static_cast<std::size_t>(track.frames[i]);
        result.pitchCents[f] = track.cents[i];
        result.carrierCents[f] = carrier[i];
        set[f] = 1;
    }
    for (int f = 1; f < frames; ++f)
        if (!set[static_cast<std::size_t>(f)] && set[static_cast<std::size_t>(f - 1)])
        {
            result.pitchCents[static_cast<std::size_t>(f)] = result.pitchCents[static_cast<std::size_t>(f - 1)];
            result.carrierCents[static_cast<std::size_t>(f)] = result.carrierCents[static_cast<std::size_t>(f - 1)];
            set[static_cast<std::size_t>(f)] = 1;
        }
    for (int f = frames - 2; f >= 0; --f)
        if (!set[static_cast<std::size_t>(f)] && set[static_cast<std::size_t>(f + 1)])
        {
            result.pitchCents[static_cast<std::size_t>(f)] = result.pitchCents[static_cast<std::size_t>(f + 1)];
            result.carrierCents[static_cast<std::size_t>(f)] = result.carrierCents[static_cast<std::size_t>(f + 1)];
            set[static_cast<std::size_t>(f)] = 1;
        }
    return result;
}

AtsSoundPtr applyVibrato(const ATS_SOUND* sound, const VibratoParams& params)
{
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0 || !params.active())
        return {};
    auto* out = copy_sound(const_cast<ATS_SOUND*>(sound));
    if (out == nullptr)
        return {};
    AtsSoundPtr result(out);

    const int frames = sound->frames;
    const double hop = hopSeconds(sound);
    const double depth = std::clamp(params.depth, 0.0, 2.0);
    const double rateScale = std::clamp(params.rateScale, 0.25, 4.0);
    const double top = sound->srate > 0 ? 0.49 * sound->srate : 20000.0;

    VibratoAnalysisParams ap;
    ap.carrierWindowSeconds = params.carrierWindowSeconds;
    const auto analysis = analyseVibrato(sound, ap);
    const int win = oddFrames(params.carrierWindowSeconds, hop);
    const double rate = analysis.rateHz;

    const auto resample = [&](const std::vector<double>& m) {
        if (rateScale == 1.0)
            return m;
        const auto L = m.size();
        std::vector<double> r(L);
        for (std::size_t i = 0; i < L; ++i)
        {
            const double pos = std::fmod(static_cast<double>(i) * rateScale, static_cast<double>(L));
            const auto i0 = static_cast<std::size_t>(std::floor(pos));
            const std::size_t i1 = (i0 + 1) % L;
            const double t = pos - static_cast<double>(i0);
            r[i] = m[i0] + t * (m[i1] - m[i0]);
        }
        return r;
    };

    std::vector<int> idx;
    std::vector<double> frq, amp;
    double frqmax = 0.0;
    for (int p = 0; p < sound->partials; ++p)
    {
        idx.clear();
        frq.clear();
        amp.clear();
        for (int f = 0; f < frames; ++f)
            if (sound->amp[p][f] > 0.0 && sound->frq[p][f] > 0.0)
            {
                idx.push_back(f);
                frq.push_back(sound->frq[p][f]);
                amp.push_back(sound->amp[p][f]);
            }
        if (!idx.empty())
        {
            const auto frqCarrier = carrierOf(frq, win, rate, hop);
            const auto ampCarrier = carrierOf(amp, win, rate, hop);
            std::vector<double> frqMod(idx.size()), ampMod(idx.size());
            for (std::size_t i = 0; i < idx.size(); ++i)
            {
                frqMod[i] = frq[i] - frqCarrier[i];
                ampMod[i] = amp[i] - ampCarrier[i];
            }
            const auto fm = resample(frqMod);
            const auto am = resample(ampMod);
            for (std::size_t i = 0; i < idx.size(); ++i)
            {
                const int f = idx[i];
                double nf = frqCarrier[i] + depth * fm[i];
                if (!(nf > 0.0))
                    nf = frqCarrier[i];
                out->frq[p][f] = std::min(top, nf);
                if (params.scaleAmplitudeModulation)
                    out->amp[p][f] = std::max(0.0, ampCarrier[i] + depth * am[i]);
            }
        }
        for (int f = 0; f < frames; ++f)
        {
            if (out->pha) out->pha[p][f] = 0.0;
            if (out->frq_slope) out->frq_slope[p][f] = 0.0;
            if (out->amp_slope) out->amp_slope[p][f] = 0.0;
            frqmax = std::max(frqmax, out->frq[p][f]);
        }
    }
    out->has_slopes = 0;
    out->frqmax = frqmax;
    return result;
}

} // namespace ats

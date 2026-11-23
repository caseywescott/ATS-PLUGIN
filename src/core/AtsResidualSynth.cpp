#include "AtsResidualSynth.h"

#include <algorithm>
#include <cmath>

namespace ats
{

namespace
{
constexpr double kNoiseVariance = 0.04; // ATSA_NOISE_VARIANCE (synthesis.c)
constexpr double kTwoPi = 6.283185307179586;
constexpr int kMaxCarriers = 64;

std::uint64_t mix64(std::uint64_t z) noexcept
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::uint64_t hash4(std::uint64_t seed, std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept
{
    return mix64(seed ^ mix64(a ^ mix64(b ^ mix64(c + 0x51ED270B27DB2A5Full))));
}

/** Uniform in [0, 1). */
double uniform01(std::uint64_t h) noexcept { return static_cast<double>(h >> 11) * 0x1.0p-53; }

/** Uniform in [-1, 1] like ATSA's randi output. */
double bipolar(std::uint64_t h) noexcept { return 2.0 * uniform01(h) - 1.0; }

double engRms(double energy, int windowSize) noexcept
{
    if (energy <= 0.0)
        return 0.0;
    const double ws = windowSize > 0 ? static_cast<double>(windowSize) : 1.0;
    return std::sqrt(energy / (ws * kNoiseVariance));
}

/** One sinusoid with its own randi amplitude stream (ATSA randi semantics:
    a1 -> a2 ramp over `size` samples, new value every `size` samples). */
struct Carrier
{
    int band = 0;
    int index = 0;
    double phase = 0.0;
    double phaseInc = 0.0;
    int size = 1;
    int cnt = 0;
    std::uint64_t segment = 0;
    double a1 = 0.0;
    double a2 = 0.0;
    bool enabled = true;

    double randiValue(std::uint64_t seed, std::uint64_t seg) const noexcept
    {
        return bipolar(hash4(seed, static_cast<std::uint64_t>(band) + 1, static_cast<std::uint64_t>(index) + 1, seg));
    }

    void setup(std::uint64_t seed, double sampleRate, double freq, double rate)
    {
        phaseInc = kTwoPi * freq / sampleRate;
        phase = kTwoPi * uniform01(hash4(seed, static_cast<std::uint64_t>(band) + 1,
                                         static_cast<std::uint64_t>(index) + 1, 0xFFFFFFFFull));
        size = std::max(1, static_cast<int>(sampleRate / rate) - 1);
        cnt = 0;
        segment = 0;
        a1 = randiValue(seed, 0);
        a2 = randiValue(seed, 1);
        enabled = freq > 0.0 && freq < 0.5 * sampleRate;
    }

    /** Advances one sample; returns sin(phase) * randi. */
    double tick(std::uint64_t seed) noexcept
    {
        if (cnt == size)
        {
            ++segment;
            a1 = a2;
            a2 = randiValue(seed, segment + 1);
            cnt = 0;
        }
        const double noise = a1 + (a2 - a1) * static_cast<double>(cnt) / static_cast<double>(size);
        ++cnt;
        const double s = std::sin(phase);
        phase += phaseInc;
        if (phase >= kTwoPi)
            phase -= kTwoPi;
        return s * noise;
    }
};
} // namespace

double bandReferenceRms(double bandEnergy, int windowSize)
{
    return engRms(bandEnergy, windowSize) / 3.0;
}

std::size_t renderResidualBands(const ATS_SOUND* sound, const ResidualSynthParams& params, std::vector<double>& out)
{
    if (sound == nullptr || sound->band_energy == nullptr || sound->frames <= 0)
        return 0;
    if (!(params.sampleRate > 0.0) || !(params.timeFactor > 0.0) || params.gain == 0.0)
        return 0;

    const int frames = sound->frames;
    const double hopSec = sound->frame_size > 0 && sound->srate > 0
                              ? static_cast<double>(sound->frame_size) / sound->srate
                              : (sound->dur > 0.0 ? sound->dur / frames : 0.01);
    const double modelLength = frames * hopSec;
    const double start = std::max(0.0, params.startTime);
    const double duration = params.duration > 0.0 ? params.duration : modelLength - start;
    if (duration <= 0.0)
        return 0;
    const auto samples = static_cast<std::size_t>(std::llround(duration * params.timeFactor * params.sampleRate));
    if (samples == 0)
        return 0;
    if (out.size() < samples)
        out.resize(samples, 0.0);

    const int density = std::clamp(params.carriersPerBand, 1, kMaxCarriers);
    const double perCarrier = params.gain / std::sqrt(static_cast<double>(density));

    // Carriers: band-major so the stream for (band, k) never depends on density of other bands.
    std::vector<Carrier> carriers(static_cast<std::size_t>(ATSA_CRITICAL_BANDS * density));
    for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
    {
        const double low = ATSA_CRITICAL_BAND_EDGES[b];
        const double high = ATSA_CRITICAL_BAND_EDGES[b + 1];
        const double width = high - low;
        const double rate = std::max(0.01, width / density);
        for (int k = 0; k < density; ++k)
        {
            auto& c = carriers[static_cast<std::size_t>(b * density + k)];
            c.band = b;
            c.index = k;
            const double jitter = density == 1
                                      ? 0.0
                                      : 0.6 * (uniform01(hash4(params.seed, static_cast<std::uint64_t>(b) + 1,
                                                                static_cast<std::uint64_t>(k) + 1, 0xFFFFFFFEull)) - 0.5);
            const double freq = low + (k + 0.5 + jitter) / density * width;
            c.setup(params.seed, params.sampleRate, freq, rate);
        }
    }

    // Optional per-frame mask: band b holds an active partial in frame f.
    std::vector<unsigned char> mask;
    if (params.skipBandsWithPartials)
    {
        mask.assign(static_cast<std::size_t>(frames) * ATSA_CRITICAL_BANDS, 0);
        for (int f = 0; f < frames; ++f)
            for (int p = 0; p < sound->partials; ++p)
            {
                const double frq = sound->frq[p][f];
                if (sound->amp[p][f] <= 0.0 || frq <= 0.0)
                    continue;
                const auto* edge = std::upper_bound(ATSA_CRITICAL_BAND_EDGES, ATSA_CRITICAL_BAND_EDGES + ATSA_CRITICAL_BANDS + 1, frq);
                const int b = static_cast<int>(edge - ATSA_CRITICAL_BAND_EDGES) - 1;
                if (b >= 0 && b < ATSA_CRITICAL_BANDS)
                    mask[static_cast<std::size_t>(f) * ATSA_CRITICAL_BANDS + b] = 1;
            }
    }

    std::vector<double> bandAmp(static_cast<std::size_t>(ATSA_CRITICAL_BANDS));
    const double invSr = 1.0 / params.sampleRate;
    for (std::size_t n = 0; n < samples; ++n)
    {
        const double tModel = start + static_cast<double>(n) * invSr / params.timeFactor;
        const double ff = hopSec > 0.0 ? tModel / hopSec : 0.0;
        int fi = static_cast<int>(std::floor(ff));
        double frac = ff - fi;
        if (fi < 0) { fi = 0; frac = 0.0; }
        if (fi > frames - 1) { fi = frames - 1; frac = 0.0; }
        const int fn = std::min(frames - 1, fi + 1);

        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        {
            const bool skip0 = !mask.empty() && mask[static_cast<std::size_t>(fi) * ATSA_CRITICAL_BANDS + b] != 0;
            const bool skip1 = !mask.empty() && mask[static_cast<std::size_t>(fn) * ATSA_CRITICAL_BANDS + b] != 0;
            const double e0 = skip0 ? 0.0 : engRms(sound->band_energy[b][fi], sound->window_size);
            const double e1 = skip1 ? 0.0 : engRms(sound->band_energy[b][fn], sound->window_size);
            bandAmp[static_cast<std::size_t>(b)] = (e0 + frac * (e1 - e0)) * perCarrier;
        }

        double acc = 0.0;
        for (auto& c : carriers)
        {
            const double v = c.tick(params.seed); // always advance: masking must not shift the noise stream
            if (c.enabled)
                acc += bandAmp[static_cast<std::size_t>(c.band)] * v;
        }
        out[n] += acc;
    }
    return samples;
}

} // namespace ats

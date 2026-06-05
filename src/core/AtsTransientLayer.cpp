#include "AtsTransientLayer.h"

#include <algorithm>
#include <cmath>

namespace ats
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279;
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Zwicker & Fastl critical-band edges (matches ATSA_CRITICAL_BAND_EDGES and
// AtsTransientSmear's table). Index b spans [edges[b], edges[b+1]).
constexpr double kBandEdges[ATSA_CRITICAL_BANDS + 1] = {
    0.0, 100.0, 200.0, 300.0, 400.0, 510.0, 630.0, 770.0, 920.0,
    1080.0, 1270.0, 1480.0, 1720.0, 2000.0, 2320.0, 2700.0, 3150.0,
    3700.0, 4400.0, 5300.0, 6400.0, 7700.0, 9500.0, 12000.0, 15500.0, 20000.0
};

bool finitePositive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

double positiveFinite(double v)
{
    return std::isfinite(v) && v > 0.0 ? v : 0.0;
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

double frameTimeSeconds(const ATS_SOUND* s, int frame, double hop)
{
    if (s->time != nullptr && s->time[0] != nullptr && frame >= 0 && frame < s->frames)
    {
        const double t = s->time[0][frame];
        if (std::isfinite(t) && t >= 0.0)
            return t;
    }
    return (double) frame * hop;
}

double partialEnergyAtFrame(const ATS_SOUND* s, int f)
{
    double e = 0.0;
    if (s->amp == nullptr)
        return 0.0;
    for (int p = 0; p < s->partials; ++p)
    {
        if (s->amp[p] == nullptr)
            continue;
        const double a = positiveFinite(s->amp[p][f]);
        e += a * a;
    }
    return e;
}

double bandEnergyAtFrame(const ATS_SOUND* s, int b, int f)
{
    if (s->band_energy == nullptr || s->band_energy[b] == nullptr)
        return 0.0;
    return positiveFinite(s->band_energy[b][f]);
}

// Net rise across [pre, peak], clamped to the in-window maximum so we never
// inject more energy than the model already carries in the attack.
double boundedRise(double pre, double peak, double inWindowMax)
{
    const double rise = std::max(0.0, peak - pre);
    return std::min(rise, std::max(0.0, inWindowMax));
}

// Deterministic LCG (Numerical Recipes constants) for reproducible phases.
struct Lcg
{
    unsigned int state;
    explicit Lcg(unsigned int seed) : state(seed ? seed : 1u) {}
    double next01()
    {
        state = 1664525u * state + 1013904223u;
        return (double) state / 4294967296.0;
    }
};
} // namespace

double transientEventEnergy(const AtsTransientEvent& event)
{
    double e = positiveFinite(event.deterministicEnergy);
    for (double b : event.bandEnergy)
        e += positiveFinite(b);
    return e;
}

std::vector<AtsTransientEvent> extractTransientLayer(const ATS_SOUND* sound,
                                                     const std::vector<AtsOnset>& onsets)
{
    std::vector<AtsTransientEvent> events;
    if (sound == nullptr || sound->frames < 2 || onsets.empty())
        return events;

    const int frames = sound->frames;
    const double hop = hopSeconds(sound);
    const bool hasResidual = sound->band_energy != nullptr;
    const bool hasPartials = sound->amp != nullptr && sound->partials > 0;

    events.reserve(onsets.size());
    for (const auto& onset : onsets)
    {
        int pre = std::clamp(std::min(onset.preFrame, onset.peakFrame), 0, frames - 1);
        int peak = std::clamp(std::max(onset.preFrame, onset.peakFrame), 0, frames - 1);
        if (peak < pre)
            std::swap(pre, peak);

        AtsTransientEvent ev;
        ev.frameIndex = std::clamp(onset.frame, 0, frames - 1);
        ev.preFrame = pre;
        ev.peakFrame = peak;
        ev.timeSeconds = frameTimeSeconds(sound, ev.frameIndex, hop);
        ev.windowSeconds = (double) (peak - pre) * hop;
        ev.strength = onset.strength;

        if (hasResidual)
        {
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            {
                double maxIn = 0.0;
                for (int f = pre; f <= peak; ++f)
                    maxIn = std::max(maxIn, bandEnergyAtFrame(sound, b, f));
                ev.bandEnergy[(size_t) b] = boundedRise(bandEnergyAtFrame(sound, b, pre),
                                                        bandEnergyAtFrame(sound, b, peak),
                                                        maxIn);
            }
        }

        if (hasPartials)
        {
            double maxIn = 0.0;
            for (int f = pre; f <= peak; ++f)
                maxIn = std::max(maxIn, partialEnergyAtFrame(sound, f));
            ev.deterministicEnergy = boundedRise(partialEnergyAtFrame(sound, pre),
                                                 partialEnergyAtFrame(sound, peak),
                                                 maxIn);
        }

        events.push_back(ev);
    }

    return events;
}

bool renderTransientLayer(const std::vector<AtsTransientEvent>& events,
                          const TransientLayerParams& params,
                          int sampleRate,
                          double* out,
                          int nSamples)
{
    if (out == nullptr || nSamples <= 0 || sampleRate <= 0)
        return false;

    std::fill(out, out + nSamples, 0.0);
    if (events.empty())
        return true;

    const double sr = (double) sampleRate;
    const double nyquist = 0.45 * sr; // leave headroom below sr/2 to avoid aliasing
    const double gain = std::clamp(std::isfinite(params.gain) ? params.gain : 1.0, 0.0, 1.0);
    if (gain <= 0.0)
        return true;

    const int attackSamples = std::max(1, (int) std::lround(std::max(0.0, params.attackSeconds) * sr));
    const int maxDecaySamples = std::max(1, (int) std::lround(std::max(0.0, params.maxDecaySeconds) * sr));

    constexpr int kLinesPerBand = 4;
    std::vector<double> burst;

    for (const auto& ev : events)
    {
        const double targetEnergy = gain * transientEventEnergy(ev);
        if (!(targetEnergy > 0.0))
            continue;

        const int onsetSample = (int) std::lround(ev.timeSeconds * sr);
        int decaySamples = (int) std::lround(std::max(0.0, ev.windowSeconds) * sr);
        decaySamples = std::clamp(decaySamples, 1, maxDecaySamples);

        const int begin = onsetSample - attackSamples;
        const int end = onsetSample + decaySamples; // exclusive upper bound
        const int len = end - begin;
        if (len <= 0)
            continue;

        // Per-event deterministic phases so overlapping events stay independent.
        Lcg rng(params.seed ^ (unsigned int) (ev.frameIndex * 2654435761u));

        // Gather the band carriers (frequency + relative amplitude). Band energy
        // sets the squared amplitude, so the burst's spectral envelope follows
        // the attack's Bark profile; deterministic energy rides as a flat term.
        struct Line { double freq; double amp; double phase; };
        std::vector<Line> lines;
        lines.reserve((size_t) (ATSA_CRITICAL_BANDS * kLinesPerBand + kLinesPerBand));

        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        {
            const double be = positiveFinite(ev.bandEnergy[(size_t) b]);
            if (be <= 0.0)
                continue;
            const double lo = kBandEdges[b];
            const double hi = std::min(kBandEdges[b + 1], nyquist);
            if (hi <= lo)
                continue;
            // Split the band energy across kLinesPerBand spectral lines.
            const double ampPerLine = std::sqrt(be / (double) kLinesPerBand);
            for (int k = 0; k < kLinesPerBand; ++k)
            {
                const double frac = ((double) k + 0.5) / (double) kLinesPerBand;
                lines.push_back({ lo + frac * (hi - lo), ampPerLine, rng.next01() * kTwoPi });
            }
        }

        // Deterministic (partial) attack energy → a few broadband lines.
        const double detE = positiveFinite(ev.deterministicEnergy);
        if (detE > 0.0)
        {
            const double ampPerLine = std::sqrt(detE / (double) kLinesPerBand);
            for (int k = 0; k < kLinesPerBand; ++k)
            {
                const double frac = ((double) k + 0.5) / (double) kLinesPerBand;
                lines.push_back({ 200.0 + frac * (nyquist - 200.0), ampPerLine, rng.next01() * kTwoPi });
            }
        }

        if (lines.empty())
            continue;

        burst.assign((size_t) len, 0.0);
        for (int i = 0; i < len; ++i)
        {
            const int n = begin + i;
            // Raised-cosine attack to the onset peak, raised-cosine decay after.
            double envv;
            if (n < onsetSample)
            {
                const double x = (double) (n - begin) / (double) attackSamples; // 0..1
                envv = 0.5 * (1.0 - std::cos(kPi * x));
            }
            else
            {
                const double x = (double) (n - onsetSample) / (double) decaySamples; // 0..1
                envv = 0.5 * (1.0 + std::cos(kPi * std::min(1.0, x)));
            }

            double carrier = 0.0;
            for (const auto& ln : lines)
                carrier += ln.amp * std::cos(kTwoPi * ln.freq * (double) n / sr + ln.phase);
            burst[(size_t) i] = envv * carrier;
        }

        // Normalize the burst so its total sample energy equals targetEnergy.
        double sumSq = 0.0;
        for (double v : burst)
            sumSq += v * v;
        if (!(sumSq > 0.0))
            continue;
        const double scale = std::sqrt(targetEnergy / sumSq);

        for (int i = 0; i < len; ++i)
        {
            const int n = begin + i;
            if (n < 0 || n >= nSamples)
                continue;
            out[n] += scale * burst[(size_t) i];
        }
    }

    return true;
}

} // namespace ats

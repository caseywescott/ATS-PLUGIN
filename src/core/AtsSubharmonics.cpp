#include "AtsSubharmonics.h"

#include "AtsAutomationCurves.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ats
{

namespace
{

constexpr double kMinHz = 20.0;
constexpr double kJitterCutoffHz = 20.0;

/** splitmix64 finaliser over (seed, partial, frame) -> uniform in [-1, 1]. */
double hashNoise(std::uint64_t seed, int partial, int frame)
{
    std::uint64_t z = seed ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(partial)) << 32)
                      ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(frame));
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

struct NewPartial
{
    std::vector<double> frq;
    std::vector<double> amp;
    std::vector<double> smr;
    bool any = false;
};

double lerpDb(double a, double b, double t)
{
    const auto dbA = 20.0 * std::log10(std::max(a, 1.0e-30));
    const auto dbB = 20.0 * std::log10(std::max(b, 1.0e-30));
    return std::pow(10.0, (dbA + t * (dbB - dbA)) / 20.0);
}

} // namespace

SubharmonicResult applySubharmonics(const ATS_SOUND* sound, const SubharmonicParams& params)
{
    SubharmonicResult result;
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0)
        return result;
    const auto frames = static_cast<std::size_t>(sound->frames);
    const auto f0 = params.fundamentalHz > 0.0 ? params.fundamentalHz
                                               : AtsAutomationCurves::estimateFundamental(sound);
    if (!(f0 > 0.0))
        return result;
    const auto gain = std::pow(10.0, params.gainDb / 20.0);

    std::vector<NewPartial> added;

    if (params.mode == SubharmonicMode::rough)
    {
        const auto divisor = std::max(2, params.divisor);
        const auto hop = sound->srate > 0 ? static_cast<double>(sound->frame_size) / sound->srate : 0.01;
        const auto alpha = std::clamp(1.0 - std::exp(-2.0 * 3.141592653589793 * kJitterCutoffHz * hop), 0.0, 1.0);
        const auto jitterF = std::max(0.0, params.periodJitterPercent) / 100.0;
        const auto jitterDb = std::max(0.0, params.ampJitterDb);
        const auto low = f0 * 0.99; // tolerance for a fundamental that wobbles around the estimate
        for (int p = 0; p < sound->partials; ++p)
        {
            NewPartial np;
            np.frq.assign(frames, 0.0);
            np.amp.assign(frames, 0.0);
            np.smr.assign(frames, 0.0);
            double noiseF = 0.0, noiseA = 0.0;
            for (std::size_t f = 0; f < frames; ++f)
            {
                const auto fi = static_cast<int>(f);
                noiseF += alpha * (hashNoise(params.seed, p, fi) - noiseF);
                noiseA += alpha * (hashNoise(params.seed ^ 0x5bd1e995ULL, p, fi) - noiseA);
                const auto srcF = sound->frq[p][f];
                const auto srcA = sound->amp[p][f];
                if (!(srcA > 0.0) || !(srcF >= low) || srcF > params.maxHz)
                    continue;
                const auto frq = (srcF / divisor) * (1.0 + jitterF * noiseF);
                if (frq < kMinHz)
                    continue;
                np.frq[f] = frq;
                np.amp[f] = srcA * gain * std::pow(10.0, jitterDb * noiseA / 20.0);
                np.smr[f] = sound->smr ? sound->smr[p][f] : 0.0;
                np.any = true;
            }
            if (np.any)
                added.push_back(std::move(np));
        }
    }
    else
    {
        const auto order = std::clamp(params.growlOrder, 2, 5);
        // Harmonic index per partial per frame; the loudest partial claims an index.
        std::vector<std::vector<int>> owner(frames); // owner[f][h] = partial or -1
        int hMax = 0;
        for (std::size_t f = 0; f < frames; ++f)
            for (int p = 0; p < sound->partials; ++p)
                if (sound->amp[p][f] > 0.0 && sound->frq[p][f] > 0.0)
                    hMax = std::max(hMax, static_cast<int>(std::lround(sound->frq[p][f] / f0)));
        if (hMax < 2)
            return result;
        for (std::size_t f = 0; f < frames; ++f)
        {
            owner[f].assign(static_cast<std::size_t>(hMax) + 1, -1);
            for (int p = 0; p < sound->partials; ++p)
            {
                if (!(sound->amp[p][f] > 0.0) || !(sound->frq[p][f] > 0.0))
                    continue;
                const auto h = static_cast<std::size_t>(std::max(1L, std::lround(sound->frq[p][f] / f0)));
                auto& slot = owner[f][h];
                if (slot < 0 || sound->amp[p][f] > sound->amp[slot][f])
                    slot = p;
            }
        }
        for (int m = 1; m < hMax; ++m)
            for (int k = 1; k < order; ++k)
            {
                NewPartial np;
                np.frq.assign(frames, 0.0);
                np.amp.assign(frames, 0.0);
                np.smr.assign(frames, 0.0);
                const auto t = static_cast<double>(k) / order;
                for (std::size_t f = 0; f < frames; ++f)
                {
                    const auto lo = owner[f][static_cast<std::size_t>(m)];
                    const auto hi = owner[f][static_cast<std::size_t>(m) + 1];
                    if (lo < 0 || hi < 0)
                        continue;
                    const auto frq = sound->frq[lo][f] + t * (sound->frq[hi][f] - sound->frq[lo][f]);
                    if (frq < kMinHz)
                        continue;
                    np.frq[f] = frq;
                    np.amp[f] = lerpDb(sound->amp[lo][f], sound->amp[hi][f], t) * gain;
                    np.smr[f] = sound->smr ? std::min(sound->smr[lo][f], sound->smr[hi][f]) : 0.0;
                    np.any = true;
                }
                if (np.any)
                    added.push_back(std::move(np));
            }
    }

    if (added.empty())
        return result;

    const auto total = sound->partials + static_cast<int>(added.size());
    auto* out = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    if (out == nullptr)
        return result;
    init_sound(out, sound->srate, sound->frame_size, sound->window_size, sound->frames, sound->dur, total,
               sound->band_energy != nullptr ? 1 : 0);
    result.sound.reset(out);
    out->optimized = sound->optimized;
    out->has_slopes = 0;

    const auto bytes = frames * sizeof(double);
    std::memcpy(out->time[0], sound->time[0], bytes);
    for (int p = 1; p < total; ++p)
        std::memcpy(out->time[p], sound->time[0], bytes);
    for (int p = 0; p < sound->partials; ++p)
    {
        std::memcpy(out->frq[p], sound->frq[p], bytes);
        std::memcpy(out->amp[p], sound->amp[p], bytes);
        if (sound->pha) std::memcpy(out->pha[p], sound->pha[p], bytes);
        if (sound->smr) std::memcpy(out->smr[p], sound->smr[p], bytes);
        if (sound->res) std::memcpy(out->res[p], sound->res[p], bytes);
    }
    for (std::size_t i = 0; i < added.size(); ++i)
    {
        const auto p = sound->partials + static_cast<int>(i);
        std::memcpy(out->frq[p], added[i].frq.data(), bytes);
        std::memcpy(out->amp[p], added[i].amp.data(), bytes);
        std::memcpy(out->smr[p], added[i].smr.data(), bytes);
    }
    if (out->band_energy != nullptr)
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            std::memcpy(out->band_energy[b], sound->band_energy[b], bytes);

    double ampmax = 0.0, frqmax = 0.0;
    for (int p = 0; p < total; ++p)
    {
        double sumF = 0.0, sumA = 0.0;
        int n = 0;
        for (std::size_t f = 0; f < frames; ++f)
        {
            ampmax = std::max(ampmax, out->amp[p][f]);
            frqmax = std::max(frqmax, out->frq[p][f]);
            if (out->amp[p][f] > 0.0) { sumF += out->frq[p][f]; sumA += out->amp[p][f]; ++n; }
        }
        if (out->av != nullptr)
        {
            if (p < sound->partials && sound->av != nullptr)
                out->av[p] = sound->av[p];
            else
            {
                out->av[p] = ATS_PEAK {};
                out->av[p].frq = n > 0 ? sumF / n : 0.0;
                out->av[p].amp = n > 0 ? sumA / n : 0.0;
                out->av[p].track = p;
            }
        }
    }
    out->ampmax = ampmax;
    out->frqmax = frqmax;
    result.addedPartials = static_cast<int>(added.size());
    return result;
}

} // namespace ats

#include "AtsSpectralSmoothness.h"

#include "AtsAutomationCurves.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ats
{

namespace
{
struct Harmonic
{
    int partial;
    double logH;
    double amp;
};
} // namespace

AtsSoundPtr applySpectralSmoothness(const ATS_SOUND* sound, const SpectralSmoothnessParams& params)
{
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0)
        return {};
    if (!(params.strength > 0.0) || !std::isfinite(params.strength))
        return {};
    const auto strength = std::min(1.0, params.strength);
    const auto f0 = params.fundamentalHz > 0.0 ? params.fundamentalHz
                                               : AtsAutomationCurves::estimateFundamental(sound);
    if (!(f0 > 0.0))
        return {};
    const auto minRatio = std::pow(10.0, -std::max(0.0, params.maxReductionDb) / 20.0);

    auto* out = copy_sound(const_cast<ATS_SOUND*>(sound));
    if (out == nullptr)
        return {};
    AtsSoundPtr result(out);

    std::vector<Harmonic> harmonics;
    harmonics.reserve(static_cast<std::size_t>(sound->partials));
    double ampmax = 0.0;
    for (int f = 0; f < sound->frames; ++f)
    {
        harmonics.clear();
        for (int p = 0; p < sound->partials; ++p)
        {
            const auto a = sound->amp[p][f];
            const auto q = sound->frq[p][f];
            if (a > 0.0 && q > 0.0)
            {
                const auto h = std::max(1L, std::lround(q / f0));
                harmonics.push_back({p, std::log2(static_cast<double>(h)), a});
            }
        }
        for (const auto& me : harmonics)
        {
            double wLo = 0.0, xLo = 0.0, aLo = 0.0;
            double wHi = 0.0, xHi = 0.0, aHi = 0.0;
            for (const auto& other : harmonics)
            {
                if (other.partial == me.partial)
                    continue;
                const auto d = other.logH - me.logH;
                const auto ad = std::abs(d);
                if (ad <= 0.0 || ad >= 1.0)
                    continue;
                const auto w = 1.0 - ad;
                if (d < 0.0) { wLo += w; xLo += w * other.logH; aLo += w * other.amp; }
                else         { wHi += w; xHi += w * other.logH; aHi += w * other.amp; }
            }
            if (wLo <= 0.0 || wHi <= 0.0)
                continue;
            xLo /= wLo; aLo /= wLo;
            xHi /= wHi; aHi /= wHi;
            const auto dbLo = 20.0 * std::log10(std::max(aLo, 1.0e-30));
            const auto dbHi = 20.0 * std::log10(std::max(aHi, 1.0e-30));
            const auto t = xHi > xLo ? std::clamp((me.logH - xLo) / (xHi - xLo), 0.0, 1.0) : 0.5;
            const auto reference = std::pow(10.0, (dbLo + t * (dbHi - dbLo)) / 20.0);
            const auto target = std::min(me.amp, reference);
            auto amp = me.amp + strength * (target - me.amp);
            amp = std::max(amp, me.amp * minRatio);
            out->amp[me.partial][f] = amp;
        }
        for (int p = 0; p < sound->partials; ++p)
            ampmax = std::max(ampmax, out->amp[p][f]);
    }
    out->ampmax = ampmax;
    if (out->av != nullptr)
        for (int p = 0; p < out->partials; ++p)
        {
            double sumA = 0.0;
            int n = 0;
            for (int f = 0; f < out->frames; ++f)
                if (out->amp[p][f] > 0.0) { sumA += out->amp[p][f]; ++n; }
            out->av[p].amp = n > 0 ? sumA / n : 0.0;
        }
    return result;
}

} // namespace ats

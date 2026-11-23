#include "AtsTimeEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace ats
{
namespace
{
constexpr double kHalfPi = 1.57079632679489661923;

// A source frame (or -1 for none) per output frame.
using FrameMap = std::vector<int>;

AtsSoundPtr build(const ATS_SOUND* a, const FrameMap& fromA, const ATS_SOUND* b = nullptr,
                  const FrameMap* fromB = nullptr)
{
    const auto frames = static_cast<int>(fromA.size());
    if (a == nullptr || frames <= 0)
        return {};
    auto* out = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    if (out == nullptr)
        return {};
    const auto hop = a->srate > 0 ? static_cast<double>(a->frame_size) / a->srate : 0.0;
    init_sound(out, a->srate, a->frame_size, a->window_size, frames, hop * frames, a->partials,
               a->band_energy != nullptr ? 1 : 0);
    out->has_slopes = a->has_slopes && (b == nullptr || b->has_slopes);
    for (int f = 0; f < frames; ++f)
    {
        out->time[0][f] = hop * f;
        const ATS_SOUND* src = a;
        int sf = fromA[static_cast<std::size_t>(f)];
        if (sf < 0 && b != nullptr && fromB != nullptr)
        {
            src = b;
            sf = (*fromB)[static_cast<std::size_t>(f)];
        }
        for (int p = 0; p < out->partials; ++p)
        {
            const auto take = [&](double** arr) { return sf >= 0 && arr && arr[p] ? arr[p][sf] : 0.0; };
            out->amp[p][f] = take(src->amp);
            out->frq[p][f] = take(src->frq);
            out->pha[p][f] = take(src->pha);
            out->smr[p][f] = take(src->smr);
            out->res[p][f] = take(src->res);
            if (out->frq_slope) out->frq_slope[p][f] = out->has_slopes ? take(src->frq_slope) : 0.0;
            if (out->amp_slope) out->amp_slope[p][f] = out->has_slopes ? take(src->amp_slope) : 0.0;
        }
        if (out->band_energy != nullptr)
            for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
                out->band_energy[band][f] = sf >= 0 && src->band_energy && src->band_energy[band]
                    ? src->band_energy[band][sf] : 0.0;
    }
    double ampmax = 0.0, frqmax = 0.0;
    for (int p = 0; p < out->partials; ++p)
        for (int f = 0; f < frames; ++f)
        {
            ampmax = std::max(ampmax, out->amp[p][f]);
            frqmax = std::max(frqmax, out->frq[p][f]);
        }
    out->ampmax = ampmax;
    out->frqmax = frqmax;
    out->optimized = a->optimized;
    set_av(out);
    return AtsSoundPtr(out);
}

bool clampRange(const ATS_SOUND* sound, int& first, int& last)
{
    if (sound == nullptr || sound->frames <= 0)
        return false;
    if (first > last)
        std::swap(first, last);
    first = std::clamp(first, 0, sound->frames - 1);
    last = std::clamp(last, 0, sound->frames - 1);
    return true;
}
} // namespace

AtsSoundPtr AtsTimeEdit::copy(const ATS_SOUND* sound, int first, int last)
{
    if (!clampRange(sound, first, last))
        return {};
    FrameMap map;
    for (int f = first; f <= last; ++f)
        map.push_back(f);
    return build(sound, map);
}

AtsSoundPtr AtsTimeEdit::remove(const ATS_SOUND* sound, int first, int last)
{
    if (!clampRange(sound, first, last))
        return {};
    FrameMap map;
    for (int f = 0; f < sound->frames; ++f)
        if (f < first || f > last)
            map.push_back(f);
    return build(sound, map);
}

AtsSoundPtr AtsTimeEdit::paste(const ATS_SOUND* sound, const ATS_SOUND* clip, int atFrame,
                               int crossfadeFrames)
{
    if (sound == nullptr || clip == nullptr || clip->frames <= 0 || sound->partials != clip->partials)
        return {};
    const auto n = sound->frames;
    const auto at = std::clamp(atFrame, 0, n);
    const auto c = clip->frames;
    FrameMap fromSound, fromClip;
    for (int f = 0; f < at; ++f) { fromSound.push_back(f); fromClip.push_back(-1); }
    for (int f = 0; f < c; ++f) { fromSound.push_back(-1); fromClip.push_back(f); }
    for (int f = at; f < n; ++f) { fromSound.push_back(f); fromClip.push_back(-1); }
    auto result = build(sound, fromSound, clip, &fromClip);
    if (!result)
        return result;
    auto* out = result.get();

    const auto fade = std::clamp(crossfadeFrames, 0, c / 2);
    const auto blend = [&](int frame, int neighbour, double toward) {
        // toward: weight of the neighbour (0 = keep the clip frame).
        const auto gOut = std::cos((1.0 - toward) * kHalfPi); // neighbour gain (equal power)
        const auto gIn = std::sin((1.0 - toward) * kHalfPi);  // clip gain
        for (int p = 0; p < out->partials; ++p)
        {
            const auto na = sound->amp[p][neighbour], nf = sound->frq[p][neighbour];
            const auto ca = out->amp[p][frame], cf = out->frq[p][frame];
            out->amp[p][frame] = gOut * na + gIn * ca;
            if (na > 0.0 && nf > 0.0 && ca > 0.0 && cf > 0.0)
                out->frq[p][frame] = toward * nf + (1.0 - toward) * cf;
            else if (!(ca > 0.0 && cf > 0.0) && na > 0.0 && nf > 0.0)
                out->frq[p][frame] = nf; // the clip is silent here: keep the neighbour's pitch
        }
        if (out->band_energy != nullptr && sound->band_energy != nullptr)
            for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
                out->band_energy[band][frame] = toward * sound->band_energy[band][neighbour]
                    + (1.0 - toward) * out->band_energy[band][frame];
    };
    for (int k = 0; k < fade; ++k)
    {
        const auto toward = 1.0 - static_cast<double>(k + 1) / (fade + 1); // fades from the neighbour
        if (at > 0)
            blend(at + k, at - 1, toward);            // seam 1: neighbour -> clip
        if (at < n)
            blend(at + c - 1 - k, at, toward);        // seam 2: clip -> neighbour
    }
    if (fade > 0)
        set_av(out);
    return result;
}

} // namespace ats

#include "AtsAutomationCurves.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ats
{

double AutomationCurve::valueAt(double x, double identity) const noexcept
{
    if (points.empty())
        return identity;
    if (x <= points.front().x)
        return points.front().y;
    if (x >= points.back().x)
        return points.back().y;
    for (std::size_t i = 1; i < points.size(); ++i)
        if (x <= points[i].x)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const auto span = static_cast<double>(b.x) - a.x;
            const auto t = span > 0.0 ? (x - a.x) / span : 0.0;
            return a.y + t * (static_cast<double>(b.y) - a.y);
        }
    return points.back().y;
}

void AutomationCurve::normalise(float minY, float maxY)
{
    points.erase(std::remove_if(points.begin(), points.end(),
                                [](const Point& p) { return !std::isfinite(p.x) || !std::isfinite(p.y); }),
                 points.end());
    for (auto& p : points)
    {
        p.x = std::clamp(p.x, 0.0f, 1.0f);
        p.y = std::clamp(p.y, minY, maxY);
    }
    std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
    if (points.size() > kMaxPoints)
        points.resize(kMaxPoints);
}

bool SoundCurves::active() const noexcept
{
    const auto varies = [](const AutomationCurve& curve, double identity) {
        for (const auto& p : curve.points)
            if (std::abs(p.y - identity) > 1.0e-6)
                return true;
        return false;
    };
    bool warped = false;
    for (const auto& p : timeWarp.points)
        warped = warped || std::abs(p.y - p.x) > 1.0e-6;
    // An identity time warp needs points at both ends on the diagonal.
    if (!timeWarp.empty() && !warped)
        warped = timeWarp.points.front().x > 1.0e-6 || timeWarp.points.back().x < 1.0f - 1.0e-6;
    return warped || varies(pitchSemitones, 0.0) || varies(spectralWarpGamma, 1.0);
}

void SoundCurves::normalise()
{
    timeWarp.normalise(0.0f, 1.0f);
    pitchSemitones.normalise(-kMaxSemitones, kMaxSemitones);
    spectralWarpGamma.normalise(kMinGamma, kMaxGamma);
}

double AtsAutomationCurves::estimateFundamental(const ATS_SOUND* sound)
{
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0)
        return 0.0;
    std::vector<double> lows;
    for (int f = 0; f < sound->frames; ++f)
    {
        double low = 0.0;
        for (int p = 0; p < sound->partials; ++p)
            if (sound->amp[p][f] > 0.0 && sound->frq[p][f] > 0.0 && (low == 0.0 || sound->frq[p][f] < low))
                low = sound->frq[p][f];
        if (low > 0.0)
            lows.push_back(low);
    }
    if (lows.empty())
        return 0.0;
    std::nth_element(lows.begin(), lows.begin() + static_cast<std::ptrdiff_t>(lows.size() / 2), lows.end());
    return lows[lows.size() / 2];
}

AtsSoundPtr AtsAutomationCurves::apply(const ATS_SOUND* sound, const SoundCurves& input)
{
    if (sound == nullptr || sound->frames <= 0 || !input.active())
        return {};
    auto curves = input;
    curves.normalise();
    auto* out = copy_sound(const_cast<ATS_SOUND*>(sound));
    if (out == nullptr)
        return {};
    AtsSoundPtr result(out);
    const auto frames = sound->frames;
    const auto last = std::max(1, frames - 1);
    const auto anchor = curves.warpAnchorHz > 0.0 ? curves.warpAnchorHz : estimateFundamental(sound);
    const auto top = sound->srate > 0 ? 0.49 * sound->srate : 20000.0;

    for (int f = 0; f < frames; ++f)
    {
        const auto u = frames > 1 ? static_cast<double>(f) / last : 0.0;
        // Time warp: read the analysis frame at the warped position.
        const auto position = std::clamp(curves.timeWarp.valueAt(u, u), 0.0, 1.0) * last;
        const auto f0 = std::min(frames - 1, static_cast<int>(std::floor(position)));
        const auto f1 = std::min(frames - 1, f0 + 1);
        const auto t = position - f0;
        const auto ratio = std::pow(2.0, curves.pitchSemitones.valueAt(u, 0.0) / 12.0);
        const auto gamma = curves.spectralWarpGamma.valueAt(u, 1.0);
        for (int p = 0; p < sound->partials; ++p)
        {
            const auto a0 = sound->amp[p][f0], a1 = sound->amp[p][f1];
            const auto q0 = sound->frq[p][f0], q1 = sound->frq[p][f1];
            const bool on0 = a0 > 0.0 && q0 > 0.0, on1 = a1 > 0.0 && q1 > 0.0;
            double amp = a0 + t * (a1 - a0);
            // Hold the active side's frequency across a birth or death.
            double frq = on0 && on1 ? q0 + t * (q1 - q0) : (on0 ? q0 : (on1 ? q1 : 0.0));
            if (frq > 0.0)
            {
                frq *= ratio;
                if (anchor > 0.0 && std::abs(gamma - 1.0) > 1.0e-9)
                    frq = anchor * std::pow(frq / anchor, gamma);
                frq = std::min(frq, top);
            }
            out->amp[p][f] = frq > 0.0 ? amp : 0.0;
            out->frq[p][f] = frq;
            if (out->pha) out->pha[p][f] = 0.0;
            if (out->smr && sound->smr) out->smr[p][f] = sound->smr[p][f0] + t * (sound->smr[p][f1] - sound->smr[p][f0]);
            if (out->res) out->res[p][f] = 0.0;
        }
        if (out->band_energy != nullptr)
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                out->band_energy[b][f] = sound->band_energy[b][f0]
                    + t * (sound->band_energy[b][f1] - sound->band_energy[b][f0]);
    }
    out->has_slopes = 0;
    double frqmax = 0.0;
    for (int p = 0; p < out->partials; ++p)
        for (int f = 0; f < frames; ++f)
            frqmax = std::max(frqmax, out->frq[p][f]);
    out->frqmax = frqmax;
    return result;
}

} // namespace ats

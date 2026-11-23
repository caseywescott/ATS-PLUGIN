#include "AtsDescriptors.h"

#include "AtsAutomationCurves.h"

#include <algorithm>
#include <cmath>

namespace ats
{

namespace
{

struct ActivePartial
{
    double frq;
    double amp;
    int h;
};

std::vector<ActivePartial> activePartials(const ATS_SOUND* sound, int frame)
{
    std::vector<ActivePartial> out;
    out.reserve(static_cast<std::size_t>(std::max(0, sound->partials)));
    for (int p = 0; p < sound->partials; ++p)
    {
        const auto a = sound->amp[p][frame];
        const auto f = sound->frq[p][frame];
        if (a > 0.0 && f > 0.0 && std::isfinite(a) && std::isfinite(f))
            out.push_back({f, a, 1});
    }
    return out;
}

int bandOf(double hz)
{
    for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        if (hz < ATSA_CRITICAL_BAND_EDGES[b + 1])
            return b;
    return ATSA_CRITICAL_BANDS - 1;
}

DescriptorSummary summarise(const std::vector<FrameDescriptors>& frames, double FrameDescriptors::*field)
{
    DescriptorSummary s;
    int n = 0;
    double sum = 0.0;
    for (const auto& fd : frames)
    {
        if (!fd.active)
            continue;
        const auto v = fd.*field;
        if (n == 0)
            s.min = s.max = v;
        s.min = std::min(s.min, v);
        s.max = std::max(s.max, v);
        sum += v;
        ++n;
    }
    if (n == 0)
        return s;
    s.mean = sum / n;
    double sq = 0.0; // two-pass: avoids cancellation in sum(v^2) - n mean^2
    for (const auto& fd : frames)
        if (fd.active)
            sq += (fd.*field - s.mean) * (fd.*field - s.mean);
    s.stddev = std::sqrt(sq / n);
    return s;
}

} // namespace

double hzToBark(double hz)
{
    if (!std::isfinite(hz) || hz <= 0.0)
        return 0.0;
    return 13.0 * std::atan(0.76 * hz / 1000.0) + 3.5 * std::atan((hz / 7500.0) * (hz / 7500.0));
}

FrameDescriptors describeFrame(const ATS_SOUND* sound, int frame, double fundamentalHz)
{
    FrameDescriptors d;
    if (sound == nullptr || frame < 0 || frame >= sound->frames || sound->partials <= 0)
        return d;

    auto parts = activePartials(sound, frame);
    // Residual energy is reported even for frames without partials.
    if (sound->band_energy != nullptr && sound->window_size > 0)
    {
        double e = 0.0;
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            e += std::max(0.0, sound->band_energy[b][frame]);
        d.residualEnergy = e / (sound->window_size * ATSA_NOISE_VARIANCE);
    }
    if (parts.empty())
    {
        d.noisiness = d.residualEnergy > 0.0 ? 1.0 : 0.0;
        return d;
    }
    d.active = true;

    double sumA = 0.0, sumA2 = 0.0, sumFA = 0.0, sumBarkA = 0.0;
    for (const auto& p : parts)
    {
        sumA += p.amp;
        sumA2 += p.amp * p.amp;
        sumFA += p.frq * p.amp;
        sumBarkA += hzToBark(p.frq) * p.amp;
    }
    d.sinusoidalEnergy = sumA2;
    d.centroidHz = sumFA / sumA;
    d.centroidBark = sumBarkA / sumA;

    // Central moments about the centroid.
    double m2 = 0.0, m3 = 0.0, m4 = 0.0;
    for (const auto& p : parts)
    {
        const auto dev = p.frq - d.centroidHz;
        const auto w = p.amp / sumA;
        m2 += w * dev * dev;
        m3 += w * dev * dev * dev;
        m4 += w * dev * dev * dev * dev;
    }
    d.spreadHz = std::sqrt(std::max(0.0, m2));
    if (d.spreadHz > 1.0e-9)
    {
        d.skewness = m3 / (d.spreadHz * d.spreadHz * d.spreadHz);
        d.kurtosis = m4 / (m2 * m2);
    }

    // Weighted least-squares slope of 20 log10(a) on log2(f).
    {
        double xBar = 0.0, yBar = 0.0;
        for (const auto& p : parts)
        {
            xBar += (p.amp / sumA) * std::log2(p.frq);
            yBar += (p.amp / sumA) * 20.0 * std::log10(p.amp);
        }
        double sxy = 0.0, sxx = 0.0;
        for (const auto& p : parts)
        {
            const auto w = p.amp / sumA;
            const auto dx = std::log2(p.frq) - xBar;
            sxy += w * dx * (20.0 * std::log10(p.amp) - yBar);
            sxx += w * dx * dx;
        }
        d.tiltDbPerOctave = sxx > 1.0e-12 ? sxy / sxx : 0.0;
    }

    // Harmonic structure.  Coarse estimate -> ranks -> refined f0 (Serra/Bonada).
    double f0Est = fundamentalHz;
    if (!(f0Est > 0.0) || !std::isfinite(f0Est))
        for (const auto& p : parts)
            f0Est = f0Est > 0.0 ? std::min(f0Est, p.frq) : p.frq;
    double sumRankA = 0.0, sumF0A = 0.0;
    for (auto& p : parts)
    {
        p.h = std::max(1, static_cast<int>(std::lround(p.frq / f0Est)));
        sumRankA += p.h * p.amp;
        sumF0A += (p.frq / p.h) * p.amp;
    }
    d.harmonicRankCentroid = sumRankA / sumA;
    d.f0Hz = sumF0A / sumA;

    double dist = 0.0, inharm = 0.0, odd = 0.0, even = 0.0;
    for (const auto& p : parts)
    {
        const auto target = p.h * d.f0Hz;
        const auto dev = std::abs(p.frq - target);
        dist += dev * p.amp;
        inharm += (dev / target) * p.amp;
        const auto e = p.amp * p.amp;
        if (p.h == 1)
            d.tristimulus[0] += e;
        else if (p.h <= 4)
            d.tristimulus[1] += e;
        else
            d.tristimulus[2] += e;
        if (p.h % 2 == 1)
            odd += e;
        else
            even += e;
    }
    d.harmonicDistortionHz = dist / sumA;
    d.inharmonicity = inharm / sumA;
    for (auto& t : d.tristimulus)
        t /= sumA2;
    d.oddEvenRatio = even > 0.0 ? odd / even : 0.0;

    d.noisiness = (d.sinusoidalEnergy + d.residualEnergy) > 0.0
        ? d.residualEnergy / (d.sinusoidalEnergy + d.residualEnergy)
        : 0.0;

    // Subband centroids over the 25 critical bands.
    std::array<double, 25> bandA {}, bandFA {};
    for (const auto& p : parts)
    {
        const auto b = static_cast<std::size_t>(bandOf(p.frq));
        bandA[b] += p.amp;
        bandFA[b] += p.frq * p.amp;
    }
    for (std::size_t b = 0; b < bandA.size(); ++b)
        d.subbandCentroidHz[b] = bandA[b] > 0.0 ? bandFA[b] / bandA[b] : 0.0;
    return d;
}

DescriptorTrack describeSound(const ATS_SOUND* sound)
{
    DescriptorTrack track;
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0)
        return track;
    track.fundamentalHz = AtsAutomationCurves::estimateFundamental(sound);
    track.frames.reserve(static_cast<std::size_t>(sound->frames));
    for (int f = 0; f < sound->frames; ++f)
        track.frames.push_back(describeFrame(sound, f, track.fundamentalHz));

    track.centroidHz = summarise(track.frames, &FrameDescriptors::centroidHz);
    track.centroidBark = summarise(track.frames, &FrameDescriptors::centroidBark);
    track.spreadHz = summarise(track.frames, &FrameDescriptors::spreadHz);
    track.tiltDbPerOctave = summarise(track.frames, &FrameDescriptors::tiltDbPerOctave);
    track.harmonicRankCentroid = summarise(track.frames, &FrameDescriptors::harmonicRankCentroid);
    track.noisiness = summarise(track.frames, &FrameDescriptors::noisiness);
    track.inharmonicity = summarise(track.frames, &FrameDescriptors::inharmonicity);

    // Attack centroid: 300 ms from the first frame above 10 % of the peak energy.
    double peak = 0.0;
    for (const auto& fd : track.frames)
        peak = std::max(peak, fd.sinusoidalEnergy + fd.residualEnergy);
    if (peak <= 0.0)
        return track;
    int onset = -1;
    for (int f = 0; f < sound->frames && onset < 0; ++f)
        if (track.frames[static_cast<std::size_t>(f)].sinusoidalEnergy
                + track.frames[static_cast<std::size_t>(f)].residualEnergy > 0.1 * peak)
            onset = f;
    if (onset < 0)
        return track;
    const auto hop = sound->srate > 0 ? static_cast<double>(sound->frame_size) / sound->srate : 0.0;
    const auto timeAt = [&](int f) { return sound->time != nullptr && sound->time[0] != nullptr ? sound->time[0][f] : f * hop; };
    const auto t0 = timeAt(onset);
    double weighted = 0.0, weight = 0.0;
    for (int f = onset; f < sound->frames; ++f)
    {
        const auto t = timeAt(f);
        if (t - t0 > 0.3)
            break;
        const auto& fd = track.frames[static_cast<std::size_t>(f)];
        if (!fd.active)
            continue;
        auto span = f + 1 < sound->frames ? timeAt(f + 1) - t : hop;
        if (!(span > 0.0))
            span = hop > 0.0 ? hop : 1.0;
        weighted += fd.centroidHz * span;
        weight += span;
    }
    track.attackCentroidHz = weight > 0.0 ? weighted / weight : 0.0;
    return track;
}

} // namespace ats

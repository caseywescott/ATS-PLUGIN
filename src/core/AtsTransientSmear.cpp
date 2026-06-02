#include "AtsTransientSmear.h"

#include <algorithm>
#include <cmath>

namespace ats
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279;
constexpr double kTwoPi = 6.283185307179586476925286766559;

bool finitePositive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

double activeAmp(const ATS_SOUND* s, int p, int f)
{
    if (s->amp == nullptr || s->amp[p] == nullptr)
        return 0.0;
    const double a = s->amp[p][f];
    return finitePositive(a) ? a : 0.0;
}

double bandEnergy(const ATS_SOUND* s, int b, int f)
{
    if (s->band_energy == nullptr || s->band_energy[b] == nullptr)
        return 0.0;
    const double e = s->band_energy[b][f];
    return std::isfinite(e) && e > 0.0 ? e : 0.0;
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

int secondsToFrames(double seconds, double hop)
{
    if (!finitePositive(hop) || seconds <= 0.0)
        return 0;
    return (int) std::lround(seconds / hop);
}

// Wrap to [-pi, pi].
double wrapPhase(double x)
{
    x = std::fmod(x + kPi, kTwoPi);
    if (x < 0.0)
        x += kTwoPi;
    return x - kPi;
}

double medianOf(std::vector<double>& scratch)
{
    if (scratch.empty())
        return 0.0;
    const size_t mid = scratch.size() / 2;
    std::nth_element(scratch.begin(), scratch.begin() + (long) mid, scratch.end());
    const double hi = scratch[mid];
    if (scratch.size() % 2 == 1)
        return hi;
    std::nth_element(scratch.begin(), scratch.begin() + (long) (mid - 1), scratch.end());
    return 0.5 * (hi + scratch[mid - 1]);
}

// Robust standardization: subtract running median, divide by running MAD, half-wave rectify.
std::vector<double> normalizeStream(const std::vector<double>& raw, int radius)
{
    const int n = (int) raw.size();
    std::vector<double> out(raw.size(), 0.0);
    if (n == 0)
        return out;
    radius = std::max(radius, 1);
    std::vector<double> win;
    std::vector<double> dev;
    win.reserve((size_t) (2 * radius + 1));
    dev.reserve((size_t) (2 * radius + 1));
    for (int f = 0; f < n; ++f)
    {
        const int lo = std::max(0, f - radius);
        const int hi = std::min(n - 1, f + radius);
        win.clear();
        for (int k = lo; k <= hi; ++k)
            win.push_back(raw[(size_t) k]);
        const double med = medianOf(win);
        dev.clear();
        for (int k = lo; k <= hi; ++k)
            dev.push_back(std::abs(raw[(size_t) k] - med));
        const double mad = medianOf(dev);
        const double denom = 1.4826 * mad + 1.0e-12;
        const double z = (raw[(size_t) f] - med) / denom;
        out[(size_t) f] = std::max(z, 0.0);
    }
    return out;
}

double globalMad(const std::vector<double>& d)
{
    if (d.empty())
        return 0.0;
    std::vector<double> tmp(d);
    const double med = medianOf(tmp);
    for (auto& v : tmp)
        v = std::abs(v - med);
    return medianOf(tmp);
}
} // namespace

double criticalBandCentreHz(int band)
{
    // Zwicker & Fastl (1990) critical-band edges; matches ATSA_CRITICAL_BAND_EDGES.
    static constexpr double edges[ATSA_CRITICAL_BANDS + 1] = {
        0.0, 100.0, 200.0, 300.0, 400.0, 510.0, 630.0, 770.0, 920.0,
        1080.0, 1270.0, 1480.0, 1720.0, 2000.0, 2320.0, 2700.0, 3150.0,
        3700.0, 4400.0, 5300.0, 6400.0, 7700.0, 9500.0, 12000.0, 15500.0, 20000.0
    };
    if (band < 0)
        band = 0;
    if (band >= ATSA_CRITICAL_BANDS)
        band = ATSA_CRITICAL_BANDS - 1;
    return 0.5 * (edges[band] + edges[band + 1]);
}

std::vector<AtsOnset> detectOnsets(const ATS_SOUND* sound,
                                   const SmearnovDetectParams& params,
                                   SmearnovDetectionFunctions* functionsOut)
{
    std::vector<AtsOnset> onsets;
    if (sound == nullptr || sound->frames < 2)
        return onsets;

    const int frames = sound->frames;
    const int partials = std::max(sound->partials, 0);
    const double hop = hopSeconds(sound);
    const bool hasResidual = sound->band_energy != nullptr;
    const bool hasPhase = params.usePhaseDeviation && sound->pha != nullptr;

    // Perceptual SMR weighting reference.
    double maxSmr = 0.0;
    const bool smrWeighting = params.useSmrWeighting && sound->smr != nullptr;
    if (smrWeighting)
        for (int p = 0; p < partials; ++p)
            if (sound->smr[p] != nullptr)
                for (int f = 0; f < frames; ++f)
                    if (std::isfinite(sound->smr[p][f]) && sound->smr[p][f] > maxSmr)
                        maxSmr = sound->smr[p][f];

    std::vector<double> d1(frames, 0.0); // deterministic energy flux
    std::vector<double> d2(frames, 0.0); // residual energy flux
    std::vector<double> d3(frames, 0.0); // residual HFC flux
    std::vector<double> d4(frames, 0.0); // partial-birth rate
    std::vector<double> d5(frames, 0.0); // phase deviation
    std::vector<double> energy(frames, 0.0); // total energy proxy for attack scan

    std::vector<double> hfc(frames, 0.0);
    if (hasResidual)
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        {
            const double c = criticalBandCentreHz(b);
            for (int f = 0; f < frames; ++f)
                hfc[(size_t) f] += c * bandEnergy(sound, b, f);
        }

    for (int f = 0; f < frames; ++f)
    {
        double detEnergy = 0.0;
        for (int p = 0; p < partials; ++p)
        {
            const double a = activeAmp(sound, p, f);
            detEnergy += a * a;

            if (f == 0)
                continue;

            const double aPrev = activeAmp(sound, p, f - 1);
            const double rise = a - aPrev;
            if (rise > 0.0)
            {
                double w = 1.0;
                if (smrWeighting && maxSmr > 0.0 && sound->smr[p] != nullptr
                    && std::isfinite(sound->smr[p][f]))
                {
                    const double r = std::clamp(sound->smr[p][f] / maxSmr, 0.0, 1.0);
                    w = params.smrWeightFloor + (1.0 - params.smrWeightFloor) * r;
                }
                d1[(size_t) f] += w * rise;
            }

            // Partial birth: inactive -> active.
            if (aPrev <= 0.0 && a > 0.0)
                d4[(size_t) f] += 1.0;

            if (hasPhase && aPrev > 0.0 && a > 0.0 && sound->pha[p] != nullptr
                && sound->frq != nullptr && sound->frq[p] != nullptr)
            {
                const double predicted = sound->pha[p][f - 1] + kTwoPi * sound->frq[p][f] * hop;
                d5[(size_t) f] += std::abs(wrapPhase(sound->pha[p][f] - predicted));
            }
        }

        double resEnergy = 0.0;
        if (hasResidual)
        {
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            {
                const double e = bandEnergy(sound, b, f);
                resEnergy += e;
                if (f > 0)
                {
                    const double rise = e - bandEnergy(sound, b, f - 1);
                    if (rise > 0.0)
                        d2[(size_t) f] += rise;
                }
            }
            if (f > 0)
            {
                const double hfcRise = hfc[(size_t) f] - hfc[(size_t) (f - 1)];
                if (hfcRise > 0.0)
                    d3[(size_t) f] = hfcRise;
            }
        }

        energy[(size_t) f] = detEnergy + resEnergy;
    }

    const auto n1 = normalizeStream(d1, params.normWindowRadiusFrames);
    const auto n2 = normalizeStream(d2, params.normWindowRadiusFrames);
    const auto n3 = normalizeStream(d3, params.normWindowRadiusFrames);
    const auto n4 = normalizeStream(d4, params.normWindowRadiusFrames);
    const auto n5 = normalizeStream(d5, params.normWindowRadiusFrames);

    std::vector<double> combined(frames, 0.0);
    for (int f = 0; f < frames; ++f)
        combined[(size_t) f] = params.weightDetFlux * n1[(size_t) f]
                             + params.weightResFlux * n2[(size_t) f]
                             + params.weightHfcFlux * n3[(size_t) f]
                             + params.weightBirth * n4[(size_t) f]
                             + (hasPhase ? params.weightPhaseDev * n5[(size_t) f] : 0.0);

    // Adaptive threshold: running median over the peak window + lambda.
    const double lambda = params.thresholdLambda > 0.0
                              ? params.thresholdLambda
                              : params.thresholdAutoScale * globalMad(combined);
    const int w = std::max(params.peakWindowRadiusFrames, 1);
    std::vector<double> threshold(frames, 0.0);
    {
        std::vector<double> win;
        win.reserve((size_t) (2 * w + 1));
        for (int f = 0; f < frames; ++f)
        {
            const int lo = std::max(0, f - w);
            const int hi = std::min(frames - 1, f + w);
            win.clear();
            for (int k = lo; k <= hi; ++k)
                win.push_back(combined[(size_t) k]);
            threshold[(size_t) f] = medianOf(win) + lambda;
        }
    }

    if (functionsOut != nullptr)
    {
        functionsOut->combined = combined;
        functionsOut->threshold = threshold;
    }

    int minGap = params.minGapFramesOverride > 0 ? params.minGapFramesOverride
                                                 : secondsToFrames(params.minGapSeconds, hop);
    minGap = std::max(minGap, 1);
    const int preMargin = secondsToFrames(params.preMarginSeconds, hop);

    // Peak picking: local maximum, above adaptive threshold, respecting the gap.
    std::vector<int> peakFrames;
    int lastOnset = -minGap - 1;
    for (int f = 0; f < frames; ++f)
    {
        const double v = combined[(size_t) f];
        if (v <= params.onsetFloor || v < threshold[(size_t) f])
            continue;

        bool isLocalMax = true;
        for (int k = 1; k <= w && isLocalMax; ++k)
        {
            if (f - k >= 0 && combined[(size_t) (f - k)] > v)
                isLocalMax = false;
            if (f + k < frames && combined[(size_t) (f + k)] > v)
                isLocalMax = false;
        }
        if (!isLocalMax)
            continue;

        if (f - lastOnset < minGap)
        {
            // Keep the stronger of the two within the refractory window.
            if (!peakFrames.empty() && v > combined[(size_t) peakFrames.back()])
            {
                peakFrames.back() = f;
                lastOnset = f;
            }
            continue;
        }

        peakFrames.push_back(f);
        lastOnset = f;
    }

    // Build attack windows from a lightly smoothed energy curve.
    auto smoothEnergy = [&](int f) {
        const int lo = std::max(0, f - 1);
        const int hi = std::min(frames - 1, f + 1);
        double sum = 0.0;
        for (int k = lo; k <= hi; ++k)
            sum += energy[(size_t) k];
        return sum / (double) (hi - lo + 1);
    };

    for (size_t i = 0; i < peakFrames.size(); ++i)
    {
        const int f0 = peakFrames[i];
        const int nextOnset = (i + 1 < peakFrames.size()) ? peakFrames[i + 1] : frames;
        int limit = nextOnset - 1;
        if (params.maxAttackFrames > 0)
            limit = std::min(limit, f0 + params.maxAttackFrames);
        limit = std::min(limit, frames - 1);

        int fPeak = f0;
        double prev = smoothEnergy(f0);
        for (int f = f0 + 1; f <= limit; ++f)
        {
            const double cur = smoothEnergy(f);
            if (cur <= prev + 1.0e-15)
                break; // stop at the first frame that no longer rises
            fPeak = f;
            prev = cur;
        }

        AtsOnset onset;
        onset.frame = f0;
        onset.preFrame = std::max(0, f0 - preMargin);
        onset.peakFrame = std::max(fPeak, f0);
        onset.strength = combined[(size_t) f0];
        onsets.push_back(onset);
    }

    return onsets;
}

} // namespace ats

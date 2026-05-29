#include "AtsFrequencyShape.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ats
{
namespace
{
struct PartialMean
{
    int partialIndex = -1;
    double meanHz = 0.0;
};

bool isPositiveFinite(double value)
{
    return std::isfinite(value) && value > 0.0;
}

double safeAmount(double amount)
{
    return isPositiveFinite(amount) ? amount : 1.0;
}

double clampWarpedFrequency(double hz, double nyquistHz)
{
    if (!isPositiveFinite(hz))
        return 0.0;

    const double top = isPositiveFinite(nyquistHz) ? nyquistHz * 0.99 : 20000.0;
    return std::clamp(hz, 1.0, std::max(1.0, top));
}

double activeAmplitude(const ATS_SOUND* sound, int partial, int frame)
{
    if (sound == nullptr || sound->amp == nullptr || sound->amp[partial] == nullptr)
        return 1.0;

    const double amp = sound->amp[partial][frame];
    return isPositiveFinite(amp) ? amp : 0.0;
}

double weightedMeanAcrossAllFrames(const ATS_SOUND* sound)
{
    if (sound == nullptr || sound->partials <= 0 || sound->frames <= 0 || sound->frq == nullptr)
        return 0.0;

    double weightedHz = 0.0;
    double ampSum = 0.0;
    for (int p = 0; p < sound->partials; ++p)
    {
        if (sound->frq[p] == nullptr)
            continue;

        for (int f = 0; f < sound->frames; ++f)
        {
            const double hz = sound->frq[p][f];
            const double amp = activeAmplitude(sound, p, f);
            if (!isPositiveFinite(hz) || amp <= 0.0)
                continue;

            weightedHz += hz * amp;
            ampSum += amp;
        }
    }

    return ampSum > 0.0 ? weightedHz / ampSum : 0.0;
}
} // namespace

const char* frequencyShapeModeName(FrequencyShapeMode mode)
{
    switch (mode)
    {
        case FrequencyShapeMode::Stretch: return "Stretch";
        case FrequencyShapeMode::Compress: return "Compress";
        case FrequencyShapeMode::Harmonic: return "Harmonic";
        case FrequencyShapeMode::Linear:
        default: return "Linear";
    }
}

bool isFrequencyShapeModeEnabled(const FrequencyShapeSettings& settings)
{
    return settings.mode != FrequencyShapeMode::Linear;
}

bool isFrequencyShapeActive(const FrequencyShapeSettings& settings)
{
    return isFrequencyShapeModeEnabled(settings)
           && std::abs(safeAmount(settings.amount) - 1.0) > 1.0e-8
           && isPositiveFinite(settings.resolvedPivotHz);
}

bool sameFrequencyShapeSettings(const FrequencyShapeSettings& a,
                                const FrequencyShapeSettings& b,
                                double tolerance)
{
    return a.mode == b.mode
           && std::abs(a.amount - b.amount) <= tolerance
           && std::abs(a.pivotHz - b.pivotHz) <= tolerance
           && std::abs(a.resolvedPivotHz - b.resolvedPivotHz) <= tolerance;
}

double resolveFrequencyShapePivotHz(const ATS_SOUND* sound,
                                    const FrequencyShapeSettings& settings)
{
    if (isPositiveFinite(settings.pivotHz))
        return settings.pivotHz;

    if (sound == nullptr || sound->partials <= 0 || sound->frames <= 0 || sound->frq == nullptr)
        return 0.0;

    const int midFrame = std::clamp(sound->frames / 2, 0, sound->frames - 1);
    double weightedHz = 0.0;
    double ampSum = 0.0;
    for (int p = 0; p < sound->partials; ++p)
    {
        if (sound->frq[p] == nullptr)
            continue;

        const double hz = sound->frq[p][midFrame];
        const double amp = activeAmplitude(sound, p, midFrame);
        if (!isPositiveFinite(hz) || amp <= 0.0)
            continue;

        weightedHz += hz * amp;
        ampSum += amp;
    }

    if (ampSum > 0.0)
        return weightedHz / ampSum;

    return weightedMeanAcrossAllFrames(sound);
}

FrequencyShapeSettings resolveFrequencyShapeSettingsForSound(const ATS_SOUND* sound,
                                                             FrequencyShapeSettings settings)
{
    settings.amount = safeAmount(settings.amount);
    settings.resolvedPivotHz = isFrequencyShapeModeEnabled(settings)
                                   ? resolveFrequencyShapePivotHz(sound, settings)
                                   : 0.0;
    return settings;
}

std::vector<int> buildFrequencyShapeRanks(const ATS_SOUND* sound)
{
    std::vector<int> ranks;
    if (sound == nullptr || sound->partials <= 0)
        return ranks;

    ranks.assign((size_t) sound->partials, 0);
    if (sound->frames <= 0 || sound->frq == nullptr)
        return ranks;

    std::vector<PartialMean> means;
    means.reserve((size_t) sound->partials);
    for (int p = 0; p < sound->partials; ++p)
    {
        if (sound->frq[p] == nullptr)
            continue;

        double weightedHz = 0.0;
        double ampSum = 0.0;
        for (int f = 0; f < sound->frames; ++f)
        {
            const double hz = sound->frq[p][f];
            const double amp = activeAmplitude(sound, p, f);
            if (!isPositiveFinite(hz) || amp <= 0.0)
                continue;

            weightedHz += hz * amp;
            ampSum += amp;
        }

        if (ampSum > 0.0)
            means.push_back({ p, weightedHz / ampSum });
    }

    std::sort(means.begin(), means.end(), [](const PartialMean& a, const PartialMean& b) {
        if (a.meanHz != b.meanHz)
            return a.meanHz < b.meanHz;
        return a.partialIndex < b.partialIndex;
    });

    for (size_t i = 0; i < means.size(); ++i)
        ranks[(size_t) means[i].partialIndex] = (int) i + 1;

    return ranks;
}

double applyFrequencyShape(double frequencyHz,
                           int harmonicRank,
                           const FrequencyShapeSettings& settings,
                           double nyquistHz)
{
    if (!isPositiveFinite(frequencyHz) || !isFrequencyShapeModeEnabled(settings))
        return frequencyHz;

    const double amount = safeAmount(settings.amount);
    const double pivot = settings.resolvedPivotHz;
    if (!isPositiveFinite(pivot))
        return clampWarpedFrequency(frequencyHz, nyquistHz);

    double anchorHz = pivot;
    if (settings.mode == FrequencyShapeMode::Harmonic && harmonicRank > 0)
        anchorHz = pivot * (double) harmonicRank;

    if (!isPositiveFinite(anchorHz))
        anchorHz = pivot;

    const double warped = anchorHz * std::pow(frequencyHz / anchorHz, amount);
    return clampWarpedFrequency(warped, nyquistHz);
}

void applyFrequencyShapeToSound(ATS_SOUND* sound, const FrequencyShapeSettings& settings)
{
    if (sound == nullptr || sound->partials <= 0 || sound->frames <= 0 || sound->frq == nullptr)
        return;

    const auto resolved = resolveFrequencyShapeSettingsForSound(sound, settings);
    if (!isFrequencyShapeActive(resolved))
        return;

    const auto ranks = buildFrequencyShapeRanks(sound);
    const double nyquistHz = sound->srate > 0 ? (double) sound->srate * 0.5 : 22050.0;
    double maxHz = 0.0;
    for (int p = 0; p < sound->partials; ++p)
    {
        if (sound->frq[p] == nullptr)
            continue;

        const int rank = p < (int) ranks.size() ? ranks[(size_t) p] : 0;
        double weightedHz = 0.0;
        double ampSum = 0.0;
        for (int f = 0; f < sound->frames; ++f)
        {
            const double hz = sound->frq[p][f];
            if (!isPositiveFinite(hz))
                continue;

            const double warped = applyFrequencyShape(hz, rank, resolved, nyquistHz);
            sound->frq[p][f] = warped;
            maxHz = std::max(maxHz, warped);

            const double amp = activeAmplitude(sound, p, f);
            if (amp > 0.0)
            {
                weightedHz += warped * amp;
                ampSum += amp;
            }
        }

        if (sound->av != nullptr)
            sound->av[p].frq = ampSum > 0.0 ? weightedHz / ampSum : 0.0;
    }

    if (maxHz > 0.0)
        sound->frqmax = maxHz;
}

} // namespace ats

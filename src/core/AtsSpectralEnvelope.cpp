#include "AtsSpectralEnvelope.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ats
{
namespace
{

double clampUnit(double value)
{
    if (!std::isfinite(value))
        return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

double safeFloor(double value)
{
    if (!std::isfinite(value) || value <= 0.0)
        return BarkSpectralEnvelope::kDefaultAmplitudeFloor;
    return std::max(value, std::numeric_limits<double>::min());
}

double amplitudeToDb(double amplitude, double floor)
{
    return 20.0 * std::log10(std::max(amplitude, floor));
}

// Geometric (log-frequency) interpolation: the perceptual midpoint of two pitches is their
// geometric mean, not their arithmetic mean. Falls back to linear if an endpoint is not a
// usable positive frequency.
double logInterpHz(double f0, double f1, double t)
{
    if (!std::isfinite(f0) || !std::isfinite(f1) || f0 <= 0.0 || f1 <= 0.0)
        return f0 + (f1 - f0) * t;
    return std::exp(std::log(f0) + (std::log(f1) - std::log(f0)) * t);
}

// Equal-power (constant-energy) interpolation: interpolate power linearly, then take the
// root. Avoids the perceived loudness sag a linear-amplitude blend produces at the midpoint.
double equalPowerInterpAmp(double a0, double a1, double t)
{
    const double power = (1.0 - t) * a0 * a0 + t * a1 * a1;
    return power > 0.0 ? std::sqrt(power) : 0.0;
}

} // namespace

double BarkSpectralEnvelope::hzToBark(double frequencyHz)
{
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0)
        return 0.0;

    return 13.0 * std::atan(0.00076 * frequencyHz)
           + 3.5 * std::atan((frequencyHz / 7500.0) * (frequencyHz / 7500.0));
}

void BarkSpectralEnvelope::build(const std::vector<SpectralEnvelopePoint>& points,
                                 int binCount,
                                 double sigmaBins,
                                 double newAmplitudeFloor)
{
    valid = false;
    maxBark = kDefaultMaxBark;
    amplitudeFloor = safeFloor(newAmplitudeFloor);
    peakAmplitude = amplitudeFloor;
    binCount = std::max(2, binCount);
    sigmaBins = std::isfinite(sigmaBins) && sigmaBins > 0.0 ? sigmaBins : kDefaultSigmaBins;

    ampBins.assign(static_cast<size_t>(binCount), amplitudeFloor);
    std::vector<double> sumW(static_cast<size_t>(binCount), 0.0);
    std::vector<double> sumWA(static_cast<size_t>(binCount), 0.0);

    int validPointCount = 0;
    for (const auto& point : points)
    {
        if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.amplitude)
            || point.frequencyHz <= 0.0 || point.amplitude <= 0.0)
        {
            continue;
        }

        ++validPointCount;
        const double rawBin = hzToBark(point.frequencyHz) / maxBark * static_cast<double>(binCount - 1);
        const double binF = std::clamp(rawBin, 0.0, static_cast<double>(binCount - 1));
        const int lo = std::max(0, static_cast<int>(std::floor(binF - 4.0 * sigmaBins)));
        const int hi = std::min(binCount - 1, static_cast<int>(std::ceil(binF + 4.0 * sigmaBins)));
        for (int b = lo; b <= hi; ++b)
        {
            const double d = static_cast<double>(b) - binF;
            const double w = std::exp(-0.5 * d * d / (sigmaBins * sigmaBins));
            sumW[static_cast<size_t>(b)] += w;
            sumWA[static_cast<size_t>(b)] += w * point.amplitude;
        }
    }

    if (validPointCount == 0)
        return;

    std::vector<int> anchors;
    anchors.reserve(static_cast<size_t>(binCount));
    for (int b = 0; b < binCount; ++b)
    {
        const size_t i = static_cast<size_t>(b);
        if (sumW[i] > 1.0e-12)
        {
            ampBins[i] = std::max(amplitudeFloor, sumWA[i] / sumW[i]);
            anchors.push_back(b);
        }
    }

    if (anchors.empty())
        return;

    const int first = anchors.front();
    for (int b = 0; b < first; ++b)
        ampBins[static_cast<size_t>(b)] = ampBins[static_cast<size_t>(first)];

    for (size_t ai = 1; ai < anchors.size(); ++ai)
    {
        const int left = anchors[ai - 1];
        const int right = anchors[ai];
        if (right <= left + 1)
            continue;

        const double leftLog = std::log(std::max(amplitudeFloor, ampBins[static_cast<size_t>(left)]));
        const double rightLog = std::log(std::max(amplitudeFloor, ampBins[static_cast<size_t>(right)]));
        for (int b = left + 1; b < right; ++b)
        {
            const double t = static_cast<double>(b - left) / static_cast<double>(right - left);
            ampBins[static_cast<size_t>(b)] = std::exp(leftLog + t * (rightLog - leftLog));
        }
    }

    const int last = anchors.back();
    for (int b = last + 1; b < binCount; ++b)
        ampBins[static_cast<size_t>(b)] = ampBins[static_cast<size_t>(last)];

    peakAmplitude = amplitudeFloor;
    for (const double a : ampBins)
        peakAmplitude = std::max(peakAmplitude, a);

    valid = true;
}

double BarkSpectralEnvelope::sampleAtHz(double frequencyHz) const
{
    if (!valid || ampBins.size() < 2 || !std::isfinite(frequencyHz) || frequencyHz <= 0.0)
        return 0.0;

    const double rawBin = hzToBark(frequencyHz) / maxBark * static_cast<double>(ampBins.size() - 1);
    const double binF = std::clamp(rawBin, 0.0, static_cast<double>(ampBins.size() - 1));
    const int b0 = static_cast<int>(std::floor(binF));
    if (b0 >= static_cast<int>(ampBins.size()) - 1)
        return std::max(amplitudeFloor, ampBins.back());

    const double t = binF - static_cast<double>(b0);
    const double value = ampBins[static_cast<size_t>(b0)] * (1.0 - t)
                         + ampBins[static_cast<size_t>(b0 + 1)] * t;
    return std::max(amplitudeFloor, value);
}

SpectralEnvelopeTransferResult applySpectralEnvelopeTransfer(double sourceAmplitude,
                                                             double sourceEnvelopeAmplitude,
                                                             double targetEnvelopeAmplitude,
                                                             double amount,
                                                             double amplitudeFloor)
{
    SpectralEnvelopeTransferResult result;
    result.outputAmplitude = std::max(0.0, sourceAmplitude);

    if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0
        || !std::isfinite(sourceEnvelopeAmplitude) || !std::isfinite(targetEnvelopeAmplitude))
    {
        return result;
    }

    result.ok = true;
    if (sourceAmplitude == 0.0)
        return result;

    const double floor = safeFloor(amplitudeFloor);
    const double envA = std::max(floor, sourceEnvelopeAmplitude);
    const double envB = std::max(floor, targetEnvelopeAmplitude);
    const double transferAmount = clampUnit(amount);
    const double logGain = (std::log(envB) - std::log(envA)) * transferAmount;
    const double logOut = std::log(sourceAmplitude) + logGain;

    result.outputAmplitude = std::exp(logOut);
    result.transferGainDb = amplitudeToDb(result.outputAmplitude, floor)
                            - amplitudeToDb(sourceAmplitude, floor);
    result.ok = std::isfinite(result.outputAmplitude) && result.outputAmplitude >= 0.0
                && std::isfinite(result.transferGainDb);
    if (!result.ok)
        result.outputAmplitude = 0.0;
    return result;
}

HybridizedPartialRenderResult resolveHybridizedPartialRender(HybridizedPartialKind kind,
                                                             double sourceFrequencyHz,
                                                             double sourceAmplitude,
                                                             double targetFrequencyHz,
                                                             double targetAmplitude,
                                                             double morphAmount,
                                                             double nearestTargetHz,
                                                             double nearestSourceHz)
{
    HybridizedPartialRenderResult result;
    const double amount = clampUnit(morphAmount);

    switch (kind)
    {
        case HybridizedPartialKind::Matched:
            // Geometric frequency blend + equal-power amplitude blend so pitch glides track
            // perceived pitch and the morph midpoint holds its loudness.
            result.frequencyHz = logInterpHz(sourceFrequencyHz, targetFrequencyHz, amount);
            result.amplitude = equalPowerInterpAmp(sourceAmplitude, targetAmplitude, amount);
            break;

        case HybridizedPartialKind::UnmatchedSource:
            // Glide toward nearest target partial (when one is supplied) while fading out.
            // Equal-power (sqrt) fade pairs with the UnmatchedTarget fade-in so a replacing
            // pair holds constant power through the crossfade; time-domain smoothing is
            // handled by per-sample amplitude ramping in the renderer, not here.
            result.frequencyHz = (nearestTargetHz > 0.0)
                                     ? logInterpHz(sourceFrequencyHz, nearestTargetHz, amount)
                                     : sourceFrequencyHz;
            result.amplitude = sourceAmplitude * std::sqrt(1.0 - amount);
            break;

        case HybridizedPartialKind::UnmatchedTarget:
            // Glide in from nearest source partial (when one is supplied) while fading in
            // with the equal-power (sqrt) law.
            result.frequencyHz = (nearestSourceHz > 0.0)
                                     ? logInterpHz(nearestSourceHz, targetFrequencyHz, amount)
                                     : targetFrequencyHz;
            result.amplitude = targetAmplitude * std::sqrt(amount);
            break;
    }

    result.active = std::isfinite(result.frequencyHz) && std::isfinite(result.amplitude)
                    && result.frequencyHz > 0.0 && result.amplitude > 0.0;
    if (!result.active)
    {
        result.frequencyHz = 0.0;
        result.amplitude = 0.0;
    }
    return result;
}

} // namespace ats

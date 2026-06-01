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
                                                             double morphAmount)
{
    HybridizedPartialRenderResult result;
    const double amount = clampUnit(morphAmount);

    switch (kind)
    {
        case HybridizedPartialKind::Matched:
            result.frequencyHz = sourceFrequencyHz + (targetFrequencyHz - sourceFrequencyHz) * amount;
            result.amplitude = sourceAmplitude + (targetAmplitude - sourceAmplitude) * amount;
            break;

        case HybridizedPartialKind::UnmatchedSource:
            result.frequencyHz = sourceFrequencyHz;
            result.amplitude = sourceAmplitude * (1.0 - amount);
            break;

        case HybridizedPartialKind::UnmatchedTarget:
            result.frequencyHz = targetFrequencyHz;
            result.amplitude = targetAmplitude * amount;
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

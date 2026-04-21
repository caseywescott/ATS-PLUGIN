#include "FrequencyWarp.h"

#include <algorithm>
#include <cmath>

namespace ats
{
namespace
{

constexpr double kMinPositiveFrequencyHz = 0.0;

double nyquist(const EnvelopeAxisSpec& axis)
{
    return axis.sampleRate > 0.0 ? axis.sampleRate * 0.5 : 0.0;
}

double barkFromHz(const double frequencyHz)
{
    return 6.0 * std::asinh(frequencyHz / 600.0);
}

double hzFromBark(const double bark)
{
    return 600.0 * std::sinh(bark / 6.0);
}

double melFromHz(const double frequencyHz)
{
    return 2595.0 * std::log10(1.0 + frequencyHz / 700.0);
}

double hzFromMel(const double mel)
{
    return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
}

double erbFromHz(const double frequencyHz)
{
    return 21.4 * std::log10(1.0 + 0.00437 * frequencyHz);
}

double hzFromErb(const double erb)
{
    return (std::pow(10.0, erb / 21.4) - 1.0) / 0.00437;
}

bool finiteAndNonNegative(const double value)
{
    return std::isfinite(value) && value >= 0.0;
}

void setOk(bool* ok, const bool value)
{
    if (ok)
        *ok = value;
}

} // namespace

bool isValidFrequencyAxis(const EnvelopeAxisSpec& axis)
{
    const double minHz = axisMinFrequencyHz(axis);
    const double maxHz = axisMaxFrequencyHz(axis);
    if (!finiteAndNonNegative(minHz) || !finiteAndNonNegative(maxHz) || maxHz <= minHz)
        return false;

    if (axis.type == EnvelopeFrequencyAxis::NormalizedNyquist && axis.sampleRate <= 0.0)
        return false;

    if (axis.type == EnvelopeFrequencyAxis::CustomNormalized && axis.customMax <= axis.customMin)
        return false;

    return true;
}

double axisMinFrequencyHz(const EnvelopeAxisSpec& axis)
{
    return std::max(kMinPositiveFrequencyHz, axis.minFrequencyHz);
}

double axisMaxFrequencyHz(const EnvelopeAxisSpec& axis)
{
    if (axis.maxFrequencyHz > 0.0)
        return axis.maxFrequencyHz;
    if (axis.sampleRate > 0.0)
        return nyquist(axis);
    return 0.0;
}

double clampFrequencyToAxis(const double frequencyHz, const EnvelopeAxisSpec& axis)
{
    return std::clamp(frequencyHz, axisMinFrequencyHz(axis), axisMaxFrequencyHz(axis));
}

double frequencyToWarped(const double frequencyHz, const EnvelopeAxisSpec& axis, bool* ok)
{
    if (!isValidFrequencyAxis(axis) || !std::isfinite(frequencyHz))
    {
        setOk(ok, false);
        return 0.0;
    }

    const double clamped = clampFrequencyToAxis(frequencyHz, axis);
    setOk(ok, clamped == frequencyHz);

    switch (axis.type)
    {
        case EnvelopeFrequencyAxis::LinearHz:
            return clamped;
        case EnvelopeFrequencyAxis::Bark:
            return barkFromHz(clamped);
        case EnvelopeFrequencyAxis::Mel:
            return melFromHz(clamped);
        case EnvelopeFrequencyAxis::Erb:
            return erbFromHz(clamped);
        case EnvelopeFrequencyAxis::NormalizedNyquist:
            return clamped / nyquist(axis);
        case EnvelopeFrequencyAxis::CustomNormalized:
        {
            const double spanHz = axisMaxFrequencyHz(axis) - axisMinFrequencyHz(axis);
            const double norm = (clamped - axisMinFrequencyHz(axis)) / spanHz;
            return axis.customMin + norm * (axis.customMax - axis.customMin);
        }
    }

    setOk(ok, false);
    return 0.0;
}

double warpedToFrequency(const double warpedValue, const EnvelopeAxisSpec& axis, bool* ok)
{
    if (!isValidFrequencyAxis(axis) || !std::isfinite(warpedValue))
    {
        setOk(ok, false);
        return 0.0;
    }

    double frequencyHz = 0.0;
    switch (axis.type)
    {
        case EnvelopeFrequencyAxis::LinearHz:
            frequencyHz = warpedValue;
            break;
        case EnvelopeFrequencyAxis::Bark:
            frequencyHz = hzFromBark(warpedValue);
            break;
        case EnvelopeFrequencyAxis::Mel:
            frequencyHz = hzFromMel(warpedValue);
            break;
        case EnvelopeFrequencyAxis::Erb:
            frequencyHz = hzFromErb(warpedValue);
            break;
        case EnvelopeFrequencyAxis::NormalizedNyquist:
            frequencyHz = warpedValue * nyquist(axis);
            break;
        case EnvelopeFrequencyAxis::CustomNormalized:
        {
            const double norm = (warpedValue - axis.customMin) / (axis.customMax - axis.customMin);
            frequencyHz = axisMinFrequencyHz(axis) + norm * (axisMaxFrequencyHz(axis) - axisMinFrequencyHz(axis));
            break;
        }
    }

    const double clamped = clampFrequencyToAxis(frequencyHz, axis);
    setOk(ok, std::abs(clamped - frequencyHz) <= 1.0e-9);
    return clamped;
}

double normalizedPositionForFrequency(const double frequencyHz, const EnvelopeAxisSpec& axis, bool* ok)
{
    if (!isValidFrequencyAxis(axis))
    {
        setOk(ok, false);
        return 0.0;
    }

    bool minOk = false;
    bool maxOk = false;
    bool valueOk = false;
    const double minWarped = frequencyToWarped(axisMinFrequencyHz(axis), axis, &minOk);
    const double maxWarped = frequencyToWarped(axisMaxFrequencyHz(axis), axis, &maxOk);
    const double valueWarped = frequencyToWarped(frequencyHz, axis, &valueOk);
    const double span = maxWarped - minWarped;
    if (!minOk || !maxOk || span == 0.0)
    {
        setOk(ok, false);
        return 0.0;
    }

    setOk(ok, valueOk);
    return std::clamp((valueWarped - minWarped) / span, 0.0, 1.0);
}

double frequencyForNormalizedPosition(const double position, const EnvelopeAxisSpec& axis, bool* ok)
{
    if (!isValidFrequencyAxis(axis) || !std::isfinite(position))
    {
        setOk(ok, false);
        return 0.0;
    }

    bool minOk = false;
    bool maxOk = false;
    const double minWarped = frequencyToWarped(axisMinFrequencyHz(axis), axis, &minOk);
    const double maxWarped = frequencyToWarped(axisMaxFrequencyHz(axis), axis, &maxOk);
    if (!minOk || !maxOk)
    {
        setOk(ok, false);
        return 0.0;
    }

    const double clampedPosition = std::clamp(position, 0.0, 1.0);
    const double warped = minWarped + clampedPosition * (maxWarped - minWarped);
    bool inverseOk = false;
    const double frequencyHz = warpedToFrequency(warped, axis, &inverseOk);
    setOk(ok, inverseOk && clampedPosition == position);
    return frequencyHz;
}

} // namespace ats

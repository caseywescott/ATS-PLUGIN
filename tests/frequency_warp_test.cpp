#include "FrequencyWarp.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool closeEnough(const double a, const double b, const double tolerance = 1.0e-6)
{
    const double scale = std::max(1.0, std::max(std::abs(a), std::abs(b)));
    return std::abs(a - b) <= tolerance * scale;
}

ats::EnvelopeAxisSpec makeAxis(const ats::EnvelopeFrequencyAxis type)
{
    ats::EnvelopeAxisSpec axis;
    axis.type = type;
    axis.minFrequencyHz = 20.0;
    axis.maxFrequencyHz = 18000.0;
    axis.sampleRate = 48000.0;
    axis.customMin = -1.0;
    axis.customMax = 1.0;
    return axis;
}

int checkAxis(const ats::EnvelopeFrequencyAxis type)
{
    const auto axis = makeAxis(type);
    if (!ats::isValidFrequencyAxis(axis))
        return fail(std::string("axis was not valid: ") + ats::toString(type));

    const std::vector<double> freqs = { 20.0, 40.0, 100.0, 440.0, 1234.0, 5000.0, 12000.0, 18000.0 };
    double previousWarped = -1.0e30;
    for (const double freq : freqs)
    {
        bool ok = false;
        const double warped = ats::frequencyToWarped(freq, axis, &ok);
        if (!ok)
            return fail(std::string("frequencyToWarped rejected in-range frequency for ") + ats::toString(type));
        if (!(warped > previousWarped))
            return fail(std::string("warped values are not monotonic for ") + ats::toString(type));
        previousWarped = warped;

        bool inverseOk = false;
        const double roundTrip = ats::warpedToFrequency(warped, axis, &inverseOk);
        if (!inverseOk || !closeEnough(roundTrip, freq))
            return fail(std::string("inverse warp mismatch for ") + ats::toString(type));
    }

    bool lowOk = true;
    const double low = ats::frequencyToWarped(5.0, axis, &lowOk);
    bool minOk = false;
    const double min = ats::frequencyToWarped(axis.minFrequencyHz, axis, &minOk);
    if (lowOk || !minOk || !closeEnough(low, min))
        return fail(std::string("low out-of-range clamp failed for ") + ats::toString(type));

    bool highOk = true;
    const double high = ats::frequencyToWarped(30000.0, axis, &highOk);
    bool maxOk = false;
    const double max = ats::frequencyToWarped(axis.maxFrequencyHz, axis, &maxOk);
    if (highOk || !maxOk || !closeEnough(high, max))
        return fail(std::string("high out-of-range clamp failed for ") + ats::toString(type));

    bool posOk = false;
    const double pos = ats::normalizedPositionForFrequency(440.0, axis, &posOk);
    if (!posOk || pos <= 0.0 || pos >= 1.0)
        return fail(std::string("normalized position failed for ") + ats::toString(type));

    bool edgeOk = false;
    const double edgeFreq = ats::frequencyForNormalizedPosition(1.0, axis, &edgeOk);
    if (!edgeOk || !closeEnough(edgeFreq, axis.maxFrequencyHz))
        return fail(std::string("position edge failed for ") + ats::toString(type));

    return 0;
}

} // namespace

int main()
{
    const std::vector<ats::EnvelopeFrequencyAxis> axes = {
        ats::EnvelopeFrequencyAxis::LinearHz,
        ats::EnvelopeFrequencyAxis::Bark,
        ats::EnvelopeFrequencyAxis::Mel,
        ats::EnvelopeFrequencyAxis::Erb,
        ats::EnvelopeFrequencyAxis::NormalizedNyquist,
        ats::EnvelopeFrequencyAxis::CustomNormalized,
    };

    for (const auto axis : axes)
    {
        if (const int result = checkAxis(axis); result != 0)
            return result;
    }

    auto invalid = makeAxis(ats::EnvelopeFrequencyAxis::LinearHz);
    invalid.maxFrequencyHz = invalid.minFrequencyHz;
    if (ats::isValidFrequencyAxis(invalid))
        return fail("invalid zero-span axis accepted");

    auto noSampleRate = makeAxis(ats::EnvelopeFrequencyAxis::NormalizedNyquist);
    noSampleRate.sampleRate = 0.0;
    if (ats::isValidFrequencyAxis(noSampleRate))
        return fail("normalized Nyquist axis accepted missing sample rate");

    bool outOfRangeOk = true;
    const double clamped = ats::frequencyForNormalizedPosition(-0.25, makeAxis(ats::EnvelopeFrequencyAxis::Mel), &outOfRangeOk);
    if (outOfRangeOk || !closeEnough(clamped, 20.0))
        return fail("out-of-range normalized position did not clamp to min");

    std::cout << "ok: frequency warp utilities\n";
    return 0;
}

#include "EnvelopeEvaluation.h"

#include "FrequencyWarp.h"
#include "RegularizedCepstrum.h"

#include <algorithm>
#include <cmath>

namespace ats
{
namespace
{

EnvelopeAxisSpec withFallbackAxis(const EnvelopeAxisSpec& preferred, const EnvelopeAxisSpec& fallback)
{
    if (isValidFrequencyAxis(preferred))
        return preferred;
    return fallback;
}

EnvelopeAxisSpec remapAxisType(EnvelopeAxisSpec axis, const EnvelopeFrequencyAxis type)
{
    axis.type = type;
    if (type == EnvelopeFrequencyAxis::NormalizedNyquist && axis.sampleRate <= 0.0)
        axis.sampleRate = axis.maxFrequencyHz > 0.0 ? axis.maxFrequencyHz * 2.0 : 0.0;
    return axis;
}

bool positionMap(const double inputFrequencyHz,
                 const EnvelopeAxisSpec& sourceAxis,
                 const EnvelopeAxisSpec& targetAxis,
                 double& lookupFrequencyHz,
                 bool& inRange)
{
    bool sourceOk = false;
    const double position = normalizedPositionForFrequency(inputFrequencyHz, sourceAxis, &sourceOk);
    bool targetOk = false;
    lookupFrequencyHz = frequencyForNormalizedPosition(position, targetAxis, &targetOk);
    inRange = sourceOk && targetOk;
    return targetOk;
}

bool directMap(const double inputFrequencyHz,
               const EnvelopeAxisSpec& targetAxis,
               const EnvelopeExtrapolationPolicy extrapolation,
               double& lookupFrequencyHz,
               bool& inRange)
{
    if (!std::isfinite(inputFrequencyHz) || !isValidFrequencyAxis(targetAxis))
        return false;

    const double minHz = axisMinFrequencyHz(targetAxis);
    const double maxHz = axisMaxFrequencyHz(targetAxis);
    inRange = inputFrequencyHz >= minHz && inputFrequencyHz <= maxHz;
    if (!inRange && extrapolation == EnvelopeExtrapolationPolicy::Reject)
    {
        lookupFrequencyHz = inputFrequencyHz;
        return false;
    }

    lookupFrequencyHz = std::clamp(inputFrequencyHz, minHz, maxHz);
    return true;
}

} // namespace

bool mapEnvelopeLookupFrequency(const double inputFrequencyHz,
                                const FrameEnvelope& envelope,
                                const EnvelopeEvaluationOptions& options,
                                double& lookupFrequencyHz,
                                bool& inRange)
{
    lookupFrequencyHz = 0.0;
    inRange = false;

    const auto targetAxis = withFallbackAxis(options.targetAxis, envelope.frequencyAxis);
    const auto sourceAxis = withFallbackAxis(options.sourceAxis, targetAxis);
    if (!isValidFrequencyAxis(targetAxis) || !isValidFrequencyAxis(sourceAxis))
        return false;

    bool mapped = false;
    switch (options.mapping)
    {
        case EnvelopeFrequencyMapping::DirectHz:
            mapped = directMap(inputFrequencyHz, targetAxis, options.extrapolation, lookupFrequencyHz, inRange);
            break;

        case EnvelopeFrequencyMapping::NormalizedNyquist:
        {
            const auto source = remapAxisType(sourceAxis, EnvelopeFrequencyAxis::NormalizedNyquist);
            const auto target = remapAxisType(targetAxis, EnvelopeFrequencyAxis::NormalizedNyquist);
            mapped = positionMap(inputFrequencyHz, source, target, lookupFrequencyHz, inRange);
            break;
        }

        case EnvelopeFrequencyMapping::PerceptualWarp:
        {
            const auto source = remapAxisType(sourceAxis, options.perceptualAxis);
            const auto target = remapAxisType(targetAxis, options.perceptualAxis);
            mapped = positionMap(inputFrequencyHz, source, target, lookupFrequencyHz, inRange);
            break;
        }

        case EnvelopeFrequencyMapping::RangeWarp:
            mapped = positionMap(inputFrequencyHz, remapAxisType(sourceAxis, EnvelopeFrequencyAxis::LinearHz),
                                 remapAxisType(targetAxis, EnvelopeFrequencyAxis::LinearHz),
                                 lookupFrequencyHz, inRange);
            break;
    }

    if (!mapped)
        return false;

    if (!inRange)
    {
        if (options.extrapolation == EnvelopeExtrapolationPolicy::Reject)
            return false;
        lookupFrequencyHz = clampFrequencyToAxis(lookupFrequencyHz, targetAxis);
    }

    return true;
}

EnvelopeEvaluationSample evaluateEnvelopeAtFrequency(const FrameEnvelope& envelope,
                                                     const double inputFrequencyHz,
                                                     const EnvelopeEvaluationOptions& options)
{
    EnvelopeEvaluationSample sample;
    sample.inputFrequencyHz = inputFrequencyHz;

    if (!mapEnvelopeLookupFrequency(inputFrequencyHz, envelope, options, sample.lookupFrequencyHz, sample.inRange))
        return sample;

    bool evalOk = false;
    sample.valueDb = evaluateRegularizedDiscreteCepstrum(envelope, sample.lookupFrequencyHz, &evalOk);
    sample.ok = evalOk;
    return sample;
}

std::vector<EnvelopeEvaluationSample> evaluateEnvelopeAtFrequencies(
    const FrameEnvelope& envelope,
    const std::vector<double>& inputFrequenciesHz,
    const EnvelopeEvaluationOptions& options)
{
    std::vector<EnvelopeEvaluationSample> result;
    result.reserve(inputFrequenciesHz.size());
    for (const double frequencyHz : inputFrequenciesHz)
        result.push_back(evaluateEnvelopeAtFrequency(envelope, frequencyHz, options));
    return result;
}

std::vector<EnvelopeEvaluationSample> evaluateEnvelopeOnGrid(const FrameEnvelope& envelope,
                                                             const int gridSize,
                                                             const EnvelopeEvaluationOptions& options)
{
    std::vector<EnvelopeEvaluationSample> result;
    if (gridSize <= 0)
        return result;

    const auto sourceAxis = withFallbackAxis(options.sourceAxis,
                                            withFallbackAxis(options.targetAxis, envelope.frequencyAxis));
    if (!isValidFrequencyAxis(sourceAxis))
        return result;

    result.reserve(static_cast<std::size_t>(gridSize));
    for (int i = 0; i < gridSize; ++i)
    {
        const double position = gridSize == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(gridSize - 1);
        bool ok = false;
        const double frequencyHz = frequencyForNormalizedPosition(position, sourceAxis, &ok);
        if (!ok)
            continue;
        result.push_back(evaluateEnvelopeAtFrequency(envelope, frequencyHz, options));
    }

    return result;
}

} // namespace ats

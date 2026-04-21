#pragma once

#include "SpectralEnvelope.h"

#include <vector>

namespace ats
{

enum class EnvelopeExtrapolationPolicy
{
    Reject,
    Clamp
};

enum class EnvelopeFrequencyMapping
{
    DirectHz,
    NormalizedNyquist,
    PerceptualWarp,
    RangeWarp
};

struct EnvelopeEvaluationOptions
{
    EnvelopeFrequencyMapping mapping = EnvelopeFrequencyMapping::DirectHz;
    EnvelopeExtrapolationPolicy extrapolation = EnvelopeExtrapolationPolicy::Clamp;
    EnvelopeAxisSpec sourceAxis;
    EnvelopeAxisSpec targetAxis;
    EnvelopeFrequencyAxis perceptualAxis = EnvelopeFrequencyAxis::Bark;
};

struct EnvelopeEvaluationSample
{
    double inputFrequencyHz = 0.0;
    double lookupFrequencyHz = 0.0;
    double valueDb = 0.0;
    bool inRange = false;
    bool ok = false;
};

[[nodiscard]] bool mapEnvelopeLookupFrequency(double inputFrequencyHz,
                                              const FrameEnvelope& envelope,
                                              const EnvelopeEvaluationOptions& options,
                                              double& lookupFrequencyHz,
                                              bool& inRange);

[[nodiscard]] EnvelopeEvaluationSample evaluateEnvelopeAtFrequency(const FrameEnvelope& envelope,
                                                                   double inputFrequencyHz,
                                                                   const EnvelopeEvaluationOptions& options);

[[nodiscard]] std::vector<EnvelopeEvaluationSample> evaluateEnvelopeAtFrequencies(
    const FrameEnvelope& envelope,
    const std::vector<double>& inputFrequenciesHz,
    const EnvelopeEvaluationOptions& options);

[[nodiscard]] std::vector<EnvelopeEvaluationSample> evaluateEnvelopeOnGrid(
    const FrameEnvelope& envelope,
    int gridSize,
    const EnvelopeEvaluationOptions& options);

} // namespace ats

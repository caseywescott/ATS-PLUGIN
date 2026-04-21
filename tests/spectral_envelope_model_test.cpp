#include "SpectralEnvelope.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

bool nearEqual(const double a, const double b)
{
    return std::abs(a - b) < 1.0e-12;
}

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main()
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = "rdc/default";
    envelope.estimatorVersion = 3;
    envelope.sourceModelId = "source\tmodel\none";
    envelope.frameIndex = 42;
    envelope.sampleRate = 48000.0;
    envelope.frequencyAxis.type = ats::EnvelopeFrequencyAxis::Bark;
    envelope.frequencyAxis.minFrequencyHz = 20.0;
    envelope.frequencyAxis.maxFrequencyHz = 18000.0;
    envelope.frequencyAxis.sampleRate = 48000.0;
    envelope.frequencyAxis.customMin = 0.0;
    envelope.frequencyAxis.customMax = 1.0;
    envelope.envelopeOrder = 18;
    envelope.regularization = 0.03125;
    envelope.penaltyType = "second_derivative";
    envelope.storage = ats::EnvelopeValueStorage::Coefficients;
    envelope.values = { -12.5, 0.25, 1.0 / 3.0, -0.0009765625 };
    envelope.diagnostics.fitPointCount = 9;
    envelope.diagnostics.weightedError = 0.004;
    envelope.diagnostics.conditionEstimate = 12.25;
    envelope.diagnostics.clippedOrFlooredCount = 2;
    envelope.diagnostics.fallbackReason = ats::EnvelopeFallbackReason::TooFewPoints;
    envelope.diagnostics.addWarning(ats::EnvelopeWarningSparseFrame, "sparse frame");
    envelope.diagnostics.addWarning(ats::EnvelopeWarningAmplitudeFloor, "floored\tamp");
    envelope.smoothingGroup = 7;
    envelope.smoothingAmount = 0.2;

    const std::string first = envelope.serialize();
    const std::string second = envelope.serialize();
    if (first != second)
        return fail("serialization is not deterministic");

    ats::FrameEnvelope roundTrip;
    std::string error;
    if (!ats::FrameEnvelope::deserialize(first, roundTrip, &error))
        return fail("deserialize failed: " + error);

    if (roundTrip.estimatorId != envelope.estimatorId)
        return fail("estimator id mismatch");
    if (roundTrip.estimatorVersion != envelope.estimatorVersion)
        return fail("estimator version mismatch");
    if (roundTrip.sourceModelId != envelope.sourceModelId)
        return fail("source model id mismatch");
    if (roundTrip.frameIndex != envelope.frameIndex)
        return fail("frame index mismatch");
    if (!nearEqual(roundTrip.sampleRate, envelope.sampleRate))
        return fail("sample rate mismatch");
    if (roundTrip.frequencyAxis.type != envelope.frequencyAxis.type)
        return fail("axis type mismatch");
    if (!nearEqual(roundTrip.frequencyAxis.maxFrequencyHz, envelope.frequencyAxis.maxFrequencyHz))
        return fail("axis max mismatch");
    if (roundTrip.envelopeOrder != envelope.envelopeOrder)
        return fail("order mismatch");
    if (roundTrip.penaltyType != envelope.penaltyType)
        return fail("penalty mismatch");
    if (roundTrip.storage != envelope.storage)
        return fail("storage mismatch");
    if (roundTrip.values.size() != envelope.values.size())
        return fail("value count mismatch");
    for (std::size_t i = 0; i < envelope.values.size(); ++i)
    {
        if (!nearEqual(roundTrip.values[i], envelope.values[i]))
            return fail("value mismatch");
    }

    if (roundTrip.diagnostics.fitPointCount != envelope.diagnostics.fitPointCount)
        return fail("fit point count mismatch");
    if (!nearEqual(roundTrip.diagnostics.weightedError, envelope.diagnostics.weightedError))
        return fail("weighted error mismatch");
    if (roundTrip.diagnostics.fallbackReason != envelope.diagnostics.fallbackReason)
        return fail("fallback reason mismatch");
    if (!roundTrip.diagnostics.hasWarning(ats::EnvelopeWarningSparseFrame))
        return fail("missing sparse-frame warning flag");
    if (!roundTrip.diagnostics.hasWarning(ats::EnvelopeWarningAmplitudeFloor))
        return fail("missing amplitude-floor warning flag");
    if (roundTrip.diagnostics.warnings.size() != 2)
        return fail("warning count mismatch");

    const std::string roundTripText = roundTrip.serialize();
    if (roundTripText != first)
        return fail("round-trip serialization changed bytes");

    std::cout << "ok: spectral envelope model serialization\n";
    return 0;
}

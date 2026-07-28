#pragma once

#include "../core/AtsSpectralEnvelope.h"

namespace ats::dsp
{

// Neutral service name for the envelope operations used by morphing and
// frequency-shape processing. The numerical implementation remains in core.
class SpectralEnvelopeService
{
public:
    using Envelope = ats::BarkSpectralEnvelope;
    using FormantMorph = ats::FormantAlignedEnvelopeMorph;
    using TransferResult = ats::SpectralEnvelopeTransferResult;

    [[nodiscard]] static TransferResult transfer(double sourceAmplitude,
                                                 double sourceEnvelopeAmplitude,
                                                 double targetEnvelopeAmplitude,
                                                 double amount,
                                                 double amplitudeFloor = Envelope::kDefaultAmplitudeFloor)
    { return ats::applySpectralEnvelopeTransfer(sourceAmplitude, sourceEnvelopeAmplitude,
                                                 targetEnvelopeAmplitude, amount, amplitudeFloor); }

    [[nodiscard]] static double hzToBark(double frequencyHz)
    { return Envelope::hzToBark(frequencyHz); }
};

} // namespace ats::dsp

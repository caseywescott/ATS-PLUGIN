#include "../src/dsp/SpectralEnvelopeService.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::dsp::SpectralEnvelopeService::Envelope envelope;
    envelope.build({ { 120.0, 0.2 }, { 9000.0, 0.8 } });
    assert(envelope.isValid());
    assert(std::isfinite(envelope.sampleAtHz(2500.0)));
    assert(ats::dsp::SpectralEnvelopeService::hzToBark(440.0) > 0.0);

    const auto transfer = ats::dsp::SpectralEnvelopeService::transfer(0.1, 1.0, 2.0, 1.0);
    assert(transfer.ok && std::abs(transfer.outputAmplitude - 0.2) < 1.0e-12);
    return 0;
}

#include "../src/audio/MeterPublisher.h"
#include "../src/audio/SpectrumDataPublisher.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::audio::MeterPublisher meter;
    meter.publishAudioRt(0.75f, 0.25f, 64, 3, true);
    const auto meterSnapshot = meter.snapshot();
    assert(meterSnapshot.peak == 0.75f && meterSnapshot.rms == 0.25f);
    assert(meterSnapshot.frameCount == 64 && meterSnapshot.revision == 3 && meterSnapshot.finite);
    meter.publishAudioRt(NAN, INFINITY, 128, 4, true);
    assert(!meter.snapshot().finite);

    ats::audio::SpectrumDataPublisher spectrum;
    const float values[] { 1.0f, 2.0f, 3.0f };
    spectrum.publish(values, 3, 9);
    const auto spectrumSnapshot = spectrum.snapshot();
    assert(spectrumSnapshot.count == 3 && spectrumSnapshot.revision == 9);
    assert(spectrumSnapshot.values[0] == 1.0f && spectrumSnapshot.values[2] == 3.0f);
    spectrum.publish(nullptr, 900, 10);
    assert(spectrum.snapshot().count == ats::audio::SpectrumSnapshot::kMaxPoints);
    return 0;
}

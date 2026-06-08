#include "TimbreFrameAdapter.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_frame_adapter_test: " << message << '\n';
    return 1;
}

ats::EnvelopeFrameInput makeMixedFrame()
{
    ats::EnvelopeFrameInput frame;
    frame.sourceModelId = "fixture-mix";
    frame.frameIndex = 7;
    frame.sampleRate = 48000.0;
    frame.fundamentalFrequencyHz = 220.0;
    frame.f0Confidence = 0.95;

    // Intentionally unsorted partials with one invalid (negative freq), one
    // below floor, one deselected, plus three good ones.
    frame.partials = {
        { 3000.0, 0.5, 20.0, 1.0, true },   // good high
        { 500.0,  0.7, 20.0, 0.5, true },   // good low
        { 1500.0, 0.3, 20.0, 1.0, false },  // deselected
        { -50.0,  0.4, 20.0, 1.0, true },   // negative freq -> invalid
        { 2000.0, 1.0e-15, 20.0, 1.0, true }, // below floor
        { 1200.0, std::numeric_limits<double>::quiet_NaN(), 20.0, 1.0, true }, // NaN amp
        { 800.0,  0.2, 20.0, 1.0, true },   // good middle
    };

    frame.residualBands = {
        { 4000.0, 0.1, 1.0, false },
        { -10.0,  0.2, 1.0, false }, // invalid freq
        { 500.0,  std::numeric_limits<double>::infinity(), 1.0, false }, // invalid energy
        { 250.0,  0.05, 1.0, true },  // orphan
        { 8000.0, 0.0,  1.0, false }, // zero -> kept with zero energy
    };
    return frame;
}

} // namespace

int main()
{
    {
        // Empty frame: every count is zero, status fields are sane.
        ats::EnvelopeFrameInput frame;
        const auto validated = ats::validateEnvelopeFrameInput(frame);
        if (!validated.partials.empty())
            return fail("empty frame produced partials");
        if (!validated.residualBands.empty())
            return fail("empty frame produced residual bands");
        if (validated.partialEnergyTotal != 0.0)
            return fail("empty frame partialEnergyTotal must be 0");
        if (validated.residualBandsPresent)
            return fail("empty frame residualBandsPresent must be false");
    }

    {
        // Mixed frame: validates sorting, filtering, selection masks, and
        // amplitude floor handling.
        const auto frame = makeMixedFrame();
        const auto validated = ats::validateEnvelopeFrameInput(frame);

        if (validated.partials.size() != 3)
            return fail("mixed frame: expected 3 valid partials");
        for (std::size_t i = 1; i < validated.partials.size(); ++i)
        {
            if (validated.partials[i - 1].frequencyHz > validated.partials[i].frequencyHz)
                return fail("partials are not sorted by frequency");
        }
        // Validated partials are at 500, 800, 3000 Hz with amplitudes
        // 0.7, 0.2, 0.5 respectively.
        if (std::abs(validated.partials[0].frequencyHz - 500.0) > 1.0e-9
            || std::abs(validated.partials[1].frequencyHz - 800.0) > 1.0e-9
            || std::abs(validated.partials[2].frequencyHz - 3000.0) > 1.0e-9)
        {
            return fail("partial frequencies after filtering are incorrect");
        }
        const double expectedTotalEnergy = 0.7 * 0.7 + 0.2 * 0.2 + 0.5 * 0.5;
        if (std::abs(validated.partialEnergyTotal - expectedTotalEnergy) > 1.0e-9)
            return fail("partialEnergyTotal is incorrect");
        if (validated.invalidPartialCount != 4)
            return fail("expected 4 invalid partials (deselected, neg freq, sub-floor, NaN)");

        if (!validated.residualBandsPresent)
            return fail("residualBandsPresent should be true");
        // Two invalid bands rejected; three kept (one with zero energy).
        if (validated.residualBands.size() != 3)
            return fail("expected 3 kept residual bands");
        if (validated.invalidResidualCount != 2)
            return fail("expected 2 invalid residual bands");
        // Sorted: 250, 4000, 8000
        if (std::abs(validated.residualBands[0].frequencyHz - 250.0) > 1.0e-9
            || std::abs(validated.residualBands[1].frequencyHz - 4000.0) > 1.0e-9
            || std::abs(validated.residualBands[2].frequencyHz - 8000.0) > 1.0e-9)
        {
            return fail("residual band frequencies after filtering are incorrect");
        }
        if (!validated.residualBands[0].orphanBand)
            return fail("orphan flag not preserved");
        if (std::abs(validated.residualBands[2].energy) > 1.0e-12)
            return fail("zero-energy band must retain zero energy");
        if (std::abs(validated.residualEnergyTotal - (0.05 + 0.1)) > 1.0e-9)
            return fail("residualEnergyTotal is incorrect");

        if (validated.sourceModelId != "fixture-mix")
            return fail("sourceModelId not propagated");
        if (validated.frameIndex != 7)
            return fail("frameIndex not propagated");
        if (validated.fundamentalFrequencyHz != 220.0)
            return fail("fundamentalFrequencyHz not propagated");
        if (validated.f0Confidence != 0.95)
            return fail("f0Confidence not propagated");
    }

    {
        // populateEnvelopeFrameInput against raw partial / residual arrays
        // produces the same shape as collectFrame in PluginProcessor.
        constexpr int partialCount = 3;
        constexpr int bandCount = 4;
        constexpr int frameIndex = 2;

        std::vector<std::vector<double>> ampSeries(partialCount, std::vector<double>(5, 0.0));
        std::vector<std::vector<double>> freqSeries(partialCount, std::vector<double>(5, 0.0));
        ampSeries[0][frameIndex] = 0.5;
        ampSeries[1][frameIndex] = 0.0;       // dropped: zero amp
        ampSeries[2][frameIndex] = 1.0;
        freqSeries[0][frameIndex] = 1000.0;
        freqSeries[1][frameIndex] = 2000.0;
        freqSeries[2][frameIndex] = 3000.0;

        std::vector<const double*> ampPtrs(partialCount);
        std::vector<const double*> freqPtrs(partialCount);
        for (int i = 0; i < partialCount; ++i)
        {
            ampPtrs[i] = ampSeries[i].data();
            freqPtrs[i] = freqSeries[i].data();
        }

        std::vector<std::vector<double>> bandSeries(bandCount, std::vector<double>(5, 0.0));
        bandSeries[0][frameIndex] = 0.0;  // dropped: zero
        bandSeries[1][frameIndex] = 0.2;
        bandSeries[2][frameIndex] = 0.1;
        bandSeries[3][frameIndex] = std::numeric_limits<double>::quiet_NaN(); // dropped: NaN
        std::vector<const double*> bandPtrs(bandCount);
        for (int i = 0; i < bandCount; ++i)
            bandPtrs[i] = bandSeries[i].data();

        ats::PartialExtractionInput partials;
        partials.frameIndex = frameIndex;
        partials.partialCount = partialCount;
        partials.amplitudeByPartial = ampPtrs.data();
        partials.frequencyByPartial = freqPtrs.data();

        ats::ResidualExtractionInput residual;
        residual.frameIndex = frameIndex;
        residual.bandCount = bandCount;
        residual.energyByBand = bandPtrs.data();
        residual.bandCenterFrequencyHz = [](int bandIndex) {
            return 200.0 * (bandIndex + 1);
        };
        residual.bandIsOrphan = [](int bandIndex) { return bandIndex == 1; };

        ats::EnvelopeFrameInput out;
        ats::populateEnvelopeFrameInput(
            partials, &residual, 48000.0, "raw-test", false, 220.0, 0.9, 1.0e-12, out);

        if (out.partials.size() != 2)
            return fail("populateEnvelopeFrameInput: expected 2 partials");
        if (out.residualBands.size() != 2)
            return fail("populateEnvelopeFrameInput: expected 2 residual bands");
        if (out.partials[0].frequencyHz != 1000.0 || out.partials[1].frequencyHz != 3000.0)
            return fail("populateEnvelopeFrameInput: wrong partial frequencies");
        if (out.sourceModelId != "raw-test")
            return fail("populateEnvelopeFrameInput: sourceModelId mismatch");
        if (out.frameIndex != static_cast<std::uint64_t>(frameIndex))
            return fail("populateEnvelopeFrameInput: frameIndex mismatch");
        if (out.fundamentalFrequencyHz != 220.0)
            return fail("populateEnvelopeFrameInput: f0 mismatch");
        if (!out.residualBands[0].orphanBand && !out.residualBands[1].orphanBand)
            return fail("populateEnvelopeFrameInput: orphan flag never set");

        // Validate the resulting frame again.
        const auto validated = ats::validateEnvelopeFrameInput(out);
        if (validated.partials.size() != 2)
            return fail("populated + validated must keep 2 partials");
    }

    {
        // validatePartialTracks: empty track is dropped; track with at least
        // one present sample is kept.
        ats::PartialTrack empty;
        empty.trackId = 1;
        ats::PartialTrack good;
        good.trackId = 2;
        good.samples = {
            { 0.0,  0.0,  1000.0, false },
            { 0.01, 0.5,  1000.0, true  },
            { 0.02, 0.3,  1000.0, true  },
        };
        ats::PartialTrack bad;
        bad.trackId = 3;
        bad.samples = {
            { 0.0, std::numeric_limits<double>::quiet_NaN(), 1000.0, true },
        };

        const auto result = ats::validatePartialTracks({ empty, good, bad });
        if (result.tracks.size() != 1)
            return fail("validatePartialTracks must keep the one valid track");
        if (result.tracks[0].trackId != 2)
            return fail("validatePartialTracks must keep the good track id");
        if (result.droppedTrackCount != 2)
            return fail("validatePartialTracks must drop two tracks");
        // The bad sample is rewritten to present=false.
        if (result.tracks[0].samples[0].present)
            return fail("first sample (zero amplitude) should be marked absent");
    }

    std::cout << "timbre_frame_adapter_test: ok\n";
    return 0;
}

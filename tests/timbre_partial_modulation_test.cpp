#include "TimbreDescriptors.h"
#include "TimbreFrameAdapter.h"
#include "TimbreSeriesAnalysis.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_partial_modulation_test: " << message << '\n';
    return 1;
}

ats::PartialTrack makeAmTrack(
    std::uint64_t id,
    double carrierFrequencyHz,
    double meanAmplitude,
    double modulationRateHz,
    double modulationDepth,
    int sampleCount,
    double hopSeconds)
{
    ats::PartialTrack track;
    track.trackId = id;
    track.samples.reserve(static_cast<std::size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i)
    {
        ats::PartialTrackSample sample;
        sample.timeSeconds = i * hopSeconds;
        sample.frequencyHz = carrierFrequencyHz;
        sample.amplitude = meanAmplitude
            * (1.0 + modulationDepth
                  * std::sin(2.0 * M_PI * modulationRateHz * sample.timeSeconds));
        sample.present = true;
        track.samples.push_back(sample);
    }
    return track;
}

ats::PartialTrack makeFmTrack(
    std::uint64_t id,
    double meanFrequencyHz,
    double modulationRateHz,
    double extentHz,
    double amplitude,
    int sampleCount,
    double hopSeconds)
{
    ats::PartialTrack track;
    track.trackId = id;
    track.samples.reserve(static_cast<std::size_t>(sampleCount));
    for (int i = 0; i < sampleCount; ++i)
    {
        ats::PartialTrackSample sample;
        sample.timeSeconds = i * hopSeconds;
        sample.frequencyHz = meanFrequencyHz
            + (extentHz * 0.5)
              * std::sin(2.0 * M_PI * modulationRateHz * sample.timeSeconds);
        sample.amplitude = amplitude;
        sample.present = true;
        track.samples.push_back(sample);
    }
    return track;
}

} // namespace

int main()
{
    ats::PartialModulationOptions options;
    options.trajectoryRateHz = 100.0; // 10 ms hop -> Nyquist = 50 Hz
    options.minimumTrackCount = 1;
    options.minimumSampleCount = 8;
    options.minimumAmDepth = 0.05;
    options.minimumFmExtentHz = 1.0;

    const double hop = 0.010;

    // ---- Tremolo: AM at 5 Hz, depth 0.5. Rate should be near 5 Hz.
    //      Zero-crossing-based estimation has roughly 1 Hz resolution at
    //      sub-second durations so we use a 1 Hz tolerance. ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeAmTrack(1, 1000.0, 1.0, 5.0, 0.5, 64, hop),
        };
        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzePartialModulation(tracks, options, series);
        if (series.partialAmRateHz.status != ats::TimbreDescriptorStatus::Available)
            return fail("tremolo: AM rate must be Available");
        if (std::abs(series.partialAmRateHz.value - 5.0) > 1.0)
            return fail("tremolo: AM rate must be within 1 Hz of 5 Hz");
        if (series.partialFmRateHz.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("tremolo: FM rate must be TooSparse (no FM)");
        if (diag.qualifyingAmTrackCount != 1)
            return fail("tremolo: qualifying AM tracks must be 1");
    }

    // ---- Vibrato: FM at 6 Hz, extent 8 Hz. Rate should be near 6 Hz. ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeFmTrack(1, 440.0, 6.0, 8.0, 0.5, 64, hop),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialModulation(tracks, options, series);
        if (series.partialFmRateHz.status != ats::TimbreDescriptorStatus::Available)
            return fail("vibrato: FM rate must be Available");
        if (std::abs(series.partialFmRateHz.value - 6.0) > 1.0)
            return fail("vibrato: FM rate must be within 1 Hz of 6 Hz");
        if (series.partialAmRateHz.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("vibrato: AM rate must be TooSparse");
    }

    // ---- Common envelope: two tracks with the same AM rate -> the energy-
    //      weighted mean should resolve to that rate. ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeAmTrack(1, 1000.0, 1.0, 4.0, 0.5, 64, hop),
            makeAmTrack(2, 2000.0, 0.5, 4.0, 0.4, 64, hop),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialModulation(tracks, options, series);
        if (std::abs(series.partialAmRateHz.value - 4.0) > 1.0)
            return fail("common envelope: AM rate must be near 4 Hz");
    }

    // ---- Independent flutter: two tracks with very different AM rates ->
    //      the weighted mean falls somewhere between them. The point of this
    //      test is to confirm that we never report a rate that is below the
    //      smaller or above the larger contributor (no out-of-range values).
    {
        std::vector<ats::PartialTrack> tracks = {
            makeAmTrack(1, 1000.0, 1.0, 4.0, 0.5, 64, hop),
            makeAmTrack(2, 2000.0, 1.0, 12.0, 0.4, 64, hop),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialModulation(tracks, options, series);
        if (series.partialAmRateHz.status != ats::TimbreDescriptorStatus::Available)
            return fail("flutter: status must be Available");
        if (series.partialAmRateHz.value < 4.0 - 1.0
            || series.partialAmRateHz.value > 12.0 + 1.0)
        {
            return fail("flutter: AM rate must lie between the two carriers");
        }
    }

    // ---- Insufficient duration: track has fewer than minimumSampleCount
    //      samples -> TooSparse on both descriptors. ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeAmTrack(1, 1000.0, 1.0, 5.0, 0.5, 5, hop),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialModulation(tracks, options, series);
        if (series.partialAmRateHz.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("insufficient duration: AM must be TooSparse");
        if (series.partialFmRateHz.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("insufficient duration: FM must be TooSparse");
    }

    // ---- Nyquist clamp: AM at 100 Hz exceeds the 50 Hz series Nyquist. The
    //      reported rate must not exceed Nyquist. ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeAmTrack(1, 1000.0, 1.0, 100.0, 0.5, 128, hop),
        };
        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzePartialModulation(tracks, options, series);
        if (series.partialAmRateHz.status == ats::TimbreDescriptorStatus::Available
            && series.partialAmRateHz.value > diag.seriesNyquistHz + 1.0e-9)
        {
            return fail("nyquist: AM rate exceeds series Nyquist");
        }
    }

    // ---- No trajectory rate -> TooSparse ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeAmTrack(1, 1000.0, 1.0, 5.0, 0.5, 64, hop),
        };
        ats::PartialModulationOptions noRate = options;
        noRate.trajectoryRateHz = 0.0;
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialModulation(tracks, noRate, series);
        if (series.partialAmRateHz.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("no trajectory rate: must be TooSparse");
    }

    // ---- Empty input -> EmptyInput ----
    {
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialModulation({}, options, series);
        if (series.partialAmRateHz.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty: AM rate must be EmptyInput");
    }

    std::cout << "timbre_partial_modulation_test: ok\n";
    return 0;
}

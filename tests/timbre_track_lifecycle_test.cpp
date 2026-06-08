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
    std::cerr << "timbre_track_lifecycle_test: " << message << '\n';
    return 1;
}

// Make a track whose amplitude curve rises from `riseStart` to peak `peakAmp`
// at `peakTime` and decays back to zero by `endTime`. Samples are placed at
// `hop` interval.
ats::PartialTrack makeTriangleTrack(
    std::uint64_t trackId,
    double onsetTime,
    double peakTime,
    double endTime,
    double peakAmp,
    double frequencyHz,
    double hop = 0.005)
{
    ats::PartialTrack track;
    track.trackId = trackId;
    for (double t = 0.0; t <= endTime + 1.0e-9; t += hop)
    {
        ats::PartialTrackSample sample;
        sample.timeSeconds = t;
        sample.frequencyHz = frequencyHz;
        if (t < onsetTime || t > endTime)
        {
            sample.amplitude = 0.0;
            sample.present = false;
        }
        else if (t <= peakTime)
        {
            const double frac = (t - onsetTime) / std::max(peakTime - onsetTime, 1.0e-9);
            sample.amplitude = peakAmp * frac;
            sample.present = sample.amplitude > 0.0;
        }
        else
        {
            const double frac = (t - peakTime) / std::max(endTime - peakTime, 1.0e-9);
            sample.amplitude = peakAmp * (1.0 - frac);
            sample.present = sample.amplitude > 0.0;
        }
        track.samples.push_back(sample);
    }
    return track;
}

} // namespace

int main()
{
    ats::PartialLifecycleOptions options;
    options.trackOnsetThresholdRatio = 0.1;
    options.trackReleaseThresholdRatio = 0.1;

    // ---- Synchronous attack: every track onsets at the same time ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeTriangleTrack(1, 0.0, 0.05, 0.20, 1.0, 1000.0),
            makeTriangleTrack(2, 0.0, 0.05, 0.20, 0.8, 2000.0),
            makeTriangleTrack(3, 0.0, 0.05, 0.20, 0.6, 3000.0),
        };
        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzePartialTrackLifecycle(tracks, options, series);
        if (series.partialOnsetAsynchronySeconds.status != ats::TimbreDescriptorStatus::Available)
            return fail("synchronous: status must be Available");
        if (std::abs(series.partialOnsetAsynchronySeconds.value) > 1.0e-6)
            return fail("synchronous: onset asynchrony must be ~0");
        if (std::abs(series.partialReleaseAsynchronySeconds.value) > 1.0e-6)
            return fail("synchronous: release asynchrony must be ~0");
        if (diag.qualifyingTrackCount != 3)
            return fail("synchronous: qualifying tracks must be 3");
    }

    // ---- Asynchronous attack: tracks onset at staggered times ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeTriangleTrack(1, 0.000, 0.05, 0.20, 1.0, 1000.0),
            makeTriangleTrack(2, 0.010, 0.06, 0.21, 1.0, 2000.0),
            makeTriangleTrack(3, 0.020, 0.07, 0.22, 1.0, 3000.0),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialTrackLifecycle(tracks, options, series);
        if (series.partialOnsetAsynchronySeconds.status != ats::TimbreDescriptorStatus::Available)
            return fail("async: onset must be Available");
        if (!(series.partialOnsetAsynchronySeconds.value > 0.005))
            return fail("async: onset asynchrony must be > 0");
    }

    // ---- Diffuse release: tracks release at very different times ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeTriangleTrack(1, 0.0, 0.05, 0.10, 1.0, 1000.0),
            makeTriangleTrack(2, 0.0, 0.05, 0.30, 1.0, 2000.0),
            makeTriangleTrack(3, 0.0, 0.05, 0.50, 1.0, 3000.0),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialTrackLifecycle(tracks, options, series);
        if (!(series.partialReleaseAsynchronySeconds.value > 0.02))
            return fail("diffuse: release asynchrony must be > 20 ms");
        // Onset is synchronous so onset asynchrony should be ~0
        if (std::abs(series.partialOnsetAsynchronySeconds.value) > 1.0e-3)
            return fail("diffuse: onset asynchrony should be ~0");
    }

    // ---- Short-lived texture: many tracks but each only 2 samples; below
    //      minimum amplitude integral -> TooSparse ----
    {
        std::vector<ats::PartialTrack> tracks;
        for (int i = 0; i < 3; ++i)
        {
            ats::PartialTrack track;
            track.trackId = static_cast<std::uint64_t>(i);
            track.samples = {
                { 0.0,  1.0e-9, 1000.0 + 500.0 * i, true },
                { 0.01, 1.0e-9, 1000.0 + 500.0 * i, true },
            };
            tracks.push_back(track);
        }
        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzePartialTrackLifecycle(tracks, options, series);
        if (series.partialOnsetAsynchronySeconds.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("short texture: must be TooSparse");
        if (diag.qualifyingTrackCount != 0)
            return fail("short texture: no qualifying tracks expected");
    }

    // ---- Sustained tracks: each long enough to qualify; both descriptors ok ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeTriangleTrack(1, 0.000, 0.10, 0.40, 1.0, 1000.0, 0.01),
            makeTriangleTrack(2, 0.000, 0.10, 0.40, 0.8, 2000.0, 0.01),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialTrackLifecycle(tracks, options, series);
        if (series.partialOnsetAsynchronySeconds.status != ats::TimbreDescriptorStatus::Available)
            return fail("sustained: onset must be Available");
        if (series.partialReleaseAsynchronySeconds.status != ats::TimbreDescriptorStatus::Available)
            return fail("sustained: release must be Available");
    }

    // ---- Empty tracks -> EmptyInput ----
    {
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialTrackLifecycle({}, options, series);
        if (series.partialOnsetAsynchronySeconds.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty: must be EmptyInput");
    }

    // ---- Single track -> TooSparse (need at least 2 tracks for asynchrony) ----
    {
        std::vector<ats::PartialTrack> tracks = {
            makeTriangleTrack(1, 0.0, 0.05, 0.20, 1.0, 1000.0),
        };
        ats::TimbreDescriptorSeries series;
        ats::analyzePartialTrackLifecycle(tracks, options, series);
        if (series.partialOnsetAsynchronySeconds.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("single track: must be TooSparse");
    }

    std::cout << "timbre_track_lifecycle_test: ok\n";
    return 0;
}

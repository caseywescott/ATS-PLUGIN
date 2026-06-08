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
    std::cerr << "timbre_residual_motion_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorOptions makeOptions()
{
    ats::TimbreDescriptorOptions opts;
    opts.comparisonAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    opts.comparisonAxis.minFrequencyHz = 100.0;
    opts.comparisonAxis.maxFrequencyHz = 8100.0;
    opts.comparisonAxis.sampleRate = 48000.0;
    opts.comparisonGridSize = 32;
    return opts;
}

ats::SeriesFrameContext frameWithResidual(
    int index,
    double time,
    const std::vector<std::pair<double, double>>& bands,
    bool transientBoundary = false)
{
    ats::EnvelopeFrameInput raw;
    raw.sourceModelId = "s";
    raw.frameIndex = static_cast<std::uint64_t>(index);
    raw.sampleRate = 48000.0;
    for (const auto& [freq, energy] : bands)
        raw.residualBands.push_back({ freq, energy, 1.0, false });
    ats::SeriesFrameContext context;
    context.samples = ats::validateEnvelopeFrameInput(raw);
    context.timeSeconds = time;
    context.transientBoundary = transientBoundary;
    return context;
}

ats::SeriesFrameContext frameWithoutResidual(int index, double time)
{
    ats::EnvelopeFrameInput raw;
    raw.sourceModelId = "s";
    raw.frameIndex = static_cast<std::uint64_t>(index);
    raw.sampleRate = 48000.0;
    raw.partials.push_back({ 1000.0, 0.5, 20.0, 1.0, true });
    ats::SeriesFrameContext context;
    context.samples = ats::validateEnvelopeFrameInput(raw);
    context.timeSeconds = time;
    return context;
}

} // namespace

int main()
{
    const auto options = makeOptions();
    ats::TimbreSeriesAlignmentOptions alignment;
    ats::ResidualMotionOptions motion;
    motion.minimumFrameCount = 4;

    // ---- Steady hiss: identical residual bands across frames -> variation
    //      ~ 0, stationarity ~ 1. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 6; ++i)
        {
            frames.push_back(frameWithResidual(i, i * 0.025,
                { { 500.0, 0.5 }, { 2000.0, 0.3 }, { 5000.0, 0.2 } }));
        }
        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzeResidualMotion(
            frames, options, alignment, motion, series);
        if (series.residualSpectralVariation.status != ats::TimbreDescriptorStatus::Available)
            return fail("steady: variation must be Available");
        if (std::abs(series.residualSpectralVariation.value) > 1.0e-9)
            return fail("steady: variation must be ~0");
        if (std::abs(series.residualStationarity.value - 1.0) > 1.0e-9)
            return fail("steady: stationarity must be ~1");
        if (diag.contributingPairs <= 0)
            return fail("steady: must have contributing lag pairs");
    }

    // ---- Moving noise color: residual band centre drifts ->
    //      variation > 0; stationarity drops below 1. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 6; ++i)
        {
            const double t = static_cast<double>(i) / 5.0;
            const double low = 1.0 - t;
            const double high = t;
            frames.push_back(frameWithResidual(i, i * 0.025,
                { { 500.0, low }, { 5000.0, high } }));
        }
        ats::TimbreDescriptorSeries series;
        ats::analyzeResidualMotion(frames, options, alignment, motion, series);
        if (!(series.residualSpectralVariation.value > 0.01))
            return fail("moving color: variation must be > 0.01");
        if (!(series.residualStationarity.value < 0.99))
            return fail("moving color: stationarity must be < 0.99");
    }

    // ---- Scrape-like burst: abrupt change across one frame -> variation
    //      jumps; with transient reset on, the burst frame is skipped from
    //      the variation accumulator, and stationarity (lag=1) also skips it.
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 4; ++i)
        {
            frames.push_back(frameWithResidual(i, i * 0.025,
                { { 500.0, 0.5 }, { 2000.0, 0.3 } }));
        }
        // Transient boundary at frame 4.
        frames.push_back(frameWithResidual(4, 4 * 0.025,
            { { 6000.0, 1.0 } }, /*transientBoundary=*/true));
        for (int i = 5; i < 8; ++i)
        {
            frames.push_back(frameWithResidual(i, i * 0.025,
                { { 6000.0, 1.0 } }));
        }
        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzeResidualMotion(
            frames, options, alignment, motion, series);
        if (series.residualSpectralVariation.status != ats::TimbreDescriptorStatus::Available)
            return fail("burst: variation must be Available");
        if (diag.contributingPairs >= diag.totalPairs)
            return fail("burst: transient reset must drop at least one pair");
    }

    // ---- No residual data at all -> MissingResidual on both descriptors. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 5; ++i)
            frames.push_back(frameWithoutResidual(i, i * 0.025));
        ats::TimbreDescriptorSeries series;
        ats::analyzeResidualMotion(frames, options, alignment, motion, series);
        if (series.residualSpectralVariation.status != ats::TimbreDescriptorStatus::MissingResidual)
            return fail("no residual: variation must be MissingResidual");
        if (series.residualStationarity.status != ats::TimbreDescriptorStatus::MissingResidual)
            return fail("no residual: stationarity must be MissingResidual");
    }

    // ---- Too sparse -> TooSparse ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 3; ++i)
        {
            frames.push_back(frameWithResidual(i, i * 0.025,
                { { 500.0, 0.5 }, { 2000.0, 0.3 } }));
        }
        ats::TimbreDescriptorSeries series;
        ats::analyzeResidualMotion(frames, options, alignment, motion, series);
        if (series.residualSpectralVariation.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("too sparse: variation must be TooSparse");
    }

    // ---- Lag clamping: stationarity with lag larger than series length
    //      returns EmptyInput (no pairs). ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 5; ++i)
        {
            frames.push_back(frameWithResidual(i, i * 0.025,
                { { 500.0, 0.5 }, { 2000.0, 0.3 } }));
        }
        ats::ResidualMotionOptions hugeLag = motion;
        hugeLag.stationarityLagFrames = 10;
        ats::TimbreDescriptorSeries series;
        ats::analyzeResidualMotion(frames, options, alignment, hugeLag, series);
        if (series.residualStationarity.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("huge lag: stationarity must be EmptyInput");
    }

    std::cout << "timbre_residual_motion_test: ok\n";
    return 0;
}

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
    std::cerr << "timbre_series_alignment_test: " << message << '\n';
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

ats::SeriesFrameContext makeFrame(
    const std::string& sourceId,
    std::uint64_t index,
    double timeSeconds,
    const std::vector<std::pair<double, double>>& partials,
    bool transientBoundary = false)
{
    ats::EnvelopeFrameInput raw;
    raw.sourceModelId = sourceId;
    raw.frameIndex = index;
    raw.sampleRate = 48000.0;
    for (const auto& [freq, amp] : partials)
        raw.partials.push_back({ freq, amp, 20.0, 1.0, true });
    ats::SeriesFrameContext context;
    context.samples = ats::validateEnvelopeFrameInput(raw);
    context.timeSeconds = timeSeconds;
    context.transientBoundary = transientBoundary;
    return context;
}

} // namespace

int main()
{
    const auto options = makeOptions();
    ats::TimbreDescriptorSeriesOptions seriesOptions;
    seriesOptions.frameHopSeconds = 0.025;
    ats::TimbreSeriesAlignmentOptions alignment;

    // ---- Empty series ----
    {
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            {}, options, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty series: status must be EmptyInput");
        if (report.frameCount != 0)
            return fail("empty series: frame count must be 0");
    }

    // ---- Too-sparse series (single frame) ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame("source-a", 0, 0.0,
            { { 1000.0, 0.5 } }));
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            frames, options, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("single frame: status must be TooSparse");
    }

    // ---- Aligned 5-frame series ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 5; ++i)
        {
            frames.push_back(makeFrame("source-a", static_cast<std::uint64_t>(i),
                static_cast<double>(i) * 0.025,
                { { 1000.0, 0.5 + 0.1 * i } }));
        }
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            frames, options, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::Available)
            return fail("aligned: status must be Available");
        if (report.frameCount != 5)
            return fail("aligned: frame count must be 5");
        if (std::abs(report.durationSeconds - 0.1) > 1.0e-9)
            return fail("aligned: duration must be 0.1");
        if (series.frames.size() != 5u)
            return fail("aligned: output frames must mirror input count");
        if (series.sourceModelId != "source-a")
            return fail("aligned: sourceModelId must propagate");
        if (std::abs(series.frameHopSeconds - 0.025) > 1.0e-12)
            return fail("aligned: frameHopSeconds must propagate");
    }

    // ---- Non-monotonic time -> Undefined ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame("s", 0, 0.0, { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("s", 1, 0.025, { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("s", 2, 0.020, { { 1000.0, 0.5 } })); // back in time
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            frames, options, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::Undefined)
            return fail("non-monotonic: must be Undefined");
    }

    // ---- Hop drift outside tolerance -> Undefined ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame("s", 0, 0.0,    { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("s", 1, 0.025,  { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("s", 2, 0.050,  { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("s", 3, 0.080,  { { 1000.0, 0.5 } })); // hop 0.030 (20% drift)
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            frames, options, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::Undefined)
            return fail("hop drift: must be Undefined");
    }

    // ---- Source-id mismatch -> Undefined ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame("source-a", 0, 0.0, { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("source-a", 1, 0.025, { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("source-b", 2, 0.050, { { 1000.0, 0.5 } }));
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            frames, options, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::Undefined)
            return fail("source mismatch: must be Undefined");
        if (report.errorMessage.find("source-b") == std::string::npos)
            return fail("source mismatch: error message must mention the bad id");
    }

    // ---- Transient boundary count propagates ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 4; ++i)
        {
            frames.push_back(makeFrame("s", static_cast<std::uint64_t>(i),
                static_cast<double>(i) * 0.025,
                { { 1000.0, 0.5 } },
                /*transientBoundary=*/i == 2));
        }
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            frames, options, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::Available)
            return fail("transient: status must be Available");
        if (report.transientBoundaryCount != 1)
            return fail("transient: boundary count must be 1");
    }

    // ---- Inverted axis -> Undefined ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame("s", 0, 0.0, { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame("s", 1, 0.025, { { 1000.0, 0.5 } }));
        ats::TimbreDescriptorOptions bad = options;
        bad.comparisonAxis.minFrequencyHz = 9000.0;
        bad.comparisonAxis.maxFrequencyHz = 100.0;
        ats::TimbreDescriptorSeries series;
        const auto report = ats::analyzeTimbreDescriptorSeries(
            frames, bad, seriesOptions, alignment, series);
        if (report.status != ats::TimbreDescriptorStatus::Undefined)
            return fail("bad axis: must be Undefined");
    }

    // ---- buildSeriesGridStack: L1-normalized grids sum to 1 per frame ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 4; ++i)
        {
            const double amp = 0.5 + 0.1 * i;
            frames.push_back(makeFrame("s", static_cast<std::uint64_t>(i),
                static_cast<double>(i) * 0.025,
                { { 1000.0, amp }, { 3000.0, amp * 0.8 } }));
        }
        const auto stack = ats::buildSeriesGridStack(
            frames, options, alignment, ats::TimbreDescriptorDomain::Combined);
        if (stack.grids.size() != 4u)
            return fail("stack: must have 4 frames");
        if (stack.gridSize != options.comparisonGridSize)
            return fail("stack: gridSize must match options");
        for (std::size_t f = 0; f < stack.grids.size(); ++f)
        {
            double sum = 0.0;
            for (double bin : stack.grids[f])
                sum += bin;
            if (std::abs(sum - 1.0) > 1.0e-9)
                return fail("stack: L1-normalized frame sum must be 1");
        }
        // Raw energies must scale with amplitude.
        for (std::size_t i = 1; i < stack.frameEnergyBeforeNormalization.size(); ++i)
        {
            if (!(stack.frameEnergyBeforeNormalization[i]
                  > stack.frameEnergyBeforeNormalization[i - 1]))
            {
                return fail("stack: raw energy must increase with amplitude");
            }
        }
    }

    // ---- None policy preserves raw energy ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 3; ++i)
        {
            frames.push_back(makeFrame("s", static_cast<std::uint64_t>(i),
                static_cast<double>(i) * 0.025,
                { { 1000.0, 0.5 } }));
        }
        ats::TimbreSeriesAlignmentOptions noNorm = alignment;
        noNorm.frameNormalization = ats::SeriesFrameNormalization::None;
        const auto stack = ats::buildSeriesGridStack(
            frames, options, noNorm, ats::TimbreDescriptorDomain::Combined);
        for (std::size_t f = 0; f < stack.grids.size(); ++f)
        {
            double sum = 0.0;
            for (double bin : stack.grids[f])
                sum += bin;
            if (std::abs(sum - stack.frameEnergyBeforeNormalization[f]) > 1.0e-9)
                return fail("None policy: sum must equal raw energy");
        }
    }

    // ---- buildSeriesFrameContexts derives times from frame hop ----
    {
        ats::TimbreSeriesInput series;
        series.sourceModelId = "s";
        series.frameHopSeconds = 0.025;
        for (int i = 0; i < 3; ++i)
        {
            ats::EnvelopeFrameInput frame;
            frame.sourceModelId = "s";
            frame.frameIndex = static_cast<std::uint64_t>(i);
            frame.sampleRate = 48000.0;
            frame.partials.push_back({ 1000.0, 0.5, 20.0, 1.0, true });
            series.frames.push_back(std::move(frame));
        }
        const auto contexts = ats::buildSeriesFrameContexts(
            series, options, seriesOptions);
        if (contexts.size() != 3u)
            return fail("buildSeriesFrameContexts: count mismatch");
        for (std::size_t i = 0; i < contexts.size(); ++i)
        {
            const double expected = static_cast<double>(i) * 0.025;
            if (std::abs(contexts[i].timeSeconds - expected) > 1.0e-9)
                return fail("buildSeriesFrameContexts: time mismatch");
        }
    }

    std::cout << "timbre_series_alignment_test: ok\n";
    return 0;
}

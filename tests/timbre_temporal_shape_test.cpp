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
    std::cerr << "timbre_temporal_shape_test: " << message << '\n';
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

ats::SeriesFrameContext frameFromEnergy(int index, double time, double targetEnergy)
{
    // Single partial at 1000 Hz with amplitude sqrt(target).
    const double amp = std::sqrt(std::max(targetEnergy, 0.0));
    ats::EnvelopeFrameInput raw;
    raw.sourceModelId = "s";
    raw.frameIndex = static_cast<std::uint64_t>(index);
    raw.sampleRate = 48000.0;
    if (targetEnergy > 0.0)
        raw.partials.push_back({ 1000.0, amp, 20.0, 1.0, true });
    ats::SeriesFrameContext context;
    context.samples = ats::validateEnvelopeFrameInput(raw);
    context.timeSeconds = time;
    return context;
}

double dbToEnergy(double dbBelowPeak, double peak)
{
    return peak * std::pow(10.0, dbBelowPeak / 10.0);
}

} // namespace

int main()
{
    const auto options = makeOptions();
    ats::TemporalEnvelopeOptions temporalOptions;
    // Use the spec defaults: attack from -40 to -3 dB below peak, effective
    // duration above -40 dB.

    // ---- Fast attack: rises from -40 to -3 dB in ~20 ms ----
    {
        const double hop = 0.005;
        const double peakEnergy = 1.0;
        std::vector<ats::SeriesFrameContext> frames;
        const double e_minus40 = dbToEnergy(-40.0, peakEnergy);
        const double e_minus3 = dbToEnergy(-3.0, peakEnergy);
        frames.push_back(frameFromEnergy(0, 0.000, e_minus40));     // attack start
        frames.push_back(frameFromEnergy(1, 0.005,
            std::sqrt(e_minus40 * e_minus3)));                       // mid
        frames.push_back(frameFromEnergy(2, 0.010,
            std::sqrt(e_minus40 * e_minus3)));                       // mid
        frames.push_back(frameFromEnergy(3, 0.015,
            std::sqrt(e_minus40 * e_minus3)));                       // mid
        frames.push_back(frameFromEnergy(4, 0.020, e_minus3));      // attack end
        frames.push_back(frameFromEnergy(5, 0.025, peakEnergy));    // peak
        for (int i = 6; i < 12; ++i)
            frames.push_back(frameFromEnergy(i, i * hop, peakEnergy));
        // decay
        for (int i = 12; i < 18; ++i)
        {
            const double t = static_cast<double>(i - 11) / 6.0;
            const double dbDrop = -40.0 * t;
            frames.push_back(frameFromEnergy(i, i * hop, dbToEnergy(dbDrop, peakEnergy)));
        }

        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzeTemporalEnvelopeShape(
            frames, options, temporalOptions, series);

        if (series.logAttackTime.status != ats::TimbreDescriptorStatus::Available)
            return fail("fast attack: logAttackTime must be Available");
        // attack duration was 0.020 -> log10(0.020) ~= -1.70
        if (std::abs(series.logAttackTime.value - std::log10(0.020)) > 0.05)
            return fail("fast attack: logAttackTime mismatch");
        if (series.attackSlopeDbPerSecond.status != ats::TimbreDescriptorStatus::Available)
            return fail("fast attack: attackSlope must be Available");
        // attack slope = (-3 - -40) / 0.020 = 1850 dB/s
        if (std::abs(series.attackSlopeDbPerSecond.value - 1850.0) > 50.0)
            return fail("fast attack: attackSlope mismatch");
        if (series.effectiveDurationSeconds.status != ats::TimbreDescriptorStatus::Available)
            return fail("fast attack: effectiveDuration must be Available");
        if (!(series.effectiveDurationSeconds.value > 0.05))
            return fail("fast attack: effectiveDuration too small");
        if (!(series.temporalCentroidSeconds.value > 0.0
              && series.temporalCentroidSeconds.value < frames.back().timeSeconds))
        {
            return fail("fast attack: temporalCentroid out of range");
        }
        if (diag.peakEnergy <= 0.0)
            return fail("fast attack: diagnostic peakEnergy must be > 0");
        if (diag.independentPeakCount != 1)
            return fail("fast attack: independentPeakCount must be 1");
    }

    // ---- Slow attack: attack window of 100 ms with -3 dB crossing late ----
    {
        const double hop = 0.010;
        const double peak = 1.0;
        std::vector<ats::SeriesFrameContext> frames;
        // Linear-in-dB rise from -40 to -3 over 100 ms.
        for (int i = 0; i < 11; ++i)
        {
            const double dbBelow = -40.0 + 37.0 * (i / 10.0);
            frames.push_back(frameFromEnergy(i, i * hop, dbToEnergy(dbBelow, peak)));
        }
        // Hold at peak briefly.
        for (int i = 11; i < 16; ++i)
            frames.push_back(frameFromEnergy(i, i * hop, peak));
        // Decay.
        for (int i = 16; i < 25; ++i)
        {
            const double dbDrop = -40.0 * ((i - 15) / 9.0);
            frames.push_back(frameFromEnergy(i, i * hop, dbToEnergy(dbDrop, peak)));
        }

        ats::TimbreDescriptorSeries series;
        ats::analyzeTemporalEnvelopeShape(frames, options, temporalOptions, series);
        if (std::abs(series.logAttackTime.value - std::log10(0.100)) > 0.1)
            return fail("slow attack: logAttackTime should be near log10(0.1)");
        // attack slope = 37 / 0.1 = 370 dB/s
        if (std::abs(series.attackSlopeDbPerSecond.value - 370.0) > 30.0)
            return fail("slow attack: attackSlope mismatch");
        if (!(series.attackSlopeDbPerSecond.value > 0.0))
            return fail("slow attack: attackSlope must be positive");
    }

    // ---- Short decay: peak then immediate drop ----
    {
        const double hop = 0.005;
        const double peak = 1.0;
        std::vector<ats::SeriesFrameContext> frames;
        // Quick attack.
        frames.push_back(frameFromEnergy(0, 0.000, dbToEnergy(-40.0, peak)));
        frames.push_back(frameFromEnergy(1, hop, dbToEnergy(-20.0, peak)));
        frames.push_back(frameFromEnergy(2, 2 * hop, dbToEnergy(-3.0, peak)));
        frames.push_back(frameFromEnergy(3, 3 * hop, peak));
        // Short decay: drop -40 dB in 20 ms.
        for (int i = 4; i < 8; ++i)
        {
            const double t = (i - 3) / 4.0;
            const double dbDrop = -40.0 * t;
            frames.push_back(frameFromEnergy(i, i * hop, dbToEnergy(dbDrop, peak)));
        }

        ats::TimbreDescriptorSeries series;
        ats::analyzeTemporalEnvelopeShape(frames, options, temporalOptions, series);
        if (series.decaySlopeDbPerSecond.status != ats::TimbreDescriptorStatus::Available)
            return fail("short decay: decaySlope must be Available");
        if (!(series.decaySlopeDbPerSecond.value < -500.0))
            return fail("short decay: decaySlope must be steeply negative");
    }

    // ---- Long decay ----
    {
        const double hop = 0.020;
        const double peak = 1.0;
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(frameFromEnergy(0, 0.000, dbToEnergy(-40.0, peak)));
        frames.push_back(frameFromEnergy(1, hop, dbToEnergy(-3.0, peak)));
        frames.push_back(frameFromEnergy(2, 2 * hop, peak));
        // Long decay -40 dB over 200 ms.
        for (int i = 3; i < 13; ++i)
        {
            const double t = (i - 2) / 10.0;
            const double dbDrop = -40.0 * t;
            frames.push_back(frameFromEnergy(i, i * hop, dbToEnergy(dbDrop, peak)));
        }

        ats::TimbreDescriptorSeries series;
        ats::analyzeTemporalEnvelopeShape(frames, options, temporalOptions, series);
        // decay slope ~ -200 dB/s
        if (!(series.decaySlopeDbPerSecond.value > -300.0
              && series.decaySlopeDbPerSecond.value < -100.0))
        {
            return fail("long decay: decaySlope out of expected range");
        }
    }

    // ---- Silence: EmptyInput on temporalCentroid; the others go TooSparse
    //      because attack duration collapses to 0. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 6; ++i)
            frames.push_back(frameFromEnergy(i, i * 0.01, 0.0));
        ats::TimbreDescriptorSeries series;
        ats::analyzeTemporalEnvelopeShape(frames, options, temporalOptions, series);
        if (series.temporalCentroidSeconds.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("silence: temporalCentroid must be EmptyInput");
        if (series.logAttackTime.status == ats::TimbreDescriptorStatus::Available)
            return fail("silence: logAttackTime must not be Available");
        if (!std::isfinite(series.attackSlopeDbPerSecond.value)
            || !std::isfinite(series.decaySlopeDbPerSecond.value))
        {
            return fail("silence: descriptors must remain finite");
        }
    }

    // ---- Multi-event rejection: two distinct peaks above the effective
    //      duration threshold -> Undefined. ----
    {
        const double hop = 0.010;
        std::vector<ats::SeriesFrameContext> frames;
        // Event 1.
        frames.push_back(frameFromEnergy(0, 0 * hop, dbToEnergy(-40.0, 1.0)));
        frames.push_back(frameFromEnergy(1, 1 * hop, dbToEnergy(-3.0, 1.0)));
        frames.push_back(frameFromEnergy(2, 2 * hop, 1.0));
        frames.push_back(frameFromEnergy(3, 3 * hop, dbToEnergy(-3.0, 1.0)));
        // Silence gap.
        frames.push_back(frameFromEnergy(4, 4 * hop, dbToEnergy(-60.0, 1.0)));
        frames.push_back(frameFromEnergy(5, 5 * hop, dbToEnergy(-60.0, 1.0)));
        // Event 2.
        frames.push_back(frameFromEnergy(6, 6 * hop, dbToEnergy(-3.0, 1.0)));
        frames.push_back(frameFromEnergy(7, 7 * hop, 1.0));
        frames.push_back(frameFromEnergy(8, 8 * hop, dbToEnergy(-40.0, 1.0)));

        ats::TimbreDescriptorSeries series;
        const auto diag = ats::analyzeTemporalEnvelopeShape(
            frames, options, temporalOptions, series);
        if (diag.independentPeakCount < 2)
            return fail("multi-event: must detect >= 2 independent peaks");
        if (series.temporalCentroidSeconds.status != ats::TimbreDescriptorStatus::Undefined)
            return fail("multi-event: temporalCentroid must be Undefined");
        if (series.logAttackTime.status != ats::TimbreDescriptorStatus::Undefined)
            return fail("multi-event: logAttackTime must be Undefined");
    }

    // ---- Too sparse: < minimumFrameCount frames ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(frameFromEnergy(0, 0.0, 0.5));
        frames.push_back(frameFromEnergy(1, 0.01, 0.7));
        ats::TimbreDescriptorSeries series;
        ats::analyzeTemporalEnvelopeShape(frames, options, temporalOptions, series);
        if (series.logAttackTime.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("too sparse: logAttackTime must be TooSparse");
    }

    std::cout << "timbre_temporal_shape_test: ok\n";
    return 0;
}

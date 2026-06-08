#include "TimbreDescriptors.h"
#include "TimbreFrameAdapter.h"
#include "TimbreFrameAnalysis.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_centroid_moments_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorOptions options()
{
    ats::TimbreDescriptorOptions opts;
    opts.comparisonAxis.type = ats::EnvelopeFrequencyAxis::Bark;
    opts.comparisonAxis.minFrequencyHz = 20.0;
    opts.comparisonAxis.maxFrequencyHz = 16000.0;
    opts.comparisonAxis.sampleRate = 48000.0;
    opts.comparisonGridSize = 32;
    return opts;
}

ats::TimbreDescriptorFrame analyze(const ats::EnvelopeFrameInput& frame)
{
    const auto samples = ats::validateEnvelopeFrameInput(frame);
    ats::TimbreDescriptorFrame descriptors;
    ats::analyzeCentroidMomentsDescriptors(samples, options(), descriptors);
    return descriptors;
}

} // namespace

int main()
{
    // ---- Single partial: centroid equals partial frequency; spread sparse ----
    {
        ats::EnvelopeFrameInput frame;
        frame.partials = { { 1000.0, 1.0, 20.0, 1.0, true } };
        const auto d = analyze(frame);
        if (d.partialCentroidHz.status != ats::TimbreDescriptorStatus::Available)
            return fail("single partial: partialCentroidHz must be Available");
        if (std::abs(d.partialCentroidHz.value - 1000.0) > 1.0e-9)
            return fail("single partial: centroid must equal 1000");
        if (d.combinedSpreadHz.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("single partial: spread must be TooSparse");
        if (d.combinedSkewness.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("single partial: skewness must be TooSparse");
        if (d.combinedKurtosis.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("single partial: kurtosis must be TooSparse");
        if (d.rolloff85Hz.status != ats::TimbreDescriptorStatus::Available)
            return fail("single partial: rolloff_85 must be Available");
        if (std::abs(d.rolloff85Hz.value - 1000.0) > 1.0e-9)
            return fail("single partial: rolloff_85 must equal 1000");
    }

    // ---- Two equal-energy partials: centroid = midpoint; spread = half-distance ----
    {
        ats::EnvelopeFrameInput frame;
        frame.partials = {
            { 1000.0, 1.0, 20.0, 1.0, true },
            { 3000.0, 1.0, 20.0, 1.0, true },
        };
        const auto d = analyze(frame);
        if (std::abs(d.combinedCentroidHz.value - 2000.0) > 1.0e-9)
            return fail("two partials: centroid must be 2000");
        if (std::abs(d.combinedSpreadHz.value - 1000.0) > 1.0e-9)
            return fail("two partials: spread must be 1000");
        if (std::abs(d.combinedSkewness.value) > 1.0e-9)
            return fail("two partials: skewness must be 0");
        // Symmetric two-point distribution: kurtosis = 1.
        if (std::abs(d.combinedKurtosis.value - 1.0) > 1.0e-9)
            return fail("two partials: kurtosis must be 1");
        if (std::abs(d.rolloff95Hz.value - 3000.0) > 1.0e-9)
            return fail("two partials: rolloff_95 must be 3000");
    }

    // ---- Flat grid (residual bands only): centroid = arithmetic mean of band freqs ----
    {
        ats::EnvelopeFrameInput frame;
        frame.residualBands = {
            {  500.0, 1.0, 1.0, false },
            { 1500.0, 1.0, 1.0, false },
            { 3500.0, 1.0, 1.0, false },
            { 5500.0, 1.0, 1.0, false },
        };
        const auto d = analyze(frame);
        if (d.residualCentroidHz.status != ats::TimbreDescriptorStatus::Available)
            return fail("flat bands: residualCentroidHz must be Available");
        const double expected = (500.0 + 1500.0 + 3500.0 + 5500.0) / 4.0;
        if (std::abs(d.residualCentroidHz.value - expected) > 1.0e-9)
            return fail("flat bands: residual centroid mismatch");
        if (d.partialCentroidHz.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("flat bands: partialCentroidHz must be EmptyInput");
    }

    // ---- Combined distribution: centroid uses partials + residual energy ----
    {
        ats::EnvelopeFrameInput frame;
        frame.partials = {
            { 1000.0, std::sqrt(0.3), 20.0, 1.0, true },
            { 3000.0, std::sqrt(0.3), 20.0, 1.0, true },
        };
        frame.residualBands = {
            { 5000.0, 0.4, 1.0, false },
        };
        // Partial energies: 0.3 each at 1000 and 3000 -> contribution (1000*0.3 + 3000*0.3)
        // Residual: 0.4 at 5000 -> contribution 2000.
        // Total energy = 1.0; centroid = (300 + 900 + 2000) / 1.0 = 3200
        const auto d = analyze(frame);
        if (std::abs(d.combinedCentroidHz.value - 3200.0) > 1.0e-9)
            return fail("combined distribution: centroid mismatch");
    }

    // ---- Empty domain: explicit EmptyInput status, finite values ----
    {
        ats::EnvelopeFrameInput frame;
        const auto d = analyze(frame);
        if (d.partialCentroidHz.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty: partialCentroidHz must be EmptyInput");
        if (d.residualCentroidHz.status != ats::TimbreDescriptorStatus::MissingResidual)
            return fail("empty: residualCentroidHz must be MissingResidual");
        if (d.combinedCentroidHz.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty: combinedCentroidHz must be EmptyInput");
        if (!std::isfinite(d.combinedCentroidHz.value)
            || !std::isfinite(d.combinedSpreadHz.value)
            || !std::isfinite(d.combinedSkewness.value)
            || !std::isfinite(d.combinedKurtosis.value)
            || !std::isfinite(d.rolloff85Hz.value)
            || !std::isfinite(d.rolloff95Hz.value))
        {
            return fail("empty: descriptor values must be finite");
        }
    }

    // ---- Gain scaling: doubling every amplitude must leave normalized moments
    //      unchanged (centroid/spread are scale-free; skewness/kurtosis are
    //      normalized; rolloff is a frequency cutoff so unchanged) ----
    {
        ats::EnvelopeFrameInput a;
        a.partials = {
            { 1000.0, 0.5, 20.0, 1.0, true },
            { 2500.0, 0.6, 20.0, 1.0, true },
            { 4000.0, 0.4, 20.0, 1.0, true },
        };
        a.residualBands = { { 750.0, 0.1, 1.0, false } };
        ats::EnvelopeFrameInput b = a;
        const double k = 4.2;
        for (auto& p : b.partials)
            p.amplitude *= k;
        for (auto& bandPoint : b.residualBands)
            bandPoint.energy *= (k * k);

        const auto da = analyze(a);
        const auto db = analyze(b);

        if (std::abs(da.combinedCentroidHz.value - db.combinedCentroidHz.value) > 1.0e-9)
            return fail("gain invariance: combined centroid changed");
        if (std::abs(da.combinedSpreadHz.value - db.combinedSpreadHz.value) > 1.0e-9)
            return fail("gain invariance: spread changed");
        if (std::abs(da.combinedSkewness.value - db.combinedSkewness.value) > 1.0e-9)
            return fail("gain invariance: skewness changed");
        if (std::abs(da.combinedKurtosis.value - db.combinedKurtosis.value) > 1.0e-9)
            return fail("gain invariance: kurtosis changed");
        if (std::abs(da.rolloff85Hz.value - db.rolloff85Hz.value) > 1.0e-9)
            return fail("gain invariance: rolloff_85 changed");
        if (std::abs(da.rolloff95Hz.value - db.rolloff95Hz.value) > 1.0e-9)
            return fail("gain invariance: rolloff_95 changed");
    }

    // ---- Axis centroid is finite and on [0, 1] for non-empty input ----
    {
        ats::EnvelopeFrameInput frame;
        frame.partials = {
            { 1000.0, 1.0, 20.0, 1.0, true },
            { 4000.0, 1.0, 20.0, 1.0, true },
        };
        const auto d = analyze(frame);
        if (d.combinedCentroidAxis.status != ats::TimbreDescriptorStatus::Available)
            return fail("axis centroid status must be Available");
        if (d.combinedCentroidAxis.value < 0.0 || d.combinedCentroidAxis.value > 1.0)
            return fail("axis centroid must be in [0, 1]");
    }

    std::cout << "timbre_centroid_moments_test: ok\n";
    return 0;
}

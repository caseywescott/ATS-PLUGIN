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
    std::cerr << "timbre_grid_shape_test: " << message << '\n';
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

ats::TimbreDescriptorOptions linearOptions(int gridSize)
{
    ats::TimbreDescriptorOptions opts;
    opts.comparisonAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    opts.comparisonAxis.minFrequencyHz = 100.0;
    opts.comparisonAxis.maxFrequencyHz = 8100.0;
    opts.comparisonAxis.sampleRate = 48000.0;
    opts.comparisonGridSize = gridSize;
    return opts;
}

ats::TimbreDescriptorFrame analyze(const ats::EnvelopeFrameInput& frame)
{
    const auto samples = ats::validateEnvelopeFrameInput(frame);
    ats::TimbreDescriptorFrame descriptors;
    ats::analyzeGridShapeDescriptors(samples, options(), {}, descriptors);
    return descriptors;
}

} // namespace

int main()
{
    // ---- Flat grid: place one band at each bin center on a Linear axis so
    //      the projected grid is genuinely uniform; flatness/entropy ~ 1,
    //      crest ~ 1.
    {
        const int gridSize = 16;
        const auto linearOpts = linearOptions(gridSize);
        const double lo = linearOpts.comparisonAxis.minFrequencyHz;
        const double hi = linearOpts.comparisonAxis.maxFrequencyHz;
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < gridSize; ++i)
        {
            const double position = (static_cast<double>(i) + 0.5)
                                     / static_cast<double>(gridSize);
            const double frequency = lo + position * (hi - lo);
            frame.residualBands.push_back({ frequency, 1.0, 1.0, false });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeGridShapeDescriptors(samples, linearOpts, {}, d);
        if (d.flatness.status != ats::TimbreDescriptorStatus::Available)
            return fail("flat grid: flatness must be Available");
        if (d.flatness.value < 0.95)
            return fail("flat grid: flatness too low");
        if (d.crest.value > 1.1)
            return fail("flat grid: crest too large");
        if (d.entropyNormalized.value < 0.95)
            return fail("flat grid: entropy too low");
    }

    // ---- Single dominant band on a Linear-axis grid; band placed at one
    //      bin centre so the projection puts the entire energy into one bin
    //      and the dominance metrics resolve cleanly. ----
    {
        const int gridSize = 16;
        const auto linearOpts = linearOptions(gridSize);
        const double lo = linearOpts.comparisonAxis.minFrequencyHz;
        const double hi = linearOpts.comparisonAxis.maxFrequencyHz;
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < gridSize; ++i)
        {
            const double position = (static_cast<double>(i) + 0.5)
                                     / static_cast<double>(gridSize);
            const double frequency = lo + position * (hi - lo);
            const double energy = (i == 8) ? 1.0 : 1.0e-9;
            frame.residualBands.push_back({ frequency, energy, 1.0, false });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeGridShapeDescriptors(samples, linearOpts, {}, d);
        if (d.flatness.value > 0.05)
            return fail("dominant band: flatness too large");
        if (d.crest.value < 10.0)
            return fail("dominant band: crest too small");
        if (d.strongPeakRatio.value < 0.95)
            return fail("dominant band: strong_peak_ratio too small");
        if (d.spectralComplexity.value > 2.0)
            return fail("dominant band: complexity must report ~1 bin");
    }

    // ---- Sloped grid: amplitude ~ 1/f (energy ~ 1/f^2) so the projected
    //      grid must have decreasing energy and spectral_decrease < 0.
    {
        const int gridSize = 16;
        const auto linearOpts = linearOptions(gridSize);
        const double lo = linearOpts.comparisonAxis.minFrequencyHz;
        const double hi = linearOpts.comparisonAxis.maxFrequencyHz;
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < gridSize; ++i)
        {
            const double position = (static_cast<double>(i) + 0.5)
                                     / static_cast<double>(gridSize);
            const double frequency = lo + position * (hi - lo);
            const double amplitude = 1000.0 / std::max(frequency, 1.0);
            frame.partials.push_back({ frequency, amplitude, 20.0, 1.0, true });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeGridShapeDescriptors(samples, linearOpts, {}, d);
        if (d.spectralDecrease.status != ats::TimbreDescriptorStatus::Available)
            return fail("sloped grid: spectralDecrease must be Available");
        if (d.spectralDecrease.value >= 0.0)
            return fail("sloped grid: spectralDecrease must be negative");
    }

    // ---- Empty grid: EmptyInput status, finite zero values ----
    {
        ats::EnvelopeFrameInput frame;
        const auto d = analyze(frame);
        if (d.flatness.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty: flatness must be EmptyInput");
        if (!std::isfinite(d.flatness.value)
            || !std::isfinite(d.crest.value)
            || !std::isfinite(d.entropyNormalized.value)
            || !std::isfinite(d.spectralDecrease.value)
            || !std::isfinite(d.strongPeakRatio.value)
            || !std::isfinite(d.spectralComplexity.value))
        {
            return fail("empty: finite zero values required");
        }
        if (!d.spectrumFlatnessVector.values.empty())
            return fail("empty: spectrumFlatnessVector must be empty");
    }

    // ---- Gain invariance: scaling every amplitude by k cannot change
    //      flatness, crest, entropy, decrease, or strong_peak_ratio ----
    {
        ats::EnvelopeFrameInput a;
        a.partials = {
            { 1000.0, 0.3, 20.0, 1.0, true },
            { 2500.0, 0.7, 20.0, 1.0, true },
            { 4000.0, 0.2, 20.0, 1.0, true },
        };
        a.residualBands = {
            { 750.0, 0.1, 1.0, false },
            { 5500.0, 0.05, 1.0, false },
        };
        ats::EnvelopeFrameInput b = a;
        const double k = 1.7;
        for (auto& p : b.partials)
            p.amplitude *= k;
        for (auto& bandPoint : b.residualBands)
            bandPoint.energy *= (k * k);

        const auto da = analyze(a);
        const auto db = analyze(b);
        if (std::abs(da.flatness.value - db.flatness.value) > 1.0e-9)
            return fail("gain invariance: flatness changed");
        if (std::abs(da.crest.value - db.crest.value) > 1.0e-9)
            return fail("gain invariance: crest changed");
        if (std::abs(da.entropyNormalized.value - db.entropyNormalized.value) > 1.0e-9)
            return fail("gain invariance: entropy changed");
        if (std::abs(da.strongPeakRatio.value - db.strongPeakRatio.value) > 1.0e-9)
            return fail("gain invariance: strong_peak_ratio changed");
        if (std::abs(da.spectralDecrease.value - db.spectralDecrease.value) > 1.0e-9)
            return fail("gain invariance: spectral_decrease changed");
        if (da.spectrumFlatnessVector.values.size()
            != db.spectrumFlatnessVector.values.size())
        {
            return fail("gain invariance: flatness vector length changed");
        }
        for (std::size_t i = 0; i < da.spectrumFlatnessVector.values.size(); ++i)
        {
            const double va = da.spectrumFlatnessVector.values[i];
            const double vb = db.spectrumFlatnessVector.values[i];
            if (std::abs(va - vb) > 1.0e-9)
                return fail("gain invariance: flatness vector bin changed");
        }
    }

    // ---- Threshold boundary: bumping complexityThresholdRatio drops
    //      borderline bins ----
    {
        ats::EnvelopeFrameInput frame;
        // Six clear peaks above 0.5 of max, eight smaller ones below.
        for (int i = 0; i < 14; ++i)
        {
            const double frequency = 200.0 + i * 500.0;
            const double energy = (i % 2 == 0) ? 1.0 : 0.05;
            frame.residualBands.push_back({ frequency, energy, 1.0, false });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame strict;
        ats::GridShapeOptions strictOptions;
        strictOptions.complexityThresholdRatio = 0.5;
        ats::analyzeGridShapeDescriptors(samples, options(), strictOptions, strict);

        ats::TimbreDescriptorFrame loose;
        ats::GridShapeOptions looseOptions;
        looseOptions.complexityThresholdRatio = 0.01;
        ats::analyzeGridShapeDescriptors(samples, options(), looseOptions, loose);

        if (strict.spectralComplexity.value >= loose.spectralComplexity.value)
            return fail("threshold boundary: stricter threshold must drop bins");
    }

    // ---- Spectrum flatness vector length matches request ----
    {
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < 16; ++i)
        {
            frame.partials.push_back(
                { 100.0 + i * 600.0, 0.5, 20.0, 1.0, true });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::GridShapeOptions vectorOpts;
        vectorOpts.flatnessVectorBands = 4;
        ats::TimbreDescriptorFrame descriptors;
        ats::analyzeGridShapeDescriptors(samples, options(), vectorOpts, descriptors);
        if (descriptors.spectrumFlatnessVector.values.size() != 4u)
            return fail("flatness vector: wrong length");
        if (descriptors.spectrumFlatnessVector.status
            != ats::TimbreDescriptorStatus::Available)
        {
            return fail("flatness vector: must be Available");
        }
    }

    std::cout << "timbre_grid_shape_test: ok\n";
    return 0;
}

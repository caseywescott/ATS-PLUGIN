#include "TimbreDescriptors.h"
#include "TimbreFrameAdapter.h"
#include "TimbreFrameAnalysis.h"
#include "TimbreEnergyDistribution.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_energy_noisiness_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorOptions defaultOptions()
{
    ats::TimbreDescriptorOptions options;
    options.comparisonAxis.type = ats::EnvelopeFrequencyAxis::Bark;
    options.comparisonAxis.minFrequencyHz = 20.0;
    options.comparisonAxis.maxFrequencyHz = 16000.0;
    options.comparisonAxis.sampleRate = 48000.0;
    options.comparisonGridSize = 32;
    return options;
}

ats::EnvelopeFrameInput makePartialResidualSplit()
{
    // partial_residual_split_75_25 from the analytical fixtures.
    ats::EnvelopeFrameInput frame;
    frame.sourceModelId = "fixture-split";
    frame.frameIndex = 11;
    frame.sampleRate = 48000.0;
    frame.partials = {
        {  500.0, 0.5, 20.0, 1.0, true },
        { 1500.0, 0.5, 20.0, 1.0, true },
        { 2500.0, 0.5, 20.0, 1.0, true },
    };
    frame.residualBands = {
        {  250.0, 0.125, 1.0, false },
        { 4500.0, 0.125, 1.0, false },
    };
    return frame;
}

ats::EnvelopeFrameInput makeOrphanFrame()
{
    ats::EnvelopeFrameInput frame;
    frame.sourceModelId = "fixture-orphan";
    frame.frameIndex = 3;
    frame.sampleRate = 48000.0;
    frame.partials = {
        {  500.0, std::sqrt(0.3), 20.0, 1.0, true },
        { 1500.0, std::sqrt(0.3), 20.0, 1.0, true },
    };
    frame.residualBands = {
        { 3500.0, 0.2, 1.0, false },
        { 5500.0, 0.2, 1.0, true  },
    };
    return frame;
}

} // namespace

int main()
{
    const auto options = defaultOptions();

    // ---- Empty frame: every descriptor is finite, no residual = MissingResidual ----
    {
        ats::EnvelopeFrameInput frame;
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame descriptors;
        const auto hnr = ats::analyzeEnergyAndNoisinessDescriptors(
            samples, options, descriptors);

        if (descriptors.partialEnergy.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty frame partial energy must be EmptyInput");
        if (descriptors.residualEnergy.status != ats::TimbreDescriptorStatus::MissingResidual)
            return fail("empty frame residual energy must be MissingResidual");
        if (descriptors.noisiness.status != ats::TimbreDescriptorStatus::MissingResidual)
            return fail("empty frame noisiness must be MissingResidual");
        if (!std::isfinite(descriptors.partialEnergy.value)
            || !std::isfinite(descriptors.residualEnergy.value)
            || !std::isfinite(descriptors.noisiness.value)
            || !std::isfinite(descriptors.combinedEnergy.value))
        {
            return fail("empty frame produced non-finite descriptor values");
        }
        if (hnr.harmonicToNoiseStatus != ats::TimbreDescriptorStatus::MissingResidual)
            return fail("empty frame HNR must be MissingResidual");
    }

    // ---- Exact split: 0.75 partial, 0.25 residual, noisiness = 0.25 ----
    {
        const auto frame = makePartialResidualSplit();
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame descriptors;
        const auto hnr = ats::analyzeEnergyAndNoisinessDescriptors(
            samples, options, descriptors);

        if (descriptors.partialEnergy.status != ats::TimbreDescriptorStatus::Available)
            return fail("split: partialEnergy must be Available");
        if (std::abs(descriptors.partialEnergy.value - 0.75) > 1.0e-12)
            return fail("split: partialEnergy must equal 0.75");
        if (descriptors.residualEnergy.status != ats::TimbreDescriptorStatus::Available)
            return fail("split: residualEnergy must be Available");
        if (std::abs(descriptors.residualEnergy.value - 0.25) > 1.0e-12)
            return fail("split: residualEnergy must equal 0.25");
        if (std::abs(descriptors.combinedEnergy.value - 1.0) > 1.0e-12)
            return fail("split: combinedEnergy must equal 1.0");
        if (std::abs(descriptors.noisiness.value - 0.25) > 1.0e-12)
            return fail("split: noisiness must equal 0.25");
        if (descriptors.orphanResidualRatio.status != ats::TimbreDescriptorStatus::Available)
            return fail("split: orphan ratio must be Available");
        if (std::abs(descriptors.orphanResidualRatio.value) > 1.0e-12)
            return fail("split: orphan ratio must equal 0");
        if (descriptors.bandwiseNoisiness.status != ats::TimbreDescriptorStatus::Available)
            return fail("split: bandwise noisiness must be Available");
        if (descriptors.bandwiseNoisiness.values.size() != 32u)
            return fail("split: bandwise noisiness vector must have 32 bins");

        // HNR = 10 log10(0.75 / 0.25) = 10 log10(3)
        const double expectedHnr = 10.0 * std::log10(3.0);
        if (hnr.harmonicToNoiseStatus != ats::TimbreDescriptorStatus::Available)
            return fail("split: HNR must be Available");
        if (std::abs(hnr.harmonicToNoiseRatioDb - expectedHnr) > 1.0e-9)
            return fail("split: HNR value mismatch");
    }

    // ---- Orphan flagged half the residual -> ratio = 0.5 ----
    {
        const auto frame = makeOrphanFrame();
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame descriptors;
        ats::analyzeEnergyAndNoisinessDescriptors(samples, options, descriptors);
        if (descriptors.orphanResidualRatio.status != ats::TimbreDescriptorStatus::Available)
            return fail("orphan: status must be Available");
        if (std::abs(descriptors.orphanResidualRatio.value - 0.5) > 1.0e-12)
            return fail("orphan: ratio must equal 0.5");
    }

    // ---- Global gain invariance: scaling partial amplitudes by k must NOT
    //      change normalized noisiness if residual energy also scales by k^2 ----
    {
        const double k = 3.7;
        ats::EnvelopeFrameInput frame;
        frame.sampleRate = 48000.0;
        frame.partials = {
            { 500.0, 0.4, 20.0, 1.0, true },
            { 2000.0, 0.6, 20.0, 1.0, true },
        };
        frame.residualBands = {
            { 250.0, 0.1, 1.0, false },
            { 4500.0, 0.2, 1.0, false },
        };

        ats::EnvelopeFrameInput scaled = frame;
        for (auto& p : scaled.partials)
            p.amplitude *= k;
        for (auto& b : scaled.residualBands)
            b.energy *= (k * k);

        const auto samplesA = ats::validateEnvelopeFrameInput(frame);
        const auto samplesB = ats::validateEnvelopeFrameInput(scaled);
        ats::TimbreDescriptorFrame descriptorsA;
        ats::TimbreDescriptorFrame descriptorsB;
        const auto hnrA = ats::analyzeEnergyAndNoisinessDescriptors(
            samplesA, options, descriptorsA);
        const auto hnrB = ats::analyzeEnergyAndNoisinessDescriptors(
            samplesB, options, descriptorsB);

        if (std::abs(descriptorsA.noisiness.value - descriptorsB.noisiness.value)
            > 1.0e-9)
        {
            return fail("noisiness must be invariant to uniform energy gain");
        }
        if (std::abs(descriptorsA.orphanResidualRatio.value
                     - descriptorsB.orphanResidualRatio.value) > 1.0e-9)
        {
            return fail("orphan ratio must be invariant to uniform gain");
        }
        if (std::abs(hnrA.harmonicToNoiseRatioDb - hnrB.harmonicToNoiseRatioDb)
            > 1.0e-9)
        {
            return fail("HNR must be invariant to uniform gain");
        }
        // Energies themselves scale.
        const double expectedScale = k * k;
        if (std::abs(descriptorsB.partialEnergy.value
                     - expectedScale * descriptorsA.partialEnergy.value) > 1.0e-9)
        {
            return fail("scaled partialEnergy must be k^2 * unscaled");
        }
        if (std::abs(descriptorsB.combinedEnergy.value
                     - expectedScale * descriptorsA.combinedEnergy.value) > 1.0e-9)
        {
            return fail("scaled combinedEnergy must be k^2 * unscaled");
        }
        // Bandwise noisiness vector should be elementwise unchanged.
        if (descriptorsA.bandwiseNoisiness.values.size()
            != descriptorsB.bandwiseNoisiness.values.size())
        {
            return fail("bandwise noisiness length differs after scaling");
        }
        for (std::size_t i = 0; i < descriptorsA.bandwiseNoisiness.values.size(); ++i)
        {
            const double a = descriptorsA.bandwiseNoisiness.values[i];
            const double b = descriptorsB.bandwiseNoisiness.values[i];
            if (std::abs(a - b) > 1.0e-9)
                return fail("bandwise noisiness bin not gain-invariant");
        }
    }

    // ---- Confidence aggregation: low-confidence partials drag down per-frame
    //      partial energy confidence per the energy-weighted rule ----
    {
        ats::EnvelopeFrameInput frame;
        frame.sampleRate = 48000.0;
        frame.partials = {
            { 1000.0, 1.0, 20.0, 0.4, true },  // energy 1.0, conf 0.4
            { 2000.0, 1.0, 20.0, 1.0, true },  // energy 1.0, conf 1.0
        };
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame descriptors;
        ats::analyzeEnergyAndNoisinessDescriptors(samples, options, descriptors);
        // energy-weighted mean = (1.0 * 0.4 + 1.0 * 1.0) / 2.0 = 0.7
        if (std::abs(descriptors.partialEnergy.confidence - 0.7) > 1.0e-9)
            return fail("partial energy confidence must be energy-weighted mean");
    }

    // ---- All finite outputs ----
    {
        ats::EnvelopeFrameInput frame;
        frame.partials = {
            { 1000.0, 1.0, 20.0, 1.0, true },
        };
        frame.residualBands = {
            { 500.0, 1.0, 1.0, false },
        };
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        const auto hnr = ats::analyzeEnergyAndNoisinessDescriptors(samples, options, d);
        if (!std::isfinite(d.partialEnergy.value)
            || !std::isfinite(d.residualEnergy.value)
            || !std::isfinite(d.combinedEnergy.value)
            || !std::isfinite(d.noisiness.value)
            || !std::isfinite(d.orphanResidualRatio.value)
            || !std::isfinite(hnr.harmonicToNoiseRatioDb))
        {
            return fail("finite descriptor outputs required");
        }
    }

    std::cout << "timbre_energy_noisiness_test: ok\n";
    return 0;
}

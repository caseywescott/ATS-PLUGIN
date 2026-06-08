#include "SpectralEnvelope.h"
#include "TimbreDescriptors.h"
#include "TimbreEnergyDistribution.h"
#include "TimbreFrameAdapter.h"
#include "TimbreFrameAnalysis.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_envelope_deviation_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorOptions makeOptions()
{
    ats::TimbreDescriptorOptions opts;
    opts.comparisonAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    opts.comparisonAxis.minFrequencyHz = 100.0;
    opts.comparisonAxis.maxFrequencyHz = 10000.0;
    opts.comparisonAxis.sampleRate = 48000.0;
    opts.comparisonGridSize = 64;
    return opts;
}

ats::FrameEnvelope stubEnvelope()
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = "stub/sampler";
    envelope.estimatorVersion = 1;
    envelope.sampleRate = 48000.0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    envelope.values = { 1.0 };
    return envelope;
}

ats::EnvelopeSamplerFn constantDbSampler(double db)
{
    const double amp = std::pow(10.0, db / 20.0);
    return [amp](const ats::FrameEnvelope&, double freq, bool* ok) -> double {
        if (freq <= 0.0)
        {
            if (ok != nullptr) *ok = false;
            return 0.0;
        }
        if (ok != nullptr) *ok = true;
        return amp;
    };
}

} // namespace

int main()
{
    const auto options = makeOptions();
    const auto envelope = stubEnvelope();
    const ats::EnvelopeDeviationOptions deviationOptions;

    // ---- Smooth envelope: partials lie exactly on a constant -20 dB envelope
    //      so envelope_deviation_rms_db must be 0. ----
    {
        ats::EnvelopeFrameInput frame;
        const double amplitude = std::pow(10.0, -20.0 / 20.0);
        for (int i = 0; i < 6; ++i)
        {
            const double frequency = 200.0 + 600.0 * i;
            frame.partials.push_back({ frequency, amplitude, 20.0, 1.0, true });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions,
            envelope, constantDbSampler(-20.0), d);
        if (d.envelopeDeviationRmsDb.status != ats::TimbreDescriptorStatus::Available)
            return fail("smooth envelope: status must be Available");
        if (std::abs(d.envelopeDeviationRmsDb.value) > 1.0e-6)
            return fail("smooth envelope: RMS dB must be 0");
    }

    // ---- Known deviation: partials at +3 and -3 dB relative to a constant
    //      -20 dB envelope -> envelope_deviation_rms_db should equal 3. ----
    {
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < 8; ++i)
        {
            const double frequency = 200.0 + 600.0 * i;
            const double db = (i % 2 == 0) ? -17.0 : -23.0;
            const double amplitude = std::pow(10.0, db / 20.0);
            frame.partials.push_back({ frequency, amplitude, 20.0, 1.0, true });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions,
            envelope, constantDbSampler(-20.0), d);
        if (std::abs(d.envelopeDeviationRmsDb.value - 3.0) > 0.05)
            return fail("known deviation: RMS dB must equal 3");
    }

    // ---- Alternating amplitudes: irregularity > 0. Compute the expected
    //      value analytically with a 3-bin local-mean window:
    //      partials = [1, 0, 1, 0, 1, 0, 1, 0]
    //      i=0: local_mean = (1+0)/2 = 0.5; |1-0.5| = 0.5
    //      i=1..6 (middle): local_mean = sum/3; |a_i - mean|
    //      i=7 (last): local_mean = (0+0)/2 = 0; |0-0| = 0
    //      With amplitudes alternating 1.0 and 0.01 (avoid floor),
    //      irregularity is large.
    {
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < 8; ++i)
        {
            const double frequency = 200.0 + 600.0 * i;
            const double amplitude = (i % 2 == 0) ? 1.0 : 0.01;
            frame.partials.push_back({ frequency, amplitude, 20.0, 1.0, true });
        }
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions,
            envelope, constantDbSampler(-6.0), d);
        if (d.adjacentPartialIrregularity.status != ats::TimbreDescriptorStatus::Available)
            return fail("alternating: irregularity must be Available");
        if (d.adjacentPartialIrregularity.value < 0.2)
            return fail("alternating: irregularity must be large");
    }

    // ---- Sparse partials: irregularity reports TooSparse ----
    {
        ats::EnvelopeFrameInput frame;
        frame.partials = {
            { 500.0, 1.0, 20.0, 1.0, true },
            { 1000.0, 0.5, 20.0, 1.0, true },
        };
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions,
            envelope, constantDbSampler(-6.0), d);
        if (d.adjacentPartialIrregularity.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("sparse: irregularity must be TooSparse");
        if (d.envelopeDeviationRmsDb.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("sparse: deviation must be TooSparse");
    }

    // ---- Missing envelope: deviation reports MissingEnvelope, irregularity
    //      can still be computed from partials alone. ----
    {
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < 6; ++i)
            frame.partials.push_back({ 200.0 + 200.0 * i, 0.5, 20.0, 1.0, true });
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::FrameEnvelope empty;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions, empty, {}, d);
        if (d.envelopeDeviationRmsDb.status != ats::TimbreDescriptorStatus::MissingEnvelope)
            return fail("missing envelope: deviation must be MissingEnvelope");
        if (d.adjacentPartialIrregularity.status != ats::TimbreDescriptorStatus::Available)
            return fail("missing envelope: irregularity still Available");
    }

    // ---- Global gain scaling: irregularity (ratio of amplitudes) is
    //      gain-invariant. Envelope deviation in dB is also gain-invariant
    //      when partial amplitudes and the envelope shift together. ----
    {
        ats::EnvelopeFrameInput baseline;
        for (int i = 0; i < 6; ++i)
        {
            const double db = (i % 2 == 0) ? -10.0 : -14.0;
            baseline.partials.push_back(
                { 200.0 + 400.0 * i, std::pow(10.0, db / 20.0), 20.0, 1.0, true });
        }
        ats::EnvelopeFrameInput scaled = baseline;
        const double k = 0.25;
        for (auto& p : scaled.partials)
            p.amplitude *= k;

        const auto samplesA = ats::validateEnvelopeFrameInput(baseline);
        const auto samplesB = ats::validateEnvelopeFrameInput(scaled);
        ats::TimbreDescriptorFrame dA;
        ats::TimbreDescriptorFrame dB;
        // Envelope shifts to keep relative dB the same.
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samplesA, options, deviationOptions, envelope,
            constantDbSampler(-12.0), dA);
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samplesB, options, deviationOptions, envelope,
            constantDbSampler(-12.0 + 20.0 * std::log10(k)), dB);

        if (std::abs(dA.adjacentPartialIrregularity.value
                     - dB.adjacentPartialIrregularity.value) > 1.0e-9)
        {
            return fail("gain invariance: irregularity changed");
        }
        if (std::abs(dA.envelopeDeviationRmsDb.value
                     - dB.envelopeDeviationRmsDb.value) > 1.0e-6)
        {
            return fail("gain invariance: deviation RMS dB changed");
        }
    }

    // ---- Dominant partial fraction edge cases ----
    {
        // Single partial -> dominance = 1.0
        ats::EnvelopeFrameInput frame;
        frame.partials = { { 1000.0, 1.0, 20.0, 1.0, true } };
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions, envelope, {}, d);
        if (d.dominantPartialFraction.status != ats::TimbreDescriptorStatus::Available)
            return fail("dominance: single-partial status must be Available");
        if (std::abs(d.dominantPartialFraction.value - 1.0) > 1.0e-12)
            return fail("dominance: single-partial value must be 1");
    }
    {
        // Equal partials -> dominance = 1 / N
        ats::EnvelopeFrameInput frame;
        for (int i = 0; i < 4; ++i)
            frame.partials.push_back({ 500.0 + 500.0 * i, 1.0, 20.0, 1.0, true });
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions, envelope, {}, d);
        if (std::abs(d.dominantPartialFraction.value - 0.25) > 1.0e-12)
            return fail("dominance: equal-partials value must be 1/N");
    }
    {
        // Empty partials -> EmptyInput, finite value.
        ats::EnvelopeFrameInput frame;
        const auto samples = ats::validateEnvelopeFrameInput(frame);
        ats::TimbreDescriptorFrame d;
        ats::analyzeEnvelopeDeviationAndIrregularity(
            samples, options, deviationOptions, envelope, {}, d);
        if (d.dominantPartialFraction.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("dominance: empty must be EmptyInput");
        if (!std::isfinite(d.dominantPartialFraction.value))
            return fail("dominance: empty value must be finite");
    }

    std::cout << "timbre_envelope_deviation_test: ok\n";
    return 0;
}

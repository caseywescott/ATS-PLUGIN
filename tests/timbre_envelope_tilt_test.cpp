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
    std::cerr << "timbre_envelope_tilt_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorOptions makeOptions()
{
    ats::TimbreDescriptorOptions opts;
    opts.comparisonAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    opts.comparisonAxis.minFrequencyHz = 200.0;
    opts.comparisonAxis.maxFrequencyHz = 12800.0;
    opts.comparisonAxis.sampleRate = 48000.0;
    opts.comparisonGridSize = 64;
    return opts;
}

// "Estimator A": linear-amplitude sampler with slope `slopeDbPerOctave` and
// dB intercept `interceptDb` at the reference frequency.
ats::EnvelopeSamplerFn makeLogLineSampler(double interceptDb,
                                           double slopeDbPerOctave,
                                           double referenceHz)
{
    return [interceptDb, slopeDbPerOctave, referenceHz](
               const ats::FrameEnvelope&,
               double frequencyHz,
               bool* ok) -> double {
        if (frequencyHz <= 0.0 || !std::isfinite(frequencyHz))
        {
            if (ok != nullptr) *ok = false;
            return 0.0;
        }
        const double db = interceptDb
            + slopeDbPerOctave * std::log2(frequencyHz / referenceHz);
        if (ok != nullptr) *ok = true;
        return std::pow(10.0, db / 20.0);
    };
}

// "Estimator B": adds a small zero-mean cosine ripple on top of the log-log
// line to verify that two estimators with similar broad shape but different
// micro-structure agree on tilt within a tight tolerance.
ats::EnvelopeSamplerFn makeRippleSampler(double interceptDb,
                                          double slopeDbPerOctave,
                                          double referenceHz,
                                          double rippleDb,
                                          double rippleOctavePeriod)
{
    return [=](const ats::FrameEnvelope&,
                double frequencyHz,
                bool* ok) -> double {
        if (frequencyHz <= 0.0 || !std::isfinite(frequencyHz))
        {
            if (ok != nullptr) *ok = false;
            return 0.0;
        }
        const double octaves = std::log2(frequencyHz / referenceHz);
        const double db = interceptDb + slopeDbPerOctave * octaves
            + rippleDb * std::cos(2.0 * M_PI * octaves / rippleOctavePeriod);
        if (ok != nullptr) *ok = true;
        return std::pow(10.0, db / 20.0);
    };
}

ats::FrameEnvelope makeStubEnvelope()
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = "stub/sampler";
    envelope.estimatorVersion = 1;
    envelope.sampleRate = 48000.0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    // Any non-empty value list signals "envelope present" to the analyzer; the
    // sampler defines actual amplitudes.
    envelope.values = { 1.0 };
    return envelope;
}

} // namespace

int main()
{
    const auto options = makeOptions();
    const auto envelope = makeStubEnvelope();
    ats::EnvelopeTiltOptions tilt;
    tilt.referenceHz = 1000.0;

    // ---- Known slope -6 dB/oct ----
    {
        const double expectedSlope = -6.0;
        const auto sampler = makeLogLineSampler(0.0, expectedSlope, tilt.referenceHz);
        ats::TimbreDescriptorFrame frame;
        const auto diagnostics = ats::analyzeFittedEnvelopeTilt(
            envelope, options, tilt, sampler, frame);

        if (frame.tiltDbPerOctave.status != ats::TimbreDescriptorStatus::Available)
            return fail("known slope: status must be Available");
        if (std::abs(frame.tiltDbPerOctave.value - expectedSlope) > 0.05)
            return fail("known slope: recovered tilt is not -6 dB/oct");
        if (diagnostics.residualRmsDb > 0.01)
            return fail("known slope: residual RMS must be tiny on a perfect line");
        if (frame.tiltDbPerOctave.confidence < 0.95)
            return fail("known slope: confidence must be near 1");
    }

    // ---- Global gain invariance: intercept changes, tilt does not ----
    {
        const auto samplerA = makeLogLineSampler(-20.0, -3.0, tilt.referenceHz);
        const auto samplerB = makeLogLineSampler(20.0,  -3.0, tilt.referenceHz);
        ats::TimbreDescriptorFrame frameA;
        ats::TimbreDescriptorFrame frameB;
        const auto diagA = ats::analyzeFittedEnvelopeTilt(
            envelope, options, tilt, samplerA, frameA);
        const auto diagB = ats::analyzeFittedEnvelopeTilt(
            envelope, options, tilt, samplerB, frameB);

        if (std::abs(frameA.tiltDbPerOctave.value - frameB.tiltDbPerOctave.value)
            > 1.0e-9)
        {
            return fail("gain invariance: tilt must be unchanged by gain");
        }
        if (std::abs(diagA.interceptDb - diagB.interceptDb) < 39.0)
            return fail("gain invariance: intercept must reflect gain shift");
    }

    // ---- Estimator comparison: two estimators with the same broad slope but
    //      different fine structure should report similar tilt (within ~0.5
    //      dB/oct of expected). ----
    {
        const double expectedSlope = -4.0;
        const auto samplerLine = makeLogLineSampler(0.0, expectedSlope, tilt.referenceHz);
        const auto samplerRipple = makeRippleSampler(
            0.0, expectedSlope, tilt.referenceHz, /*rippleDb=*/3.0,
            /*rippleOctavePeriod=*/2.5);
        ats::TimbreDescriptorFrame lineFrame;
        ats::TimbreDescriptorFrame rippleFrame;
        ats::analyzeFittedEnvelopeTilt(envelope, options, tilt, samplerLine, lineFrame);
        ats::analyzeFittedEnvelopeTilt(envelope, options, tilt, samplerRipple, rippleFrame);

        const double lineTilt = lineFrame.tiltDbPerOctave.value;
        const double rippleTilt = rippleFrame.tiltDbPerOctave.value;
        if (std::abs(lineTilt - expectedSlope) > 0.1)
            return fail("estimator comparison: line tilt not within 0.1 dB/oct");
        if (std::abs(rippleTilt - expectedSlope) > 0.6)
            return fail("estimator comparison: ripple tilt not within 0.6 dB/oct");
        if (std::abs(lineTilt - rippleTilt) > 0.6)
            return fail("estimator comparison: estimators disagree by > 0.6 dB/oct");
        // Ripple residual must be larger than line residual.
        // (We do not assert hard bounds since the regression is least-squares.)
    }

    // ---- Empty envelope returns MissingEnvelope ----
    {
        ats::FrameEnvelope empty;
        ats::TimbreDescriptorFrame frame;
        const auto sampler = makeLogLineSampler(0.0, -3.0, tilt.referenceHz);
        ats::analyzeFittedEnvelopeTilt(empty, options, tilt, sampler, frame);
        if (frame.tiltDbPerOctave.status != ats::TimbreDescriptorStatus::MissingEnvelope)
            return fail("empty envelope: status must be MissingEnvelope");
        if (!std::isfinite(frame.tiltDbPerOctave.value))
            return fail("empty envelope: value must remain finite");
    }

    // ---- Null sampler returns MissingEnvelope ----
    {
        ats::TimbreDescriptorFrame frame;
        ats::analyzeFittedEnvelopeTilt(envelope, options, tilt, {}, frame);
        if (frame.tiltDbPerOctave.status != ats::TimbreDescriptorStatus::MissingEnvelope)
            return fail("null sampler: status must be MissingEnvelope");
    }

    // ---- Confidence drops as the envelope deviates from a line ----
    {
        const auto samplerSmooth = makeRippleSampler(
            0.0, -3.0, tilt.referenceHz, 0.5, 2.0);
        const auto samplerRough = makeRippleSampler(
            0.0, -3.0, tilt.referenceHz, 18.0, 2.0);
        ats::TimbreDescriptorFrame smooth;
        ats::TimbreDescriptorFrame rough;
        ats::analyzeFittedEnvelopeTilt(envelope, options, tilt, samplerSmooth, smooth);
        ats::analyzeFittedEnvelopeTilt(envelope, options, tilt, samplerRough, rough);
        if (smooth.tiltDbPerOctave.confidence <= rough.tiltDbPerOctave.confidence)
            return fail("confidence must decrease as residual RMS grows");
    }

    std::cout << "timbre_envelope_tilt_test: ok\n";
    return 0;
}

#include "F0AdaptiveEnvelope.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

ats::EnvelopeAxisSpec axis()
{
    ats::EnvelopeAxisSpec a;
    a.type = ats::EnvelopeFrequencyAxis::LinearHz;
    a.minFrequencyHz = 80.0;
    a.maxFrequencyHz = 8000.0;
    a.sampleRate = 48000.0;
    return a;
}

double gaussian(const double x, const double center, const double width)
{
    const double normalized = (x - center) / width;
    return std::exp(-0.5 * normalized * normalized);
}

double voiceLikeEnvelopeDb(const double frequencyHz)
{
    return -46.0 +
           18.0 * gaussian(frequencyHz, 720.0, 170.0) +
           11.0 * gaussian(frequencyHz, 2300.0, 360.0) +
           6.0 * gaussian(frequencyHz, 4200.0, 620.0);
}

std::vector<ats::EnvelopeSpectrumPoint> voiceLikeWaveformSpectrum(const double f0Hz)
{
    std::vector<ats::EnvelopeSpectrumPoint> spectrum;
    for (int i = 0; i < 260; ++i)
    {
        const double frequencyHz = 80.0 + static_cast<double>(i) * (8000.0 - 80.0) / 259.0;
        const double harmonicPhase = std::cos(2.0 * 3.141592653589793238462643383279502884 *
                                             frequencyHz / f0Hz);
        const double rippleDb = 2.0 * harmonicPhase;
        const double amplitude = std::pow(10.0, (voiceLikeEnvelopeDb(frequencyHz) + rippleDb) / 20.0);
        spectrum.push_back({ frequencyHz, amplitude });
    }
    return spectrum;
}

double meanEnvelopeError(const ats::FrameEnvelope& envelope)
{
    double sum = 0.0;
    for (int i = 0; i < 25; ++i)
    {
        const double frequencyHz = 120.0 + static_cast<double>(i) * (7600.0 - 120.0) / 24.0;
        bool ok = false;
        const double actual = ats::evaluateF0AdaptiveEnvelope(envelope, frequencyHz, &ok);
        if (!ok)
            return 1.0e9;
        sum += std::abs(actual - voiceLikeEnvelopeDb(frequencyHz));
    }
    return sum / 25.0;
}

ats::F0AdaptiveEnvelopeInput validInput()
{
    ats::F0AdaptiveEnvelopeInput input;
    input.sourceModelId = "singing_voice_waveform_fixture";
    input.frameIndex = 7;
    input.sampleRate = 48000.0;
    input.waveformAvailable = true;
    input.fundamentalFrequencyHz = 180.0;
    input.f0Confidence = 0.96;
    input.spectrum = voiceLikeWaveformSpectrum(input.fundamentalFrequencyHz);
    return input;
}

ats::F0AdaptiveEnvelopeOptions options()
{
    ats::F0AdaptiveEnvelopeOptions opts;
    opts.frequencyAxis = axis();
    opts.sampleCount = 128;
    opts.minF0Confidence = 0.9;
    opts.smoothingBandwidthInF0 = 1.8;
    opts.minimumBandwidthHz = 90.0;
    return opts;
}

} // namespace

int main()
{
    const auto opts = options();

    ats::F0AdaptiveEnvelopeFitResult result;
    if (!ats::fitF0AdaptiveEnvelope(validInput(), opts, result))
        return fail("F0-adaptive fit failed: " + result.errorMessage);
    if (result.envelope.estimatorId != ats::F0AdaptiveEnvelopeEstimator::kId)
        return fail("F0-adaptive fit did not store estimator id");
    if (result.envelope.penaltyType != "f0_adaptive_waveform")
        return fail("F0-adaptive fit did not record prototype type");
    if (result.envelope.storage != ats::EnvelopeValueStorage::Samples ||
        result.envelope.values.size() != static_cast<std::size_t>(opts.sampleCount))
    {
        return fail("F0-adaptive fit did not store sampled values");
    }
    if (!(meanEnvelopeError(result.envelope) < 2.4))
        return fail("F0-adaptive voice-like fixture fit error was too high");

    auto noWaveform = validInput();
    noWaveform.waveformAvailable = false;
    ats::F0AdaptiveEnvelopeFitResult noWaveformResult;
    if (ats::fitF0AdaptiveEnvelope(noWaveform, opts, noWaveformResult))
        return fail("F0-adaptive fit should require waveform availability");
    if (noWaveformResult.diagnostics.fallbackReason != ats::EnvelopeFallbackReason::Unsupported)
        return fail("missing waveform should be reported as unsupported");

    auto weakF0 = validInput();
    weakF0.f0Confidence = 0.25;
    ats::F0AdaptiveEnvelopeFitResult weakF0Result;
    if (ats::fitF0AdaptiveEnvelope(weakF0, opts, weakF0Result))
        return fail("F0-adaptive fit should require reliable F0");
    if (!weakF0Result.diagnostics.hasWarning(ats::EnvelopeWarningFallback))
        return fail("unreliable F0 should produce a diagnostic warning");

    auto sparseSpectrum = validInput();
    sparseSpectrum.spectrum.resize(4);
    ats::F0AdaptiveEnvelopeFitResult sparseResult;
    if (ats::fitF0AdaptiveEnvelope(sparseSpectrum, opts, sparseResult))
        return fail("F0-adaptive fit should reject sparse waveform spectra");
    if (sparseResult.diagnostics.fallbackReason != ats::EnvelopeFallbackReason::TooFewPoints)
        return fail("sparse waveform spectrum should report too few points");

    ats::F0AdaptiveEnvelopeEstimator estimator;
    ats::EnvelopeEstimatorContext context;
    context.sampleRate = 48000.0;
    context.frequencyAxis = axis();
    std::string error;
    if (!estimator.prepare(context, &error))
        return fail("F0-adaptive estimator prepare failed: " + error);

    ats::EnvelopeFrameInput frame;
    frame.sourceModelId = "interface_voice_fixture";
    frame.frameIndex = 2;
    frame.sampleRate = 48000.0;
    frame.waveformAvailable = true;
    frame.fundamentalFrequencyHz = 180.0;
    frame.f0Confidence = 0.96;
    frame.waveformSpectrum = voiceLikeWaveformSpectrum(frame.fundamentalFrequencyHz);

    ats::FrameEnvelope envelope;
    if (!estimator.estimate(frame, envelope, &error))
        return fail("F0-adaptive estimator interface failed: " + error);

    ats::EnvelopeFrameInput partialOnlyFrame;
    partialOnlyFrame.sampleRate = 48000.0;
    partialOnlyFrame.partials.push_back({ 440.0, 0.5, 10.0, 1.0, true });
    if (estimator.estimate(partialOnlyFrame, envelope, &error))
        return fail("F0-adaptive estimator should stay gated for partial-only frames");

    std::cout << "ok: F0-adaptive waveform envelope prototype\n";
    return 0;
}

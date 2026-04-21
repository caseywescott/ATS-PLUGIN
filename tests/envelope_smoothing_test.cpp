#include "SpectralEnvelopeSmoothing.h"

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

ats::FrameEnvelope makeEnvelope(const std::uint64_t frameIndex, const double offset)
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = "rdc/weighted";
    envelope.estimatorVersion = 1;
    envelope.sourceModelId = "smooth-fixture";
    envelope.frameIndex = frameIndex;
    envelope.sampleRate = 48000.0;
    envelope.frequencyAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    envelope.frequencyAxis.minFrequencyHz = 100.0;
    envelope.frequencyAxis.maxFrequencyHz = 8000.0;
    envelope.frequencyAxis.sampleRate = 48000.0;
    envelope.envelopeOrder = 2;
    envelope.storage = ats::EnvelopeValueStorage::Coefficients;
    envelope.values = { -20.0 + offset, 1.5 - offset * 0.25, -0.5 + offset * 0.1 };
    return envelope;
}

bool sameValues(const ats::FrameEnvelope& a, const ats::FrameEnvelope& b)
{
    if (a.values.size() != b.values.size())
        return false;
    for (std::size_t i = 0; i < a.values.size(); ++i)
    {
        if (std::abs(a.values[i] - b.values[i]) > 1.0e-12)
            return false;
    }
    return true;
}

} // namespace

int main()
{
    std::vector<ats::EnvelopeSmoothingFrame> frames;
    const std::vector<double> offsets = { 0.0, 1.0, -1.0, 8.0, 0.75, -0.75, 0.5 };
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        ats::EnvelopeSmoothingFrame frame;
        frame.envelope = makeEnvelope(static_cast<std::uint64_t>(i), offsets[i]);
        frame.transientBoundary = i == 3;
        frames.push_back(frame);
    }

    std::vector<ats::FrameEnvelope> raw;
    for (const auto& frame : frames)
        raw.push_back(frame.envelope);

    ats::EnvelopeSmoothingOptions options;
    options.amount = 0.65;
    options.resetOnTransient = true;
    const auto smoothed = ats::smoothEnvelopeSeries(frames, options);
    if (smoothed.size() != raw.size())
        return fail("smoothed series size mismatch");

    if (!sameValues(smoothed[0], raw[0]))
        return fail("first frame should not be smoothed");
    if (!sameValues(smoothed[3], raw[3]))
        return fail("transient boundary frame should not be smoothed");
    if (smoothed[2].smoothingAmount <= 0.0)
        return fail("sustained frame was not marked as smoothed");
    if (smoothed[3].smoothingAmount != 0.0)
        return fail("transient boundary frame smoothing amount should reset");

    const double rawPreTransientShimmer = ats::envelopeSeriesShimmerRms({ raw[0], raw[1], raw[2] });
    const double smoothedPreTransientShimmer = ats::envelopeSeriesShimmerRms({ smoothed[0], smoothed[1], smoothed[2] });
    if (!(smoothedPreTransientShimmer < rawPreTransientShimmer))
        return fail("smoothing did not reduce pre-transient shimmer");

    ats::FrameEnvelope sampleA = makeEnvelope(10, 0.0);
    sampleA.storage = ats::EnvelopeValueStorage::Samples;
    sampleA.values = { -12.0 };
    ats::FrameEnvelope sampleB = sampleA;
    sampleB.frameIndex = 11;
    sampleB.values = { -6.0 };
    const auto sampleSmoothed = ats::smoothEnvelopeFrame(sampleA, sampleB, options);
    if (std::abs(sampleSmoothed.values.front() - (-9.9)) > 1.0e-12)
        return fail("sample envelope smoothing mismatch");

    std::cout << "ok: spectral envelope temporal smoothing\n";
    return 0;
}

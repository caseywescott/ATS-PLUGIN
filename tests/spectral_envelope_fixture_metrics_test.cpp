#include "RegularizedCepstrum.h"
#include "SpectralEnvelopeHybrid.h"
#include "SpectralEnvelopeSmoothing.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr double kPi = 3.141592653589793238462643383279502884;

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

bool closeEnough(const double a, const double b, const double tolerance = 1.0e-6)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

ats::EnvelopeAxisSpec axis()
{
    ats::EnvelopeAxisSpec a;
    a.type = ats::EnvelopeFrequencyAxis::LinearHz;
    a.minFrequencyHz = 100.0;
    a.maxFrequencyHz = 12000.0;
    a.sampleRate = 48000.0;
    return a;
}

double positionForFrequency(const double frequencyHz)
{
    return (frequencyHz - 100.0) / (12000.0 - 100.0);
}

double denseEnvelopeDb(const double frequencyHz)
{
    const double theta = kPi * positionForFrequency(frequencyHz);
    return -22.0 + 2.5 * std::cos(theta) - 1.2 * std::cos(2.0 * theta) + 0.5 * std::cos(3.0 * theta);
}

ats::FrameEnvelope sampledEnvelope(const std::string& id, const std::vector<double>& values)
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = ats::RegularizedCepstrumEnvelopeEstimator::kId;
    envelope.estimatorVersion = ats::RegularizedCepstrumEnvelopeEstimator::kVersion;
    envelope.sourceModelId = id;
    envelope.frameIndex = 0;
    envelope.sampleRate = 48000.0;
    envelope.frequencyAxis = axis();
    envelope.envelopeOrder = 0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    envelope.values = values;
    return envelope;
}

ats::FrameEnvelope smoothFixtureEnvelope(const std::uint64_t frameIndex, const double offset)
{
    auto envelope = sampledEnvelope("plucked_percussive_synthetic", { -18.0 + offset, -12.0 - offset * 0.2, -15.0 });
    envelope.frameIndex = frameIndex;
    return envelope;
}

double linearSourceEnvDb(const double frequencyHz)
{
    return -24.0 + 12.0 * positionForFrequency(frequencyHz);
}

double linearTargetEnvDb(const double frequencyHz)
{
    return -10.0 + 4.0 * positionForFrequency(frequencyHz);
}

double rmsError(const std::vector<double>& actual, const std::vector<double>& expected)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        const double error = actual[i] - expected[i];
        sum += error * error;
    }
    return std::sqrt(sum / static_cast<double>(actual.size()));
}

bool densePolyphonyFixturePasses()
{
    std::vector<ats::RdcFitPoint> points;
    for (int i = 0; i < 48; ++i)
    {
        const double t = (static_cast<double>(i) + 0.5) / 48.0;
        const double jitter = (i % 5 == 0) ? 17.0 : (i % 3 == 0 ? -11.0 : 0.0);
        const double frequencyHz = 100.0 + t * (12000.0 - 100.0) + jitter;
        points.push_back({ frequencyHz, denseEnvelopeDb(frequencyHz), 0.75 + 0.25 * (1.0 - t) });
    }

    ats::RdcFitOptions options;
    options.frequencyAxis = axis();
    options.order = 5;
    options.regularization = 0.05;
    options.penalty = ats::CepstrumPenalty::SecondDerivative;

    ats::RdcFitResult result;
    if (!ats::fitRegularizedDiscreteCepstrum(points, options, result))
        return false;

    return result.diagnostics.weightedError < 0.15 &&
           result.diagnostics.fallbackReason == ats::EnvelopeFallbackReason::None;
}

bool noisyResidualFixturePasses()
{
    ats::SpectralEnvelopeTransferRecipe recipe;
    recipe.residualPolicy = ats::HybridResidualPolicy::DecolorThenColor;
    recipe.residualNormalization = ats::HybridNormalizationPolicy::EnergyPreserving;
    recipe.preserveOrphanResidual = true;

    const auto source = sampledEnvelope("noisy_texture_source", { -18.0, -15.0, -20.0 });
    const auto target = sampledEnvelope("noisy_texture_target", { -10.0, -7.0, -12.0 });
    const std::vector<ats::HybridResidualBandInput> bands = {
        { 300.0, 0.20, false },
        { 1800.0, 0.11, false },
        { 7000.0, 0.05, true },
    };

    const auto out = ats::applyResidualEnvelopeTransfer(bands, source, target, recipe);
    if (out.size() != bands.size() || !closeEnough(out.back().outputEnergy, bands.back().energy))
        return false;

    double sourceEnergy = 0.0;
    double outputEnergy = 0.0;
    for (const auto& band : out)
    {
        if (!band.ok)
            return false;
        sourceEnergy += band.sourceEnergy;
        outputEnergy += band.outputEnergy;
    }
    return closeEnough(sourceEnergy, outputEnergy, 1.0e-6);
}

bool pluckedTransientFixturePasses()
{
    std::vector<ats::EnvelopeSmoothingFrame> frames;
    for (std::size_t i = 0; i < 5; ++i)
    {
        ats::EnvelopeSmoothingFrame frame;
        frame.envelope = smoothFixtureEnvelope(static_cast<std::uint64_t>(i),
                                               i == 2 ? 9.0 : (i % 2 == 0 ? 0.5 : -0.5));
        frame.transientBoundary = i == 2;
        frames.push_back(frame);
    }

    std::vector<ats::FrameEnvelope> raw;
    for (const auto& frame : frames)
        raw.push_back(frame.envelope);

    ats::EnvelopeSmoothingOptions options;
    options.amount = 0.7;
    options.resetOnTransient = true;
    const auto smoothed = ats::smoothEnvelopeSeries(frames, options);
    if (smoothed.size() != raw.size() ||
        smoothed[2].values != raw[2].values ||
        smoothed[2].smoothingAmount != 0.0 ||
        smoothed[3].smoothingGroup == smoothed[1].smoothingGroup)
    {
        return false;
    }

    return ats::envelopeSeriesShimmerRms({ smoothed[0], smoothed[1] }) <
           ats::envelopeSeriesShimmerRms({ raw[0], raw[1] });
}

bool pairedTransferFixturePasses()
{
    ats::SpectralEnvelopeTransferRecipe recipe;
    recipe.partialNormalization = ats::HybridNormalizationPolicy::None;
    recipe.sourceDecolorAmount = 1.0;
    recipe.targetColorAmount = 1.0;

    const auto source = sampledEnvelope("paired_source", { linearSourceEnvDb(100.0), linearSourceEnvDb(12000.0) });
    const auto target = sampledEnvelope("paired_target", { linearTargetEnvDb(100.0), linearTargetEnvDb(12000.0) });
    const std::vector<double> frequencies = { 173.0, 641.0, 1559.0, 4337.0, 9323.0 };
    const std::vector<double> fineDetailDb = { -3.0, -1.5, -4.0, -2.0, -5.5 };

    std::vector<ats::HybridPartialInput> partials;
    std::vector<double> expectedDb;
    for (std::size_t i = 0; i < frequencies.size(); ++i)
    {
        partials.push_back({ frequencies[i], ats::dbToAmplitude(fineDetailDb[i] + linearSourceEnvDb(frequencies[i])) });
        expectedDb.push_back(fineDetailDb[i] + linearTargetEnvDb(frequencies[i]));
    }

    const auto transferred = ats::applyEnvelopeTransferDirect(partials, source, target, recipe);
    recipe.sourceDecolorAmount = 0.0;
    const auto targetOnly = ats::applyEnvelopeTransferDirect(partials, source, target, recipe);

    std::vector<double> transferredDb;
    std::vector<double> targetOnlyDb;
    for (std::size_t i = 0; i < transferred.size(); ++i)
    {
        if (!transferred[i].ok || !targetOnly[i].ok)
            return false;
        transferredDb.push_back(ats::amplitudeToDb(transferred[i].outputAmplitude, recipe.amplitudeFloor));
        targetOnlyDb.push_back(ats::amplitudeToDb(targetOnly[i].outputAmplitude, recipe.amplitudeFloor));
    }

    return rmsError(transferredDb, expectedDb) < 1.0e-9 &&
           rmsError(targetOnlyDb, expectedDb) > 4.0;
}

} // namespace

int main()
{
    if (!densePolyphonyFixturePasses())
        return fail("dense polyphony fixture metrics failed");
    if (!noisyResidualFixturePasses())
        return fail("noisy residual fixture metrics failed");
    if (!pluckedTransientFixturePasses())
        return fail("plucked transient fixture metrics failed");
    if (!pairedTransferFixturePasses())
        return fail("paired source/target transfer fixture metrics failed");

    std::cout << "ok: spectral envelope fixture metrics\n";
    return 0;
}

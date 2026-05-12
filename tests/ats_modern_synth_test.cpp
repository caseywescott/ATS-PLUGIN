#include "AtsModernSynth.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int failures = 0;
constexpr double kHalfPi = 1.57079632679489661923;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        ++failures;
    }
}

ats::ModernAtsModel makeBaseModel(int partialCount = 1, int frameCount = 4)
{
    ats::ModernAtsModel model;
    model.header.schemaVersion = ats::kModernAtsSchemaVersion;
    model.header.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
    model.header.trackerMode = ats::ModernAtsTrackerMode::ModernAssignment;
    model.header.sampleRate = 48000;
    model.header.hopSizeSamples = 64;
    model.header.windowSizeSamples = 256;
    model.header.parameterHash = "synth-params";
    model.header.sourceHash = "synth-source";

    std::vector<int> activeIds;
    for (int p = 0; p < partialCount; ++p)
        activeIds.push_back(p);

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        ats::ModernAtsFrame frame;
        frame.timeSeconds = (double)(frameIndex * model.header.hopSizeSamples)
                            / (double)model.header.sampleRate;
        frame.activePartialIds = activeIds;
        frame.orphanResidualBandEnergy.assign(ats::kModernAtsDefaultResidualBands, 0.0);
        model.frames.push_back(frame);
    }

    for (int p = 0; p < partialCount; ++p)
    {
        ats::ModernAtsPartial partial;
        partial.id = p;
        partial.label = "p" + std::to_string(p);
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            partial.frames.push_back({ 0.05 + 0.001 * (double)(p % 7),
                                       110.0 + 13.0 * (double)p,
                                       0.0,
                                       50.0,
                                       0.0,
                                       1.0,
                                       frameIndex == 0 ? ats::ModernAtsFrameReason::Born
                                                       : ats::ModernAtsFrameReason::Matched });
        }
        model.partials.push_back(partial);
    }

    return model;
}

float peakAbs(const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (const float sample : samples)
        peak = std::max(peak, std::abs(sample));
    return peak;
}

double meanAbsDiff(const std::vector<float>& samples)
{
    if (samples.size() < 2)
        return 0.0;

    double total = 0.0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        total += std::abs(static_cast<double>(samples[i]) - static_cast<double>(samples[i - 1]));
    return total / static_cast<double>(samples.size() - 1);
}

void testSnapshotRenderAndSafety()
{
    const auto model = makeBaseModel();
    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 64;
    config.orphanResidualGain = 0.0;
    config.transientGain = 0.0;

    ats::ModernAtsRenderPlan plan;
    std::string error;
    expect(ats::prepareModernAtsRenderPlan(model, config, plan, error), error.c_str());
    expect(plan.totalSamples == 256, "render plan total samples mismatch");

    const auto safety = ats::checkModernAtsRealtimeSafety(plan);
    expect(safety.renderIntoCallerOwnedBuffer, "modern render plan should use caller-owned output");
    expect(!safety.allocatesDuringRender, "modern render should not allocate in renderInto");
    expect(!safety.locksDuringRender, "modern render should not lock in renderInto");
    expect(!safety.performsFileIoDuringRender, "modern render should not perform file I/O");
    expect(!safety.performsAnalysisDuringRender, "modern render should not perform analysis");

    std::vector<float> output((std::size_t)plan.totalSamples, 0.0f);
    expect(ats::renderModernAtsPlanIntoBuffer(plan, output.data(), (int)output.size()),
           "renderInto failed for modern snapshot");
    expect(peakAbs(output) > 0.0f, "modern snapshot render should be non-zero");
    expect(!ats::hasDenormalOrNonFiniteSamples(output.data(), (int)output.size()),
           "modern snapshot render produced denormal/non-finite samples");
}

void testRenderPlanScalesHopToOutputSampleRate()
{
    auto model = makeBaseModel(1, 4);
    model.header.sampleRate = 48000;
    model.header.hopSizeSamples = 64;

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 24000;
    config.hopSizeSamples = 0;
    config.orphanResidualGain = 0.0;
    config.transientGain = 0.0;

    ats::ModernAtsRenderPlan plan;
    std::string error;
    expect(ats::prepareModernAtsRenderPlan(model, config, plan, error), error.c_str());
    expect(plan.hopSizeSamples == 32, "render hop should scale from model rate to output rate");
    expect(plan.totalSamples == 128, "render length should preserve model duration across sample rates");

    config.timeFactor = 2.0;
    expect(ats::prepareModernAtsRenderPlan(model, config, plan, error), error.c_str());
    expect(plan.hopSizeSamples == 64, "time stretch should apply after output sample-rate hop scaling");
    expect(plan.totalSamples == 256, "time-stretched render length should scale after sample-rate conversion");
}

void testOrphanResidualGainAndSeed()
{
    auto model = makeBaseModel(0, 3);
    for (auto& frame : model.frames)
        frame.orphanResidualBandEnergy[4] = 0.25;

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 32;
    config.partialGain = 0.0;
    config.transientGain = 0.0;
    config.orphanResidualGain = 0.5;
    config.deterministicNoiseSeed = 12345u;

    const auto a = ats::renderModernAtsModel(model, config);
    const auto b = ats::renderModernAtsModel(model, config);
    expect(a == b, "orphan residual render should be deterministic with fixed seed");
    expect(peakAbs(a) > 0.0f, "orphan residual render should be non-zero");

    config.deterministicNoiseSeed = 54321u;
    const auto c = ats::renderModernAtsModel(model, config);
    expect(a != c, "orphan residual render should change when seed changes");

    config.orphanResidualGain = 0.0;
    const auto muted = ats::renderModernAtsModel(model, config);
    expect(peakAbs(muted) == 0.0f, "orphan residual gain should mute orphan residual render");
}

void testOrphanResidualBandsAreSpectrallyShaped()
{
    auto lowBandModel = makeBaseModel(0, 80);
    auto highBandModel = makeBaseModel(0, 80);
    for (auto& frame : lowBandModel.frames)
        frame.orphanResidualBandEnergy[2] = 0.5;
    for (auto& frame : highBandModel.frames)
        frame.orphanResidualBandEnergy[20] = 0.5;

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 64;
    config.partialGain = 0.0;
    config.transientGain = 0.0;
    config.orphanResidualGain = 0.8;
    config.deterministicNoiseSeed = 2468u;

    const auto lowBand = ats::renderModernAtsModel(lowBandModel, config);
    const auto highBand = ats::renderModernAtsModel(highBandModel, config);
    expect(!lowBand.empty() && !highBand.empty(), "band-shaped residual render should produce output");
    expect(peakAbs(lowBand) > 0.0f && peakAbs(highBand) > 0.0f,
           "band-shaped residual render should be audible");
    expect(meanAbsDiff(highBand) > meanAbsDiff(lowBand) * 2.0,
           "high residual Bark bands should render brighter/faster than low bands");
}

void testTransientBufferCrossfadeAndGain()
{
    auto model = makeBaseModel(0, 2);
    model.transients.push_back({ 0, 0, 0.0, 0.0, 0.00033333333333333332, 0.5 });
    ats::ModernAtsTransientBuffer buffer;
    buffer.markerIndex = 0;
    buffer.sampleRate = 48000;
    buffer.gain = 0.75;
    buffer.samples.assign(16, 1.0f);
    model.transientBuffers.push_back(buffer);

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 16;
    config.partialGain = 0.0;
    config.orphanResidualGain = 0.0;
    config.transientGain = 2.0;
    config.transientCrossfadeSamples = 4;

    const auto rendered = ats::renderModernAtsModel(model, config);
    expect(rendered.size() >= 16, "transient render too short");
    expect(rendered.front() == 0.0f, "transient crossfade should start at zero");
    expect(std::abs(rendered[8] - 0.75f) < 1.0e-5f, "transient gain path mismatch");
    expect(rendered[15] == 0.0f, "transient crossfade should end at zero");

    config.transientGain = 0.0;
    const auto muted = ats::renderModernAtsModel(model, config);
    expect(peakAbs(muted) == 0.0f, "transient gain should mute transient playback");
}

void testTransientBuffersFollowTimeFactor()
{
    auto model = makeBaseModel(0, 4);
    model.transients.push_back({ 0, 0, 8.0 / 48000.0, 8.0 / 48000.0, 12.0 / 48000.0, 1.0 });
    ats::ModernAtsTransientBuffer buffer;
    buffer.markerIndex = 0;
    buffer.sampleRate = 48000;
    buffer.gain = 1.0;
    buffer.samples.assign(4, 1.0f);
    model.transientBuffers.push_back(buffer);

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 16;
    config.partialGain = 0.0;
    config.orphanResidualGain = 0.0;
    config.transientGain = 1.0;
    config.transientCrossfadeSamples = 0;

    config.timeFactor = 2.0;
    const auto stretched = ats::renderModernAtsModel(model, config);
    expect(stretched.size() >= 24, "stretched transient render too short");
    expect(stretched[8] == 0.0f, "stretched transient should not play at unscaled source time");
    expect(stretched[16] > 0.9f, "stretched transient should start at scaled time");
    expect(stretched[23] > 0.9f, "stretched transient length should scale with time factor");

    config.timeFactor = 0.5;
    const auto compressed = ats::renderModernAtsModel(model, config);
    expect(compressed.size() >= 7, "compressed transient render too short");
    expect(compressed[4] > 0.9f, "compressed transient should start at scaled time");
    expect(compressed[6] == 0.0f, "compressed transient length should scale with time factor");
}

void testOutputSoftLimitBoundsDenseMix()
{
    auto quietModel = makeBaseModel(1, 2);
    for (auto& frame : quietModel.partials[0].frames)
    {
        frame.amplitude = 0.75;
        frame.frequencyHz = 0.0;
        frame.phaseRadians = kHalfPi;
    }

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 32;
    config.orphanResidualGain = 0.0;
    config.transientGain = 0.0;

    const auto quiet = ats::renderModernAtsModel(quietModel, config);
    expect(!quiet.empty(), "quiet limiter fixture should render");
    expect(std::abs(quiet.front() - 0.75f) < 1.0e-6f,
           "soft limiter should preserve samples below the knee");

    auto denseModel = makeBaseModel(96, 2);
    for (auto& partial : denseModel.partials)
    {
        for (auto& frame : partial.frames)
        {
            frame.amplitude = 0.08;
            frame.frequencyHz = 0.0;
            frame.phaseRadians = kHalfPi;
        }
    }

    ats::ModernAtsRenderStats limitedStats;
    const auto limited = ats::renderModernAtsModel(denseModel, config, &limitedStats);
    expect(!limited.empty(), "dense limiter fixture should render");
    expect(limitedStats.peakAbs <= 1.0f + 1.0e-6f,
           "modern render soft limiter should bound dense partial sums");
    expect(limitedStats.peakAbs > 0.95f, "modern render soft limiter should preserve audible headroom");

    config.outputCeiling = 0.0;
    ats::ModernAtsRenderStats rawStats;
    const auto raw = ats::renderModernAtsModel(denseModel, config, &rawStats);
    expect(!raw.empty(), "unlimited dense fixture should render");
    expect(rawStats.peakAbs > 4.0f, "disabled output limiter should expose the raw dense partial sum");
}

void testDenormalAndSilenceEdges()
{
    auto model = makeBaseModel(1, 2);
    for (auto& frame : model.partials[0].frames)
        frame.amplitude = 1.0e-30;

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 32;
    config.orphanResidualGain = 0.0;
    config.transientGain = 0.0;
    config.denormalClamp = 1.0e-20;

    const auto rendered = ats::renderModernAtsModel(model, config);
    expect(!rendered.empty(), "tiny-amplitude model should still render buffer length");
    expect(peakAbs(rendered) == 0.0f, "tiny-amplitude render should clamp to silence");
    expect(!ats::hasDenormalOrNonFiniteSamples(rendered.data(), (int)rendered.size()),
           "tiny-amplitude render produced denormal/non-finite samples");

    model.partials.clear();
    for (auto& frame : model.frames)
        frame.activePartialIds.clear();
    const auto silence = ats::renderModernAtsModel(model, config);
    expect(!silence.empty(), "empty modern model should preserve timeline length");
    expect(peakAbs(silence) == 0.0f, "empty modern model should render silence");
}

ats::ModernAtsModel makeBudgetModel(int partialCount, int frameCount, bool residual, bool transient)
{
    auto model = makeBaseModel(partialCount, frameCount);
    model.header.hopSizeSamples = 256;
    model.header.windowSizeSamples = 1024;
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        model.frames[(std::size_t)frameIndex].timeSeconds = (double)(frameIndex * 256) / 48000.0;
        if (residual)
            model.frames[(std::size_t)frameIndex]
                .orphanResidualBandEnergy[(std::size_t)(frameIndex % ats::kModernAtsDefaultResidualBands)] = 0.0001;
    }
    if (transient)
    {
        for (int frameIndex = 0; frameIndex < frameCount; frameIndex += 48)
        {
            const double onset = (double)(frameIndex * 256) / 48000.0;
            const int markerIndex = (int)model.transients.size();
            model.transients.push_back({ frameIndex, markerIndex, onset, onset, onset + 0.02, 0.5 });
            ats::ModernAtsTransientBuffer buffer;
            buffer.markerIndex = markerIndex;
            buffer.sampleRate = 48000;
            buffer.gain = 0.5;
            buffer.samples.assign(256, 0.01f);
            model.transientBuffers.push_back(buffer);
        }
    }
    return model;
}

void testRepresentativeBudgets()
{
    struct Shape
    {
        const char* name;
        int partials;
        int frames;
        bool residual;
        bool transient;
        double maxCpuRatio;
    };

    const Shape shapes[] = {
        { "sustained", 32, 180, true, false, 2.0 },
        { "vibrato", 40, 360, true, false, 2.5 },
        { "close_crossing_partials", 48, 300, true, false, 2.5 },
        { "plucked_percussive", 64, 240, true, true, 3.0 },
        { "noisy_voice_flute", 48, 300, true, false, 2.5 },
        { "dense_polyphony", 128, 480, true, false, 5.0 },
        { "percussive_hit", 96, 180, true, true, 4.0 },
    };

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 256;
    config.partialGain = 0.5;
    config.orphanResidualGain = 0.25;
    config.transientGain = 0.5;
    config.deterministicNoiseSeed = 42u;

    for (const auto& shape : shapes)
    {
        const auto model = makeBudgetModel(shape.partials, shape.frames, shape.residual, shape.transient);
        ats::ModernAtsRenderStats stats;
        const auto rendered = ats::renderModernAtsModel(model, config, &stats);
        std::cout << "modern_synth_budget " << shape.name << " samples=" << stats.totalSamples
                  << " state_bytes=" << stats.modelStateBytes << " output_bytes=" << stats.outputBytes
                  << " render_seconds=" << stats.renderSeconds
                  << " cpu_ratio=" << stats.cpuRatioAtRealtime << " peak=" << stats.peakAbs << '\n';
        expect(!rendered.empty(), "budget render produced empty output");
        expect(stats.cpuRatioAtRealtime < shape.maxCpuRatio, "budget render exceeded realtime CPU guardrail");
        expect(!ats::hasDenormalOrNonFiniteSamples(rendered.data(), (int)rendered.size()),
               "budget render produced denormal/non-finite samples");
    }
}

} // namespace

int main()
{
    testSnapshotRenderAndSafety();
    testRenderPlanScalesHopToOutputSampleRate();
    testOrphanResidualGainAndSeed();
    testOrphanResidualBandsAreSpectrallyShaped();
    testTransientBufferCrossfadeAndGain();
    testTransientBuffersFollowTimeFactor();
    testOutputSoftLimitBoundsDenseMix();
    testDenormalAndSilenceEdges();
    testRepresentativeBudgets();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

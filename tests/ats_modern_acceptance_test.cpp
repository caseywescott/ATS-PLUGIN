#include "AtsAnalysisPipeline.h"
#include "AtsModernAcceptance.h"
#include "AtsModernMorph.h"
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

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        ++failures;
    }
}

ats::TrackingPeak peak(int sourceIndex, double frequency, double amplitude = 0.5, double smr = 60.0)
{
    ats::TrackingPeak p;
    p.sourceIndex = sourceIndex;
    p.frequencyHz = frequency;
    p.amplitude = amplitude;
    p.smrDb = smr;
    return p;
}

bool frameIsActive(const ats::ModernAtsPartialFrame& frame)
{
    return frame.amplitude > 0.0 && frame.frequencyHz > 0.0
           && frame.reason != ats::ModernAtsFrameReason::Slept
           && frame.reason != ats::ModernAtsFrameReason::Pruned
           && frame.reason != ats::ModernAtsFrameReason::Rejected;
}

ats::ModernAtsModel modelFromTrackerResult(const ats::AssignmentTrackerResult& result,
                                           ats::ModernAtsAnalysisMode mode)
{
    ats::ModernAtsModel model;
    model.header.schemaVersion = ats::kModernAtsSchemaVersion;
    model.header.analysisMode = mode;
    model.header.trackerMode = mode == ats::ModernAtsAnalysisMode::ModernAts
                                   ? ats::ModernAtsTrackerMode::ModernAssignment
                                   : ats::ModernAtsTrackerMode::ClassicRecursiveFrequencySmr;
    model.header.sampleRate = 48000;
    model.header.hopSizeSamples = 64;
    model.header.windowSizeSamples = 256;
    model.header.parameterHash = mode == ats::ModernAtsAnalysisMode::ModernAts ? "modern" : "classic";
    model.header.sourceHash = "tracker-fixture";
    model.partials = result.partials;
    model.trackLifecycle = result.trackLifecycle;
    model.assignmentCosts = result.assignmentCosts;

    std::size_t frameCount = 0;
    for (const auto& partial : model.partials)
        frameCount = std::max(frameCount, partial.frames.size());

    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        ats::ModernAtsFrame frame;
        frame.timeSeconds = static_cast<double>(frameIndex * 64) / 48000.0;
        frame.orphanResidualBandEnergy.assign(ats::kModernAtsDefaultResidualBands, 0.0);
        for (const auto& partial : model.partials)
        {
            if (frameIndex < partial.frames.size() && frameIsActive(partial.frames[frameIndex]))
                frame.activePartialIds.push_back(partial.id);
        }
        model.frames.push_back(frame);
    }

    return model;
}

ats::ModernAtsModel makeSustainedModel(const std::string& id, double frequencyHz)
{
    ats::ModernAtsModel model;
    model.header.schemaVersion = ats::kModernAtsSchemaVersion;
    model.header.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
    model.header.trackerMode = ats::ModernAtsTrackerMode::ModernAssignment;
    model.header.sampleRate = 48000;
    model.header.hopSizeSamples = 64;
    model.header.windowSizeSamples = 256;
    model.header.parameterHash = id + "-params";
    model.header.sourceHash = id;

    for (int frameIndex = 0; frameIndex < 6; ++frameIndex)
    {
        ats::ModernAtsFrame frame;
        frame.timeSeconds = static_cast<double>(frameIndex * 64) / 48000.0;
        frame.activePartialIds = { 0 };
        frame.orphanResidualBandEnergy.assign(ats::kModernAtsDefaultResidualBands, 0.0);
        frame.orphanResidualBandEnergy[static_cast<std::size_t>(frameIndex % ats::kModernAtsDefaultResidualBands)] =
            0.01;
        model.frames.push_back(frame);
    }

    ats::ModernAtsPartial partial;
    partial.id = 0;
    partial.label = "steady";
    for (int frameIndex = 0; frameIndex < 6; ++frameIndex)
    {
        partial.frames.push_back({ 0.1,
                                   frequencyHz + static_cast<double>(frameIndex),
                                   0.0,
                                   55.0,
                                   0.02,
                                   1.0,
                                   frameIndex == 0 ? ats::ModernAtsFrameReason::Born
                                                   : ats::ModernAtsFrameReason::Matched });
    }
    model.partials.push_back(partial);
    return model;
}

ats::ModernAtsModel makeTransientModel(bool protectedAttack)
{
    auto model = makeSustainedModel(protectedAttack ? "protected-attack" : "baseline-attack", 220.0);
    if (!protectedAttack)
        return model;

    model.transients.push_back({ 1, 0, 64.0 / 48000.0, 0.0, 0.02, 1.0 });
    ats::ModernAtsTransientBuffer buffer;
    buffer.markerIndex = 0;
    buffer.sampleRate = 48000;
    buffer.gain = 1.0;
    buffer.samples = { 0.0f, 0.1f, 0.4f, 0.8f, 0.4f, 0.1f, 0.0f };
    model.transientBuffers.push_back(buffer);
    return model;
}

std::vector<float> sineFixture()
{
    std::vector<float> samples(512, 0.0f);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979323846
                                                        * static_cast<double>(i) / 64.0));
    return samples;
}

std::vector<double> directSourceFixture(int sampleRate)
{
    std::vector<double> samples(static_cast<std::size_t>(sampleRate), 0.0);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        samples[i] = 0.34 * std::sin(2.0 * 3.14159265358979323846 * 512.0 * t)
                     + 0.21 * std::sin(2.0 * 3.14159265358979323846 * 768.0 * t + 0.4);
    }
    return samples;
}

int bestSustainedMatchCount(const ats::ModernAtsModel& model, double frequencyHz, double toleranceHz)
{
    int best = 0;
    for (const auto& partial : model.partials)
    {
        int matches = 0;
        for (const auto& frame : partial.frames)
        {
            if (frameIsActive(frame) && std::abs(frame.frequencyHz - frequencyHz) <= toleranceHz)
                ++matches;
        }
        best = std::max(best, matches);
    }
    return best;
}

ats::MorphRecipe makeRecipe()
{
    ats::MorphRecipe recipe;
    recipe.recipeId = "acceptance-morph";
    recipe.sourceModelId = "source";
    recipe.targetModelId = "target";
    recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::Automatic;
    recipe.envelopes = ats::makeDefaultMorphEnvelopeSet();
    return recipe;
}

void testMetricHelpers()
{
    const auto reference = sineFixture();
    auto candidate = reference;
    candidate[10] += 0.0001f;
    const auto audio = ats::computeModernAtsAudioMetrics(reference, candidate);
    expect(audio.referenceRms > 0.0, "audio metrics should measure reference RMS");
    expect(audio.normalizedRmsError > 0.0, "audio metrics should catch small render error");
    expect(audio.spectralDistanceDb >= 0.0, "audio metrics should report non-negative spectral distance");

    const auto model = makeTransientModel(true);
    const auto continuity = ats::computeModernAtsContinuityMetrics(model);
    expect(continuity.partialCount == 1, "continuity partial count mismatch");
    expect(continuity.continuityScore > 0.9, "continuity score should be high for sustained fixture");
    const auto residual = ats::computeModernAtsResidualMetrics(model);
    expect(residual.totalResidualEnergy > residual.orphanResidualEnergy,
           "residual metrics should include partial and orphan energy");
    const auto transient = ats::computeModernAtsTransientMetrics(model);
    expect(transient.markerCount == 1 && transient.bufferCount == 1,
           "transient metrics should count markers and buffers");
    expect(transient.attackSharpness > 0.0, "transient metrics should measure attack sharpness");
}

void testAcceptanceReportPassesRepresentativeFixtures()
{
    const std::vector<ats::TrackingPeakFrame> crossing = {
        { peak(0, 400.0), peak(1, 700.0) },
        { peak(0, 500.0), peak(1, 600.0) },
        { peak(0, 500.0), peak(1, 600.0) },
    };

    ats::AssignmentTrackerConfig classicConfig;
    classicConfig.analysisMode = ats::ModernAtsAnalysisMode::ClassicAts;
    classicConfig.useFrequencySlopeContinuity = false;
    const auto classicCrossing =
        modelFromTrackerResult(ats::runAssignmentTrackerPrototype(crossing, classicConfig),
                               ats::ModernAtsAnalysisMode::ClassicAts);

    ats::AssignmentTrackerConfig modernConfig;
    modernConfig.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
    modernConfig.useFrequencySlopeContinuity = true;
    const auto modernCrossing =
        modelFromTrackerResult(ats::runAssignmentTrackerPrototype(crossing, modernConfig),
                               ats::ModernAtsAnalysisMode::ModernAts);

    const std::vector<ats::ModernAtsTrackerReference> crossingReference = {
        { 0, { 400.0, 500.0, 600.0 }, 1.0 },
        { 1, { 700.0, 600.0, 500.0 }, 1.0 },
    };

    const auto classicSustained = makeSustainedModel("classic-sustained", 220.0);
    const auto modernSustained = makeSustainedModel("modern-sustained", 220.0);
    const std::vector<ats::ModernAtsTrackerReference> sustainedReference = {
        { 0, { 220.0, 221.0, 222.0, 223.0, 224.0, 225.0 }, 1.0 },
    };

    const auto source = makeSustainedModel("source", 160.0);
    const auto target = makeSustainedModel("target", 240.0);
    const auto recipe = makeRecipe();
    const auto recipeA = ats::serializeMorphRecipeToJson(recipe);
    const auto recipeB = ats::serializeMorphRecipeToJson(recipe);
    const auto morphed = ats::morphModernAtsModels(source, target, recipe, 0.5);

    ats::ModernAtsSynthConfig synthConfig;
    synthConfig.outputSampleRate = 48000;
    synthConfig.hopSizeSamples = 64;
    synthConfig.deterministicNoiseSeed = 77u;
    ats::ModernAtsRenderStats renderStats;
    const auto renderedA = ats::renderModernAtsModel(morphed, synthConfig, &renderStats);
    const auto renderedB = ats::renderModernAtsModel(morphed, synthConfig);

    ats::ModernAtsRenderPlan plan;
    std::string planError;
    expect(ats::prepareModernAtsRenderPlan(morphed, synthConfig, plan, planError), planError.c_str());

    ats::ModernAtsAcceptanceInputs inputs;
    const auto classicAudio = sineFixture();
    inputs.classicParityAudio = ats::computeModernAtsAudioMetrics(classicAudio, classicAudio);
    inputs.classicCrossingTracker =
        ats::computeModernAtsTrackerFixtureMetrics(classicCrossing, "crossing-classic", crossingReference);
    inputs.modernCrossingTracker =
        ats::computeModernAtsTrackerFixtureMetrics(modernCrossing, "crossing-modern", crossingReference);
    inputs.classicSustainedTracker =
        ats::computeModernAtsTrackerFixtureMetrics(classicSustained, "sustained-classic", sustainedReference);
    inputs.modernSustainedTracker =
        ats::computeModernAtsTrackerFixtureMetrics(modernSustained, "sustained-modern", sustainedReference);
    inputs.baselineTransient = ats::computeModernAtsTransientMetrics(makeTransientModel(false));
    inputs.protectedTransient = ats::computeModernAtsTransientMetrics(makeTransientModel(true));
    inputs.residualMetrics = ats::computeModernAtsResidualMetrics(modernSustained);
    inputs.expectedResidualEnergy = inputs.residualMetrics.totalResidualEnergy;
    inputs.morphRecipeRecallDeterministic = recipeA == recipeB;
    inputs.morphRenderDeterministic = renderedA == renderedB;
    inputs.renderSamplesFinite = !renderedA.empty()
                                 && !ats::hasDenormalOrNonFiniteSamples(renderedA.data(),
                                                                        static_cast<int>(renderedA.size()));
    inputs.realtimeSafety = ats::checkModernAtsRealtimeSafety(plan);
    inputs.renderStats = renderStats;

    ats::ModernAtsAcceptanceThresholds thresholds;
    thresholds.maxRenderCpuRatio = 10.0;
    const auto report = ats::evaluateModernAtsAcceptanceGates(inputs, thresholds);
    for (const auto& failure : report.failureMessages())
        std::cerr << failure << '\n';
    expect(report.passed(), "representative modern ATS acceptance report should pass");
    expect(report.checks.size() >= 10, "acceptance report should include all quality gates");
    expect(inputs.modernCrossingTracker.identityErrors < inputs.classicCrossingTracker.identityErrors,
           "modern crossing fixture should improve identity errors");
}

void testAcceptanceRunsDirectModernSourceAnalysis()
{
    const int sampleRate = 4096;
    const auto samples = directSourceFixture(sampleRate);

    ats::AnalysisParams params;
    params.win_size = 256;
    params.hop_size = 128.0;
    params.lowest_freq = 100.0;
    params.highest_freq = 1400.0;
    params.lowest_mag = -85.0;
    params.min_seg_len = 2;

    ats::ModernAtsAnalysisConfig config;
    config.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
    config.trackerMode = ats::ModernAtsTrackerMode::ModernAssignment;
    config.maxPeaksPerFrame = 12;
    config.maxCandidatesPerTrack = 6;

    const auto result = ats::runModernAtsAnalysis(params, samples, sampleRate, config);
    expect(result.errorMessage.empty(), result.errorMessage.c_str());

    std::string validationError;
    expect(ats::validateModernAtsModel(result.model, validationError), validationError.c_str());
    expect(!result.model.partials.empty(), "direct source acceptance should produce partials");
    expect(!result.model.assignmentCosts.empty(), "direct source acceptance should record assignment costs");

    const int minSustainedFrames = 20;
    expect(bestSustainedMatchCount(result.model, 512.0, 10.0) >= minSustainedFrames,
           "direct source acceptance should sustain the 512 Hz partial");
    expect(bestSustainedMatchCount(result.model, 768.0, 10.0) >= minSustainedFrames,
           "direct source acceptance should sustain the 768 Hz partial");

    const auto continuity = ats::computeModernAtsContinuityMetrics(result.model);
    expect(continuity.continuityScore > 0.60, "direct source acceptance continuity regressed");

    ats::ModernAtsSynthConfig synthConfig;
    synthConfig.outputSampleRate = sampleRate;
    synthConfig.hopSizeSamples = static_cast<int>(params.hop_size);
    synthConfig.orphanResidualGain = 0.0;
    synthConfig.transientGain = 0.0;
    ats::ModernAtsRenderStats stats;
    const auto rendered = ats::renderModernAtsModel(result.model, synthConfig, &stats);
    expect(!rendered.empty(), "direct source acceptance render should not be empty");
    expect(!ats::hasDenormalOrNonFiniteSamples(rendered.data(), static_cast<int>(rendered.size())),
           "direct source acceptance render should be finite");
    expect(stats.peakAbs > 0.01f, "direct source acceptance render should preserve audible energy");
}

void testAcceptanceReportFlagsFailures()
{
    ats::ModernAtsAcceptanceInputs inputs;
    inputs.classicParityAudio.normalizedRmsError = 1.0;
    inputs.classicParityAudio.spectralDistanceDb = 20.0;
    inputs.classicCrossingTracker.identityErrors = 0;
    inputs.modernCrossingTracker.identityErrors = 3;
    inputs.expectedResidualEnergy = 1.0;
    inputs.residualMetrics.totalResidualEnergy = 0.2;
    inputs.renderStats.cpuRatioAtRealtime = 99.0;

    const auto report = ats::evaluateModernAtsAcceptanceGates(inputs);
    expect(!report.passed(), "failing metrics should fail acceptance report");
    expect(!report.failureMessages().empty(), "failing report should explain failures");
}

void testEmptyAcceptanceReportPasses()
{
    ats::ModernAtsAcceptanceReport report;
    expect(report.passed(), "empty acceptance report should default to passing");
    expect(report.failureMessages().empty(), "empty acceptance report should have no failures");
}

} // namespace

int main()
{
    testMetricHelpers();
    testAcceptanceReportPassesRepresentativeFixtures();
    testAcceptanceRunsDirectModernSourceAnalysis();
    testAcceptanceReportFlagsFailures();
    testEmptyAcceptanceReportPasses();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

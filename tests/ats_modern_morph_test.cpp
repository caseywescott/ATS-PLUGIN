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

ats::ModernAtsModel makeModel(const std::string& id,
                              double f0,
                              int partialCount,
                              int frameCount,
                              double ampScale = 1.0)
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

    std::vector<int> activeIds;
    for (int partialId = 0; partialId < partialCount; ++partialId)
        activeIds.push_back(partialId);

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        ats::ModernAtsFrame frame;
        frame.timeSeconds = (double)(frameIndex * model.header.hopSizeSamples) / 48000.0;
        frame.activePartialIds = activeIds;
        frame.orphanResidualBandEnergy.assign(ats::kModernAtsDefaultResidualBands, 0.0);
        frame.orphanResidualBandEnergy[(std::size_t)(frameIndex % ats::kModernAtsDefaultResidualBands)] =
            0.001 * ampScale;
        model.frames.push_back(frame);
    }

    for (int partialId = 0; partialId < partialCount; ++partialId)
    {
        ats::ModernAtsPartial partial;
        partial.id = partialId;
        partial.label = "h" + std::to_string(partialId + 1);
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            partial.frames.push_back({ 0.1 * ampScale,
                                       f0 * (double)(partialId + 1) + 2.0 * (double)frameIndex,
                                       0.1 * (double)partialId,
                                       50.0 + (double)partialId,
                                       0.0001 * (double)(partialId + 1) * ampScale,
                                       1.0,
                                       frameIndex == 0 ? ats::ModernAtsFrameReason::Born
                                                       : ats::ModernAtsFrameReason::Matched });
        }
        model.partials.push_back(partial);
    }

    return model;
}

ats::MorphRecipe makeRecipe()
{
    ats::MorphRecipe recipe;
    recipe.recipeId = "recipe-a";
    recipe.sourceModelId = "source-a";
    recipe.targetModelId = "target-b";
    recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::Explicit;
    recipe.envelopes = ats::makeDefaultMorphEnvelopeSet();
    return recipe;
}

float peakAbs(const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (const float sample : samples)
        peak = std::max(peak, std::abs(sample));
    return peak;
}

void testRecipeSerialization()
{
    auto recipe = makeRecipe();
    recipe.correspondences.push_back(
        { 1, 2, "lead", "src-lead", "dst-lead", ats::MorphUnmatchedPolicy::FadeOut,
          ats::MorphUnmatchedPolicy::FadeIn });
    std::string error;
    expect(ats::validateMorphRecipe(recipe, error), error.c_str());

    const auto jsonA = ats::serializeMorphRecipeToJson(recipe);
    const auto jsonB = ats::serializeMorphRecipeToJson(recipe);
    expect(jsonA == jsonB, "morph recipe serialization should be deterministic");
    expect(jsonA.find("\"recipe_id\":\"recipe-a\"") != std::string::npos, "recipe id missing");
    expect(jsonA.find("\"source_model_id\":\"source-a\"") != std::string::npos, "source id missing");
    expect(jsonA.find("\"correspondence_strategy\":\"explicit\"") != std::string::npos,
           "strategy missing");
    expect(jsonA.find("\"source_partial_id\":1") != std::string::npos, "source correspondence missing");

    recipe.recipeId = "recipe\bcontrol";
    const auto escaped = ats::serializeMorphRecipeToJson(recipe);
    expect(escaped.find("recipe\\bcontrol") != std::string::npos,
           "morph recipe JSON should escape control characters");
}

void testHarmonicCorrespondence()
{
    const auto source = makeModel("source-h", 100.0, 3, 3);
    const auto target = makeModel("target-h", 150.0, 3, 3);
    auto recipe = makeRecipe();
    recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::HarmonicNumber;
    recipe.sourceFundamentalHz = 100.0;
    recipe.targetFundamentalHz = 150.0;
    recipe.sourceHarmonicity = 0.95;
    recipe.targetHarmonicity = 0.95;

    const auto c = ats::buildAutomaticCorrespondence(source, target, recipe);
    expect(c.size() == 3, "harmonic correspondence should pair all three harmonics");
    expect(c[0].label == "h1", "first harmonic correspondence label mismatch");
    expect(c[1].sourcePartialId == 1 && c[1].targetPartialId == 1, "second harmonic pair mismatch");
}

void testHarmonicCorrespondenceRejectsDuplicateTargets()
{
    auto source = makeModel("source-dup-h", 100.0, 2, 3);
    const auto target = makeModel("target-dup-h", 100.0, 1, 3);
    for (auto& frame : source.partials[1].frames)
        frame.frequencyHz = 105.0;

    auto recipe = makeRecipe();
    recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::HarmonicNumber;
    recipe.sourceFundamentalHz = 100.0;
    recipe.targetFundamentalHz = 100.0;

    const auto c = ats::buildAutomaticCorrespondence(source, target, recipe);
    int matched = 0;
    int unmatchedSource = 0;
    int reusedTarget = 0;
    for (const auto& item : c)
    {
        if (item.sourcePartialId >= 0 && item.targetPartialId >= 0)
        {
            ++matched;
            if (item.targetPartialId == 0)
                ++reusedTarget;
        }
        else if (item.sourcePartialId >= 0)
        {
            ++unmatchedSource;
        }
    }

    expect(matched == 1, "harmonic correspondence should select one source for a single target");
    expect(reusedTarget == 1, "harmonic correspondence should not reuse target partials");
    expect(unmatchedSource == 1, "duplicate source harmonic should remain unmatched");

    recipe.sourceFundamentalHz = 0.0;
    const auto invalidF0 = ats::buildAutomaticCorrespondence(source, target, recipe);
    bool matchedInvalidF0 = false;
    for (const auto& item : invalidF0)
        matchedInvalidF0 = matchedInvalidF0 || (item.sourcePartialId >= 0 && item.targetPartialId >= 0);
    expect(!matchedInvalidF0, "invalid harmonic fundamentals should not create harmonic matches");
}

void testNearestAndFallbackCorrespondence()
{
    auto source = makeModel("source-n", 100.0, 2, 3);
    auto target = makeModel("target-n", 100.0, 2, 3);
    source.partials[0].frames[0].frequencyHz = 440.0;
    source.partials[1].frames[0].frequencyHz = 700.0;
    target.partials[0].frames[0].frequencyHz = 444.0;
    target.partials[1].frames[0].frequencyHz = 980.0;

    auto recipe = makeRecipe();
    recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::Automatic;
    recipe.sourceHarmonicity = 0.2;
    recipe.targetHarmonicity = 0.2;
    recipe.maxNearestCents = 40.0;

    const auto c = ats::buildAutomaticCorrespondence(source, target, recipe);
    bool pairedNearest = false;
    bool sawUnmatchedSource = false;
    bool sawUnmatchedTarget = false;
    for (const auto& item : c)
    {
        pairedNearest = pairedNearest || (item.sourcePartialId == 0 && item.targetPartialId == 0);
        sawUnmatchedSource = sawUnmatchedSource || (item.sourcePartialId == 1 && item.targetPartialId < 0);
        sawUnmatchedTarget = sawUnmatchedTarget || (item.sourcePartialId < 0 && item.targetPartialId == 1);
    }

    expect(pairedNearest, "nearest strategy should pair close partials");
    expect(sawUnmatchedSource, "nearest fallback should retain unmatched source partial");
    expect(sawUnmatchedTarget, "nearest fallback should retain unmatched target partial");
}

void testMorphInterpolationAndPolicies()
{
    const auto source = makeModel("source-i", 100.0, 2, 4, 1.0);
    const auto target = makeModel("target-i", 200.0, 2, 4, 2.0);
    auto recipe = makeRecipe();
    recipe.correspondences.push_back({ 0, 0, "paired", "s0", "t0", ats::MorphUnmatchedPolicy::FadeOut,
                                       ats::MorphUnmatchedPolicy::FadeIn });
    recipe.correspondences.push_back({ 1, -1, "source-only", "s1", "", ats::MorphUnmatchedPolicy::FadeOut,
                                       ats::MorphUnmatchedPolicy::FadeIn });
    recipe.correspondences.push_back({ -1, 1, "target-only", "", "t1", ats::MorphUnmatchedPolicy::FadeOut,
                                       ats::MorphUnmatchedPolicy::FadeIn });

    ats::MorphFixtureMetrics metrics;
    const auto morphed = ats::morphModernAtsModels(source, target, recipe, 0.5, &metrics);
    std::string error;
    expect(ats::validateModernAtsModel(morphed, error), error.c_str());
    expect(metrics.outputPartials == 3, "morph should produce paired plus unmatched tracks");
    expect(std::abs(morphed.partials[0].frames[0].frequencyHz - 150.0) < 1.0e-9,
           "frequency envelope interpolation mismatch");
    expect(std::abs(morphed.partials[0].frames[0].amplitude - 0.15) < 1.0e-9,
           "amplitude envelope interpolation mismatch");
    expect(std::abs(morphed.partials[0].frames[0].noiseEnergy - 0.00015) < 1.0e-12,
           "partial noise interpolation mismatch");
    expect(std::abs(morphed.frames[0].orphanResidualBandEnergy[0] - 0.0015) < 1.0e-12,
           "orphan residual interpolation mismatch");
    expect(std::abs(morphed.partials[1].frames[0].amplitude - 0.05) < 1.0e-9,
           "source unmatched fade-out mismatch");
    expect(std::abs(morphed.partials[2].frames[0].amplitude - 0.1) < 1.0e-9,
           "target unmatched fade-in mismatch");
}

void testTransientAwareMorphAndRender()
{
    auto source = makeModel("source-t", 120.0, 1, 4, 1.0);
    auto target = makeModel("target-t", 180.0, 1, 4, 1.0);
    source.transients.push_back({ 0, 0, 0.0, 0.0, 0.02, 1.0 });
    target.transients.push_back({ 2, 0, 0.08, 0.075, 0.11, 0.5 });
    ats::ModernAtsTransientBuffer sourceBuffer;
    sourceBuffer.markerIndex = 0;
    sourceBuffer.sampleRate = 48000;
    sourceBuffer.gain = 1.0;
    sourceBuffer.samples.assign(32, 0.2f);
    ats::ModernAtsTransientBuffer targetBuffer;
    targetBuffer.markerIndex = 0;
    targetBuffer.sampleRate = 48000;
    targetBuffer.gain = 0.5;
    targetBuffer.samples.assign(32, 0.1f);
    source.transientBuffers.push_back(sourceBuffer);
    target.transientBuffers.push_back(targetBuffer);

    auto recipe = makeRecipe();
    recipe.transientMode = ats::MorphTransientMode::BlendBoth;
    recipe.correspondences.push_back({ 0, 0, "paired", "s0", "t0", ats::MorphUnmatchedPolicy::FadeOut,
                                       ats::MorphUnmatchedPolicy::FadeIn });
    recipe.envelopes.transientTiming = ats::makeLinearMorphEnvelope("transient_timing", 0.0, 1.0);
    recipe.envelopes.transientSourceMix = ats::makeConstantMorphEnvelope("transient_source_mix", 0.25);
    recipe.envelopes.transientGain = ats::makeConstantMorphEnvelope("transient_gain", 0.8);

    ats::MorphFixtureMetrics metrics;
    const auto morphed = ats::morphModernAtsModels(source, target, recipe, 0.5, &metrics);
    expect(morphed.transients.size() == 1, "transient-aware morph should produce one marker");
    expect(std::abs(morphed.transients[0].onsetSeconds - 0.04) < 1.0e-9,
           "transient timing interpolation mismatch");
    expect(morphed.transients[0].windowEndSeconds > morphed.transients[0].onsetSeconds,
           "protected transient window should not collapse");
    expect(metrics.attackWindowSeconds > 0.0, "transient fixture metrics missing attack window");

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 64;
    config.orphanResidualGain = 0.0;
    const auto renderedA = ats::renderModernAtsModel(morphed, config);
    const auto renderedB = ats::renderModernAtsModel(morphed, config);
    expect(renderedA == renderedB, "morphed render should be deterministic");
    expect(peakAbs(renderedA) > 0.0f, "morphed render should be non-zero");
    expect(!ats::hasDenormalOrNonFiniteSamples(renderedA.data(), (int)renderedA.size()),
           "morphed render produced denormal/non-finite samples");
}

void testAutomationRenderContinuity()
{
    const auto source = makeModel("source-a", 100.0, 2, 8, 0.8);
    const auto target = makeModel("target-a", 160.0, 2, 8, 0.9);
    auto recipe = makeRecipe();
    recipe.correspondences = ats::buildAutomaticCorrespondence(source, target, recipe);

    ats::ModernAtsSynthConfig config;
    config.outputSampleRate = 48000;
    config.hopSizeSamples = 64;
    config.orphanResidualGain = 0.1;
    config.deterministicNoiseSeed = 77u;

    for (double position : { 0.25, 0.5, 0.75 })
    {
        const auto morphed = ats::morphModernAtsModels(source, target, recipe, position);
        const auto rendered = ats::renderModernAtsModel(morphed, config);
        float maxStep = 0.0f;
        for (std::size_t i = 1; i < rendered.size(); ++i)
            maxStep = std::max(maxStep, std::abs(rendered[i] - rendered[i - 1]));
        expect(maxStep < 0.25f, "morphed automation render has a click-sized discontinuity");
    }
}

} // namespace

int main()
{
    testRecipeSerialization();
    testHarmonicCorrespondence();
    testHarmonicCorrespondenceRejectsDuplicateTargets();
    testNearestAndFallbackCorrespondence();
    testMorphInterpolationAndPolicies();
    testTransientAwareMorphAndRender();
    testAutomationRenderContinuity();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

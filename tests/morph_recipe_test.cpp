#include "AtsMorphRecipe.h"

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

ats::MorphSourceMetadata makeSource(const std::string& id, const std::string& hash, const std::string& name)
{
    ats::MorphSourceMetadata source;
    source.id = id;
    source.hash = hash;
    source.name = name;
    source.sampleRate = 48000.0;
    source.durationSeconds = 1.5;
    source.frameHopSeconds = 0.01;
    source.lowestFrequencyHz = 40.0;
    source.highestFrequencyHz = 18000.0;
    source.channelCount = 1;
    source.frameCount = 128;
    source.partialCount = 64;
    source.hasPhase = true;
    source.hasResidual = true;
    return source;
}

bool approxEqual(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

double matchedLogCostSum(const std::vector<ats::MorphCorrespondenceRecord>& records)
{
    double sum = 0.0;
    for (const auto& record : records)
    {
        if (record.kind == ats::MorphCorrespondenceKind::Matched)
            sum += std::abs(std::log(record.targetFrequencyHz / record.sourceFrequencyHz));
    }
    return sum;
}

ats::MorphCorrespondenceRecord makeMatchedEnergyRecord(int bandIndex,
                                                       int rank,
                                                       double sourceAmplitude,
                                                       double targetAmplitude)
{
    ats::MorphCorrespondenceRecord record;
    record.bandIndex = bandIndex;
    record.rank = rank;
    record.sourcePartialIndex = rank;
    record.sourceFrameIndex = 0;
    record.sourceFrequencyHz = 100.0 + rank;
    record.sourceAmplitude = sourceAmplitude;
    record.targetPartialIndex = 100 + rank;
    record.targetFrameIndex = 0;
    record.targetFrequencyHz = 100.0 + rank;
    record.targetAmplitude = targetAmplitude;
    record.kind = ats::MorphCorrespondenceKind::Matched;
    return record;
}

ats::MorphCorrespondenceRecord makeUnmatchedSourceEnergyRecord(int bandIndex, int rank, double amplitude)
{
    ats::MorphCorrespondenceRecord record;
    record.bandIndex = bandIndex;
    record.rank = rank;
    record.sourcePartialIndex = rank;
    record.sourceFrameIndex = 0;
    record.sourceFrequencyHz = 100.0 + rank;
    record.sourceAmplitude = amplitude;
    record.kind = ats::MorphCorrespondenceKind::UnmatchedSource;
    return record;
}

ats::MorphCorrespondenceRecord makeUnmatchedTargetEnergyRecord(int bandIndex, int rank, double amplitude)
{
    ats::MorphCorrespondenceRecord record;
    record.bandIndex = bandIndex;
    record.rank = rank;
    record.targetPartialIndex = 100 + rank;
    record.targetFrameIndex = 0;
    record.targetFrequencyHz = 100.0 + rank;
    record.targetAmplitude = amplitude;
    record.kind = ats::MorphCorrespondenceKind::UnmatchedTarget;
    return record;
}

} // namespace

int main()
{
    ats::MorphRecipe recipe = ats::makeDefaultMorphRecipe();
    recipe.engineVersion = "test-engine=1\\roundtrip";
    recipe.frequencyGrid = ats::makeBarkFrequencyGrid(25, 20.0, 20000.0);
    recipe.defaultMorphAmount = 0.35;
    recipe.sourceA = makeSource("source-a", "sha256:a", "A steady harmonic source");
    recipe.sourceB = makeSource("source-b", "sha256:b", "B vowel\nsource");
    recipe.sourceB.partialCount = 72;
    recipe.sourceB.hasResidual = false;
    recipe.outputGains.partialGain = 0.5;
    recipe.outputGains.partialNoiseGain = 0.25;
    recipe.outputGains.orphanResidualGain = 2.0;
    if (recipe.phase.strategy != ats::MorphPhaseStrategy::OscillatorContinuation)
        return fail("default phase strategy should use renderer oscillator continuation");

    ats::MorphPhaseInput matchedPhase;
    matchedPhase.kind = ats::MorphCorrespondenceKind::Matched;
    matchedPhase.sourcePhaseRadians = 0.25;
    matchedPhase.targetPhaseRadians = 1.25;
    matchedPhase.hasSourcePhase = true;
    matchedPhase.hasTargetPhase = true;
    const auto defaultPhase = ats::resolveMorphPhase(matchedPhase, 0.5, recipe.phase);
    if (!defaultPhase.valid || !defaultPhase.usesRendererContinuation
        || defaultPhase.usedSourcePhase || defaultPhase.usedTargetPhase || defaultPhase.interpolatedPhase)
    {
        return fail("default phase strategy should defer to renderer oscillator continuation");
    }
    recipe.phase.strategy = ats::MorphPhaseStrategy::DirectBlend;
    recipe.lifecycle.birthFadeFrames = 8;
    recipe.lifecycle.deathFadeFrames = 6;
    recipe.lifecycle.maxGainStepPerFrame = 0.2;
    recipe.lifecycle.curve = ats::MorphCurveShape::EaseInOut;
    recipe.transient.policy = ats::MorphTransientPolicy::Crossfade;
    recipe.transient.protectionWindowSeconds = 0.125;
    recipe.transient.maxOnsetAlignmentOffsetSeconds = 0.03125;
    recipe.sourceATransients = {
        { 0.0, 1.0, 0 },
        { 0.25, 0.75, 24 },
    };
    recipe.sourceBTransients = {
        { 0.0, 0.9, 0 },
        { 0.3125, 0.6, 30 },
    };

    const auto defaultLaneAmounts = ats::evaluateMorphLaneAmounts(recipe, recipe.defaultMorphAmount);
    for (const auto amount : defaultLaneAmounts)
    {
        if (!approxEqual(amount, recipe.defaultMorphAmount, 1.0e-12))
            return fail("default linked linear morph lanes should follow the main morph amount");
    }

    recipe.lanes[static_cast<size_t>(ats::MorphLane::Frequency)].linkedToMainMorph = true;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Frequency)].amount = 0.25;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Frequency)].curve = ats::MorphCurveShape::EaseOut;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Amplitude)].linkedToMainMorph = false;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Amplitude)].amount = 0.7;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Amplitude)].curve = ats::MorphCurveShape::EaseIn;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::ResidualNoise)].linkedToMainMorph = false;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::ResidualNoise)].amount = 0.5;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Time)].amount = 0.1;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Transient)].amount = 0.0;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Phase)].linkedToMainMorph = false;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Phase)].amount = 1.0;
    recipe.lanes[static_cast<size_t>(ats::MorphLane::Phase)].curve = ats::MorphCurveShape::EaseInOut;

    const auto laneAmounts = ats::evaluateMorphLaneAmounts(recipe, recipe.defaultMorphAmount);
    if (!approxEqual(laneAmounts[static_cast<size_t>(ats::MorphLane::Frequency)], 0.5775, 1.0e-12)
        || !approxEqual(laneAmounts[static_cast<size_t>(ats::MorphLane::Amplitude)], 0.49, 1.0e-12)
        || !approxEqual(laneAmounts[static_cast<size_t>(ats::MorphLane::ResidualNoise)], 0.5, 1.0e-12)
        || !approxEqual(laneAmounts[static_cast<size_t>(ats::MorphLane::Time)], 0.35, 1.0e-12)
        || !approxEqual(laneAmounts[static_cast<size_t>(ats::MorphLane::Phase)], 1.0, 1.0e-12))
    {
        return fail("independent morph lane curves did not evaluate linked and unlinked lanes correctly");
    }
    if (ats::applyMorphCurve(-1.0, ats::MorphCurveShape::EaseInOut) != 0.0
        || ats::applyMorphCurve(2.0, ats::MorphCurveShape::EaseInOut) != 1.0)
    {
        return fail("morph lane curve evaluation should clamp automation amounts");
    }

    const std::string serialized = ats::serializeMorphRecipe(recipe);
    if (serialized.find("ATS_MORPH_RECIPE 1\n") != 0)
        return fail("serialized recipe missing expected header");
    if (serialized.find("source_a.id=source-a\n") == std::string::npos)
        return fail("serialized recipe missing source A metadata");
    if (serialized.find("lane.frequency.amount=0.25\n") == std::string::npos)
        return fail("serialized recipe missing lane defaults");
    if (serialized.find("lane.frequency.curve=ease_out\n") == std::string::npos
        || serialized.find("lane.amplitude.curve=ease_in\n") == std::string::npos
        || serialized.find("lane.phase.curve=ease_in_out\n") == std::string::npos)
    {
        return fail("serialized recipe missing independent lane curve metadata");
    }
    if (serialized.find("grid.type=bark\n") == std::string::npos
        || serialized.find("grid.band_count=25\n") == std::string::npos)
    {
        return fail("serialized recipe missing Bark grid metadata");
    }
    if (serialized.find("unmatched.source_policy=fade_out\n") == std::string::npos
        || serialized.find("unmatched.target_policy=fade_in\n") == std::string::npos
        || serialized.find("unmatched.discard_opt_in=0\n") == std::string::npos)
    {
        return fail("serialized recipe missing default unmatched partial policy metadata");
    }
    if (serialized.find("gain.partial=0.5\n") == std::string::npos
        || serialized.find("gain.partial_noise=0.25\n") == std::string::npos
        || serialized.find("gain.orphan_residual=2\n") == std::string::npos)
    {
        return fail("serialized recipe missing independent output gain metadata");
    }
    if (serialized.find("phase.strategy=direct_blend\n") == std::string::npos)
        return fail("serialized recipe missing phase strategy metadata");
    if (serialized.find("lifecycle.birth_fade_frames=8\n") == std::string::npos
        || serialized.find("lifecycle.death_fade_frames=6\n") == std::string::npos
        || serialized.find("lifecycle.max_gain_step_per_frame=0.20000000000000001\n") == std::string::npos
        || serialized.find("lifecycle.curve=ease_in_out\n") == std::string::npos)
    {
        return fail("serialized recipe missing lifecycle fade metadata");
    }
    if (serialized.find("transient.policy=crossfade\n") == std::string::npos
        || serialized.find("transient.protection_window_seconds=0.125\n") == std::string::npos
        || serialized.find("transient.max_onset_alignment_offset_seconds=0.03125\n") == std::string::npos
        || serialized.find("source_a.transient.count=2\n") == std::string::npos
        || serialized.find("source_a.transient.1.time_seconds=0.25\n") == std::string::npos
        || serialized.find("source_b.transient.1.time_seconds=0.3125\n") == std::string::npos)
    {
        return fail("serialized recipe missing transient policy and marker metadata");
    }

    ats::MorphRecipe parsed;
    std::string error;
    if (!ats::deserializeMorphRecipe(serialized, parsed, &error))
        return fail("failed to deserialize recipe: " + error);
    if (parsed != recipe)
        return fail("recipe did not survive serialize/deserialize round trip");

    const std::string futureSerialized = serialized + "future.policy=keep-me\\eplease\n";
    if (!ats::deserializeMorphRecipe(futureSerialized, parsed, &error))
        return fail("failed to deserialize recipe with future field: " + error);
    if (parsed.unknownFields.size() != 1 || parsed.unknownFields[0].key != "future.policy"
        || parsed.unknownFields[0].value != "keep-me=please")
    {
        return fail("future recipe fields were not preserved");
    }
    const std::string preservedFutureSerialized = ats::serializeMorphRecipe(parsed);
    if (preservedFutureSerialized.find("future.policy=keep-me\\eplease\n") == std::string::npos)
        return fail("future recipe field was not reserialized");

    ats::MorphLane lane = ats::MorphLane::Count;
    if (!ats::morphLaneFromName("residual_noise", lane) || lane != ats::MorphLane::ResidualNoise)
        return fail("lane name lookup failed");
    ats::MorphCurveShape curve = ats::MorphCurveShape::Linear;
    if (!ats::morphCurveShapeFromName("ease_in_out", curve) || curve != ats::MorphCurveShape::EaseInOut
        || std::string(ats::morphCurveShapeName(ats::MorphCurveShape::EaseOut)) != "ease_out")
    {
        return fail("morph lane curve lookup failed");
    }
    ats::MorphPhaseStrategy phaseStrategy = ats::MorphPhaseStrategy::OscillatorContinuation;
    if (!ats::morphPhaseStrategyFromName("oscillator_continuation", phaseStrategy)
        || phaseStrategy != ats::MorphPhaseStrategy::OscillatorContinuation
        || !ats::morphPhaseStrategyFromName("source_a_donor", phaseStrategy)
        || phaseStrategy != ats::MorphPhaseStrategy::SourceADonor
        || !ats::morphPhaseStrategyFromName("source_b_donor", phaseStrategy)
        || phaseStrategy != ats::MorphPhaseStrategy::SourceBDonor
        || !ats::morphPhaseStrategyFromName("relative_cluster_preservation", phaseStrategy)
        || phaseStrategy != ats::MorphPhaseStrategy::RelativeClusterPreservation
        || !ats::morphPhaseStrategyFromName("direct_blend", phaseStrategy)
        || phaseStrategy != ats::MorphPhaseStrategy::DirectBlend)
    {
        return fail("phase strategy lookup failed");
    }
    if (std::string(ats::morphPhaseStrategyName(ats::MorphPhaseStrategy::SourceBDonor)) != "source_b_donor")
        return fail("phase strategy name lookup failed");

    ats::MorphTransientPolicy transientPolicy = ats::MorphTransientPolicy::SustainedOnlyAfterAttack;
    if (!ats::morphTransientPolicyFromName("source_a", transientPolicy)
        || transientPolicy != ats::MorphTransientPolicy::SourceA
        || !ats::morphTransientPolicyFromName("source_b", transientPolicy)
        || transientPolicy != ats::MorphTransientPolicy::SourceB
        || !ats::morphTransientPolicyFromName("crossfade", transientPolicy)
        || transientPolicy != ats::MorphTransientPolicy::Crossfade
        || !ats::morphTransientPolicyFromName("sustained_only_after_attack", transientPolicy)
        || transientPolicy != ats::MorphTransientPolicy::SustainedOnlyAfterAttack)
    {
        return fail("transient policy lookup failed");
    }
    if (std::string(ats::morphTransientPolicyName(ats::MorphTransientPolicy::Crossfade)) != "crossfade")
        return fail("transient policy name lookup failed");

    ats::MorphPhaseSettings directPhaseSettings;
    directPhaseSettings.strategy = ats::MorphPhaseStrategy::DirectBlend;
    const auto phaseStart = ats::resolveMorphPhase(matchedPhase, 0.0, directPhaseSettings);
    const auto phaseMiddle = ats::resolveMorphPhase(matchedPhase, 0.5, directPhaseSettings);
    const auto phaseEnd = ats::resolveMorphPhase(matchedPhase, 1.0, directPhaseSettings);
    if (!phaseStart.valid || !phaseMiddle.valid || !phaseEnd.valid
        || !phaseMiddle.interpolatedPhase
        || !approxEqual(phaseStart.phaseRadians, matchedPhase.sourcePhaseRadians, 1.0e-12)
        || !approxEqual(phaseMiddle.phaseRadians, 0.75, 1.0e-12)
        || !approxEqual(phaseEnd.phaseRadians, matchedPhase.targetPhaseRadians, 1.0e-12))
    {
        return fail("direct phase blend did not preserve endpoints and deterministic midpoint");
    }

    ats::MorphPhaseInput birthPhase;
    birthPhase.kind = ats::MorphCorrespondenceKind::UnmatchedTarget;
    birthPhase.targetPhaseRadians = 2.0;
    birthPhase.hasTargetPhase = true;
    const auto resolvedBirthPhase = ats::resolveMorphPhase(birthPhase, 0.25, directPhaseSettings);
    if (!resolvedBirthPhase.valid || !resolvedBirthPhase.isBirth
        || resolvedBirthPhase.isDeath || !resolvedBirthPhase.usedTargetPhase
        || resolvedBirthPhase.usedSourcePhase
        || !approxEqual(resolvedBirthPhase.phaseRadians, 2.0, 1.0e-12))
    {
        return fail("phase birth handling should deterministically use the available target phase");
    }

    ats::MorphPhaseInput deathPhase;
    deathPhase.kind = ats::MorphCorrespondenceKind::UnmatchedSource;
    deathPhase.sourcePhaseRadians = -1.0;
    deathPhase.hasSourcePhase = true;
    const auto resolvedDeathPhase = ats::resolveMorphPhase(deathPhase, 0.75, directPhaseSettings);
    if (!resolvedDeathPhase.valid || resolvedDeathPhase.isBirth
        || !resolvedDeathPhase.isDeath || !resolvedDeathPhase.usedSourcePhase
        || resolvedDeathPhase.usedTargetPhase
        || !approxEqual(resolvedDeathPhase.phaseRadians, -1.0, 1.0e-12))
    {
        return fail("phase death handling should deterministically use the available source phase");
    }

    ats::MorphLifecycleFadeSettings lifecycleDefaults;
    const auto matchedLifecycle = ats::evaluateMorphLifecycleFade(
        ats::MorphCorrespondenceKind::Matched,
        0,
        lifecycleDefaults);
    if (matchedLifecycle.amplitudeScale != 1.0
        || matchedLifecycle.allowsPhaseReset
        || matchedLifecycle.clickRisk
        || matchedLifecycle.zipperRisk)
    {
        return fail("matched partial lifecycle should stay at unity without reset or transition warnings");
    }

    const auto birthStart = ats::evaluateMorphLifecycleFade(
        ats::MorphCorrespondenceKind::UnmatchedTarget,
        0,
        lifecycleDefaults);
    const auto birthMiddle = ats::evaluateMorphLifecycleFade(
        ats::MorphCorrespondenceKind::UnmatchedTarget,
        2,
        lifecycleDefaults);
    const auto deathStart = ats::evaluateMorphLifecycleFade(
        ats::MorphCorrespondenceKind::UnmatchedSource,
        0,
        lifecycleDefaults);
    const auto deathMiddle = ats::evaluateMorphLifecycleFade(
        ats::MorphCorrespondenceKind::UnmatchedSource,
        2,
        lifecycleDefaults);
    if (!birthStart.isBirth || birthStart.isDeath
        || !birthStart.allowsPhaseReset
        || !approxEqual(birthStart.amplitudeScale, 0.0, 1.0e-12)
        || !approxEqual(birthStart.nextAmplitudeScale, 0.25, 1.0e-12)
        || !approxEqual(birthMiddle.amplitudeScale, 0.5, 1.0e-12)
        || birthMiddle.allowsPhaseReset
        || !deathStart.isDeath || deathStart.isBirth
        || !deathStart.allowsPhaseReset
        || !approxEqual(deathStart.amplitudeScale, 1.0, 1.0e-12)
        || !approxEqual(deathStart.nextAmplitudeScale, 0.75, 1.0e-12)
        || !approxEqual(deathMiddle.amplitudeScale, 0.5, 1.0e-12)
        || deathMiddle.allowsPhaseReset)
    {
        return fail("birth/death lifecycle fades did not ramp deterministically or gate phase resets");
    }

    ats::MorphLifecycleFadeSettings instantLifecycle;
    instantLifecycle.birthFadeFrames = 0;
    const auto instantBirth = ats::evaluateMorphLifecycleFade(
        ats::MorphCorrespondenceKind::UnmatchedTarget,
        0,
        instantLifecycle);
    if (!instantBirth.clickRisk || !instantBirth.zipperRisk || !approxEqual(instantBirth.frameGainStep, 1.0, 1.0e-12))
        return fail("instant lifecycle birth should report click and zipper risk");

    ats::MorphLifecycleFadeSettings zipperLifecycle;
    zipperLifecycle.birthFadeFrames = 1;
    zipperLifecycle.maxGainStepPerFrame = 0.25;
    const auto steepBirth = ats::evaluateMorphLifecycleFade(
        ats::MorphCorrespondenceKind::UnmatchedTarget,
        0,
        zipperLifecycle);
    if (steepBirth.clickRisk || !steepBirth.zipperRisk || !approxEqual(steepBirth.frameGainStep, 1.0, 1.0e-12))
        return fail("too-short lifecycle fade should report zipper risk without being an instant click");

    const auto pluckedPercussiveTransientReport =
        ats::analyzeMorphTransientAlignment(recipe.sourceATransients, recipe.sourceBTransients, recipe.transient);
    if (pluckedPercussiveTransientReport.comparedMarkerCount != 2
        || pluckedPercussiveTransientReport.warningCount != 1
        || pluckedPercussiveTransientReport.warnings.size() != 1
        || pluckedPercussiveTransientReport.warnings[0].sourceMarkerIndex != 1
        || pluckedPercussiveTransientReport.warnings[0].targetMarkerIndex != 1
        || !approxEqual(pluckedPercussiveTransientReport.warnings[0].sourceTimeSeconds, 0.25, 1.0e-12)
        || !approxEqual(pluckedPercussiveTransientReport.warnings[0].targetTimeSeconds, 0.3125, 1.0e-12)
        || !approxEqual(pluckedPercussiveTransientReport.warnings[0].offsetSeconds, 0.0625, 1.0e-12)
        || !pluckedPercussiveTransientReport.warnings[0].exceedsTolerance)
    {
        return fail("plucked/percussive transient fixture should warn on poorly aligned onsets");
    }

    std::vector<ats::MorphTransientMarker> unsortedSourceTransients = {
        { 0.25, 0.75, 24 },
        { 0.0, 1.0, 0 },
    };
    const auto sortedTransientReport =
        ats::analyzeMorphTransientAlignment(unsortedSourceTransients, recipe.sourceBTransients, recipe.transient);
    if (sortedTransientReport.warningCount != 1
        || sortedTransientReport.warnings[0].sourceMarkerIndex != 0
        || !approxEqual(sortedTransientReport.warnings[0].offsetSeconds, 0.0625, 1.0e-12))
    {
        return fail("transient alignment should compare plucked/percussive onsets by time rank");
    }

    ats::MorphRecipe snapshotRecipe = recipe;
    snapshotRecipe.correspondenceRecords.clear();
    for (auto& lane : snapshotRecipe.lanes)
    {
        lane.linkedToMainMorph = true;
        lane.amount = 0.0;
        lane.curve = ats::MorphCurveShape::Linear;
    }
    snapshotRecipe.outputGains.partialGain = 1.0;
    snapshotRecipe.outputGains.partialNoiseGain = 0.5;
    snapshotRecipe.outputGains.orphanResidualGain = 2.0;
    snapshotRecipe.lifecycle.birthFadeFrames = 4;
    snapshotRecipe.lifecycle.deathFadeFrames = 4;
    snapshotRecipe.lifecycle.curve = ats::MorphCurveShape::Linear;

    ats::MorphCorrespondenceRecord snapshotMatched;
    snapshotMatched.bandIndex = 0;
    snapshotMatched.rank = 0;
    snapshotMatched.sourcePartialIndex = 10;
    snapshotMatched.sourceFrameIndex = 0;
    snapshotMatched.sourceFrequencyHz = 100.0;
    snapshotMatched.sourceAmplitude = 0.2;
    snapshotMatched.targetPartialIndex = 20;
    snapshotMatched.targetFrameIndex = 0;
    snapshotMatched.targetFrequencyHz = 400.0;
    snapshotMatched.targetAmplitude = 0.6;
    snapshotMatched.kind = ats::MorphCorrespondenceKind::Matched;

    ats::MorphCorrespondenceRecord snapshotBirth;
    snapshotBirth.bandIndex = 1;
    snapshotBirth.rank = 0;
    snapshotBirth.targetPartialIndex = 21;
    snapshotBirth.targetFrameIndex = 0;
    snapshotBirth.targetFrequencyHz = 800.0;
    snapshotBirth.targetAmplitude = 0.4;
    snapshotBirth.kind = ats::MorphCorrespondenceKind::UnmatchedTarget;

    ats::MorphCorrespondenceRecord snapshotDeath;
    snapshotDeath.bandIndex = 1;
    snapshotDeath.rank = 1;
    snapshotDeath.sourcePartialIndex = 11;
    snapshotDeath.sourceFrameIndex = 0;
    snapshotDeath.sourceFrequencyHz = 200.0;
    snapshotDeath.sourceAmplitude = 0.3;
    snapshotDeath.kind = ats::MorphCorrespondenceKind::UnmatchedSource;

    snapshotRecipe.correspondenceRecords = {
        snapshotMatched,
        snapshotBirth,
        snapshotDeath,
    };

    ats::MorphSnapshotOptions snapshotOptions;
    snapshotOptions.morphAmount = 0.5;
    snapshotOptions.lifecycleFrameIndex = 4;
    snapshotOptions.sourceResidualNoiseBands = { { 0, 0.04, 0.16 } };
    snapshotOptions.targetResidualNoiseBands = { { 0, 0.36, 0.04 } };

    const auto snapshot = ats::evaluateMorphRecipeSnapshot(snapshotRecipe, snapshotOptions);
    const auto repeatedSnapshot = ats::evaluateMorphRecipeSnapshot(snapshotRecipe, snapshotOptions);
    if (snapshot != repeatedSnapshot)
        return fail("offline morph snapshot evaluation should be deterministic");
    if (!snapshot.requiresOfflinePreparation || !snapshot.safeForAudioThreadPlayback
        || snapshot.recipeSchemaVersion != snapshotRecipe.schemaVersion
        || snapshot.engineVersion != snapshotRecipe.engineVersion
        || !approxEqual(snapshot.morphAmount, 0.5, 1.0e-12)
        || !approxEqual(snapshot.laneAmounts[static_cast<size_t>(ats::MorphLane::Frequency)], 0.5, 1.0e-12)
        || !approxEqual(snapshot.laneAmounts[static_cast<size_t>(ats::MorphLane::Amplitude)], 0.5, 1.0e-12))
    {
        return fail("offline morph snapshot metadata or lane amounts were not prepared correctly");
    }
    if (snapshot.partials.size() != 3
        || !snapshot.partials[0].valid
        || !approxEqual(snapshot.partials[0].frequencyHz, 200.0, 1.0e-12)
        || !approxEqual(snapshot.partials[0].amplitude, std::sqrt(0.2), 1.0e-12)
        || snapshot.partials[0].lifecycle.isBirth
        || snapshot.partials[0].lifecycle.isDeath)
    {
        return fail("offline morph snapshot did not render the matched partial deterministically");
    }
    if (!snapshot.partials[1].lifecycle.isBirth
        || snapshot.partials[1].lifecycle.isDeath
        || !approxEqual(snapshot.partials[1].frequencyHz, 800.0, 1.0e-12)
        || !approxEqual(snapshot.partials[1].amplitude, std::sqrt(0.08), 1.0e-12))
    {
        return fail("offline morph snapshot did not render the unmatched target birth deterministically");
    }
    if (!snapshot.partials[2].lifecycle.isDeath
        || snapshot.partials[2].lifecycle.isBirth
        || !approxEqual(snapshot.partials[2].frequencyHz, 200.0, 1.0e-12)
        || !approxEqual(snapshot.partials[2].amplitude, 0.0, 1.0e-12))
    {
        return fail("offline morph snapshot did not apply the unmatched source death fade");
    }
    if (snapshot.residualNoise.bands.size() != 1
        || !approxEqual(snapshot.residualNoise.bands[0].outputPartialNoisePower, 0.05, 1.0e-12)
        || !approxEqual(snapshot.residualNoise.bands[0].outputOrphanResidualPower, 0.4, 1.0e-12)
        || snapshot.energy.warningBandCount != 0
        || snapshot.energy.bands.size() != 2
        || snapshot.transientAlignment.warningCount != 1)
    {
        return fail("offline morph snapshot did not prepare residual, energy, and transient diagnostics");
    }

    ats::MorphSnapshotPlaybackSettings playbackSettings;
    playbackSettings.sampleRate = 1000.0;
    playbackSettings.durationSeconds = 0.01;
    playbackSettings.residualCarrierHz = 125.0;
    const auto playback = ats::renderMorphSnapshotPlaybackBuffer(snapshot, playbackSettings);
    const auto repeatedPlayback = ats::renderMorphSnapshotPlaybackBuffer(snapshot, playbackSettings);
    if (playback.getNumSamples() != 10
        || playback.partialSamples.size() != 10
        || playback.residualSamples.size() != 10)
    {
        return fail("prepared morph snapshot playback buffer should use fixed partial/residual layers");
    }
    if (playback != repeatedPlayback)
        return fail("prepared morph snapshot playback buffer should be deterministic");

    constexpr double twoPi = 6.283185307179586476925286766559;
    const double collapsedResidualAmplitude = std::sqrt(snapshot.residualNoise.bands[0].outputTotalNoisePower);
    const double collapsedCarrierIncrement = twoPi * playbackSettings.residualCarrierHz / playbackSettings.sampleRate;
    bool differsFromCollapsedCarrier = false;
    for (int i = 0; i < playback.getNumSamples(); ++i)
    {
        const double collapsedCarrier = std::sin(collapsedCarrierIncrement * static_cast<double>(i))
                                        * collapsedResidualAmplitude;
        if (std::abs(static_cast<double>(playback.residualSamples[static_cast<size_t>(i)])
                     - collapsedCarrier) > 1.0e-4)
        {
            differsFromCollapsedCarrier = true;
            break;
        }
    }
    if (!differsFromCollapsedCarrier)
        return fail("prepared morph residual playback should not collapse band noise to one carrier");

    bool sawPartialSample = false;
    bool sawResidualSample = false;
    for (int i = 0; i < playback.getNumSamples(); ++i)
    {
        sawPartialSample = sawPartialSample || std::abs(playback.partialSamples[static_cast<size_t>(i)]) > 1.0e-6f;
        sawResidualSample = sawResidualSample || std::abs(playback.residualSamples[static_cast<size_t>(i)]) > 1.0e-6f;
    }
    if (!sawPartialSample || !sawResidualSample)
        return fail("prepared morph snapshot playback buffer should contain partial and residual layers");

    ats::MorphSnapshotPlaybackCursor cursor;
    std::vector<float> mixedBlock(4, 0.0f);
    if (ats::mixMorphSnapshotPlaybackBlock(playback,
                                           cursor,
                                           1.0f,
                                           0.0f,
                                           1.0f,
                                           false,
                                           mixedBlock.data(),
                                           static_cast<int>(mixedBlock.size())) != 4
        || cursor.sampleIndex != 4)
    {
        return fail("snapshot playback cursor should advance without allocation or synthesis");
    }

    cursor.sampleIndex = 0;
    std::vector<float> residualOnlyBlock(4, 0.0f);
    if (ats::mixMorphSnapshotPlaybackBlock(playback,
                                           cursor,
                                           0.0f,
                                           1.0f,
                                           1.0f,
                                           true,
                                           residualOnlyBlock.data(),
                                           static_cast<int>(residualOnlyBlock.size())) != 4
        || residualOnlyBlock == mixedBlock)
    {
        return fail("snapshot playback should support residual-only live mix changes");
    }

    ats::MorphSnapshotOptions alternateSnapshotOptions = snapshotOptions;
    alternateSnapshotOptions.morphAmount = 0.75;
    const auto alternateSnapshot = ats::evaluateMorphRecipeSnapshot(snapshotRecipe, alternateSnapshotOptions);
    const auto alternatePlayback = ats::renderMorphSnapshotPlaybackBuffer(alternateSnapshot, playbackSettings);
    const ats::MorphSnapshotPlaybackBuffer* activePlayback = &playback;
    cursor.sampleIndex = 0;
    std::vector<float> swapStressBlock(8, 0.0f);
    for (int iteration = 0; iteration < 48; ++iteration)
    {
        const bool swap = (iteration % 3) == 0;
        if (swap)
        {
            activePlayback = activePlayback == &playback ? &alternatePlayback : &playback;
            cursor.sampleIndex = 0;
        }

        const auto partialSizeBefore = activePlayback->partialSamples.size();
        const auto residualSizeBefore = activePlayback->residualSamples.size();
        const float partialGain = 0.25f + 0.01f * static_cast<float>(iteration % 7);
        const float residualGain = 0.5f + 0.02f * static_cast<float>(iteration % 5);
        const bool residualOnly = (iteration % 11) == 0;
        const int consumed = ats::mixMorphSnapshotPlaybackBlock(*activePlayback,
                                                                cursor,
                                                                partialGain,
                                                                residualGain,
                                                                0.9f,
                                                                residualOnly,
                                                                swapStressBlock.data(),
                                                                static_cast<int>(swapStressBlock.size()));
        if (consumed < 0 || activePlayback->partialSamples.size() != partialSizeBefore
            || activePlayback->residualSamples.size() != residualSizeBefore)
        {
            return fail("snapshot playback swaps should not mutate prepared buffers during live mix changes");
        }
    }

    const std::vector<ats::MorphBandAssignment> performanceSourceAssignments {
        { 300, 0, 100.0, 0.9, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        { 301, 0, 110.0, 0.8, 0, ats::MorphBandAssignmentKind::Assigned, 0.9 },
        { 302, 0, 120.0, 0.7, 0, ats::MorphBandAssignmentKind::Assigned, 0.8 },
        { 303, 0, 400.0, 0.6, 1, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        { 304, 0, 420.0, 0.5, 1, ats::MorphBandAssignmentKind::Assigned, 0.7 },
    };
    const std::vector<ats::MorphBandAssignment> performanceTargetAssignments {
        { 400, 0, 101.0, 0.9, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        { 401, 0, 121.0, 0.7, 0, ats::MorphBandAssignmentKind::Assigned, 0.8 },
        { 402, 0, 399.0, 0.6, 1, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        { 403, 0, 418.0, 0.5, 1, ats::MorphBandAssignmentKind::Assigned, 0.7 },
        { 404, 0, 440.0, 0.4, 1, ats::MorphBandAssignmentKind::Assigned, 0.6 },
    };
    ats::MorphAssignmentMatrixOptions performanceMatrixOptions;
    performanceMatrixOptions.advancedStrategyEnabled = true;
    performanceMatrixOptions.maxCandidatesPerBand = 8;
    performanceMatrixOptions.confidenceWeight = 0.25;
    const auto performanceProfile = ats::profileMorphPerformance(
        performanceSourceAssignments,
        performanceTargetAssignments,
        512,
        performanceMatrixOptions);
    const auto performanceProfileAgain = ats::profileMorphPerformance(
        performanceSourceAssignments,
        performanceTargetAssignments,
        512,
        performanceMatrixOptions);
    if (performanceProfile != performanceProfileAgain)
        return fail("morph performance profiling should be deterministic");
    if (performanceProfile.frameCount != 512
        || performanceProfile.sourceCandidateCount != 5
        || performanceProfile.targetCandidateCount != 5
        || performanceProfile.bandCount != 2
        || performanceProfile.strategies.size() != 3
        || performanceProfile.warningCount != 0
        || performanceProfile.estimatedSnapshotMemoryBytes == 0
        || performanceProfile.estimatedAnalysisMemoryBytes <= performanceProfile.estimatedSnapshotMemoryBytes)
    {
        return fail("representative morph performance profile did not record counts and memory estimates");
    }
    if (performanceProfile.strategies[0].strategy != ats::MorphCorrespondenceStrategy::FrequencyRank
        || performanceProfile.strategies[1].strategy != ats::MorphCorrespondenceStrategy::GreedyNearest
        || performanceProfile.strategies[2].strategy != ats::MorphCorrespondenceStrategy::AssignmentMatrix)
    {
        return fail("morph performance profile should compare rank, greedy, and assignment strategies");
    }
    if (performanceProfile.strategies[0].outputRecordCount
            != static_cast<int>(ats::buildFrequencyRankCorrespondence(performanceSourceAssignments,
                                                                      performanceTargetAssignments).size())
        || performanceProfile.strategies[1].candidateEdgeCount != 12
        || performanceProfile.strategies[2].candidateEdgeCount != 12
        || performanceProfile.strategies[2].evaluatedAssignmentCount <= 0
        || !(performanceProfile.strategies[2].relativeCpuCost
             > performanceProfile.strategies[1].relativeCpuCost))
    {
        return fail("morph performance profile did not compare strategy CPU costs");
    }

    ats::MorphPerformanceSoftCaps tinySoftCaps;
    tinySoftCaps.maxPartialsPerFrame = 4;
    tinySoftCaps.maxFrameCount = 128;
    tinySoftCaps.maxCandidateEdgesPerBand = 4;
    tinySoftCaps.maxEvaluatedAssignments = 0;
    tinySoftCaps.maxAnalysisMemoryBytes = 1;
    tinySoftCaps.maxSnapshotMemoryBytes = 1;
    const auto warnedPerformanceProfile = ats::profileMorphPerformance(
        performanceSourceAssignments,
        performanceTargetAssignments,
        512,
        performanceMatrixOptions,
        tinySoftCaps);
    if (!warnedPerformanceProfile.exceedsPartialSoftCap
        || !warnedPerformanceProfile.exceedsFrameSoftCap
        || !warnedPerformanceProfile.exceedsAnalysisMemorySoftCap
        || !warnedPerformanceProfile.exceedsSnapshotMemorySoftCap
        || warnedPerformanceProfile.strategies[0].exceedsCandidateEdgeSoftCap
        || !warnedPerformanceProfile.strategies[1].exceedsCandidateEdgeSoftCap
        || !warnedPerformanceProfile.strategies[2].exceedsCandidateEdgeSoftCap
        || !warnedPerformanceProfile.strategies[2].exceedsAssignmentSoftCap
        || warnedPerformanceProfile.warningCount < 7)
    {
        return fail("morph performance profile did not emit deterministic UI warning thresholds");
    }

    const ats::MorphFrequencyGrid barkGrid = ats::makeBarkFrequencyGrid(25, 20.0, 20000.0);
    if (barkGrid.type != ats::MorphFrequencyGridType::Bark || barkGrid.bandCount != 25
        || barkGrid.edgeFrequenciesHz.size() != 26)
    {
        return fail("Bark grid did not produce the expected band metadata");
    }
    if (!approxEqual(barkGrid.edgeFrequenciesHz.front(), 20.0, 1.0e-12)
        || !approxEqual(barkGrid.edgeFrequenciesHz.back(), 20000.0, 1.0e-9))
    {
        return fail("Bark grid did not preserve requested frequency bounds");
    }
    for (size_t i = 1; i < barkGrid.edgeFrequenciesHz.size(); ++i)
    {
        if (!(barkGrid.edgeFrequenciesHz[i] > barkGrid.edgeFrequenciesHz[i - 1]))
            return fail("Bark grid edges must be strictly increasing");
    }
    if (!approxEqual(barkGrid.edgeFrequenciesHz[1], 118.9285879352392, 1.0e-9)
        || !approxEqual(barkGrid.edgeFrequenciesHz[12], 1665.0498251908016, 1.0e-9)
        || !approxEqual(barkGrid.edgeFrequenciesHz[24], 13469.694503613933, 1.0e-8))
    {
        return fail("Bark grid stable edge anchors changed");
    }
    ats::MorphFrequencyGridType gridType = ats::MorphFrequencyGridType::Bark;
    if (!ats::morphFrequencyGridTypeFromName("bark", gridType) || gridType != ats::MorphFrequencyGridType::Bark)
        return fail("Bark grid type lookup failed");
    if (!ats::morphFrequencyGridTypeFromName("mel", gridType) || gridType != ats::MorphFrequencyGridType::Mel)
        return fail("Mel grid type lookup failed");

    const ats::MorphRecipe defaultRecipe = ats::makeDefaultMorphRecipe();
    if (defaultRecipe.frequencyGrid.type != ats::MorphFrequencyGridType::Bark)
        return fail("Bark must remain the default morph frequency grid");

    const ats::MorphFrequencyGrid melGrid = ats::makeMelFrequencyGrid(25, 20.0, 20000.0);
    if (melGrid.type != ats::MorphFrequencyGridType::Mel || melGrid.bandCount != 25
        || melGrid.edgeFrequenciesHz.size() != 26)
    {
        return fail("Mel grid did not produce the expected non-default metadata");
    }
    if (!approxEqual(melGrid.edgeFrequenciesHz.front(), 20.0, 1.0e-12)
        || !approxEqual(melGrid.edgeFrequenciesHz.back(), 20000.0, 1.0e-9))
    {
        return fail("Mel grid did not preserve requested frequency bounds");
    }
    for (size_t i = 1; i < melGrid.edgeFrequenciesHz.size(); ++i)
    {
        if (!(melGrid.edgeFrequenciesHz[i] > melGrid.edgeFrequenciesHz[i - 1]))
            return fail("Mel grid edges must be strictly increasing");
    }
    if (!approxEqual(melGrid.edgeFrequenciesHz[1], 123.52731848625925, 1.0e-9)
        || !approxEqual(melGrid.edgeFrequenciesHz[12], 2909.7627909392645, 1.0e-9)
        || !approxEqual(melGrid.edgeFrequenciesHz[24], 17397.760287291152, 1.0e-8))
    {
        return fail("Mel grid stable edge anchors changed");
    }
    if (approxEqual(barkGrid.edgeFrequenciesHz[12], melGrid.edgeFrequenciesHz[12], 1.0e-6)
        || approxEqual(barkGrid.edgeFrequenciesHz[24], melGrid.edgeFrequenciesHz[24], 1.0e-6))
    {
        return fail("Bark and Mel fixture-scale grids should remain measurably distinct");
    }

    ats::MorphRecipe melRecipe = recipe;
    melRecipe.frequencyGrid = melGrid;
    const std::string serializedMel = ats::serializeMorphRecipe(melRecipe);
    if (serializedMel.find("grid.type=mel\n") == std::string::npos)
        return fail("serialized recipe missing Mel grid metadata");
    if (!ats::deserializeMorphRecipe(serializedMel, parsed, &error))
        return fail("failed to deserialize Mel grid recipe: " + error);
    if (parsed != melRecipe)
        return fail("Mel grid recipe did not survive serialize/deserialize round trip");

    const ats::MorphFrequencyGrid simpleGrid = ats::makeBarkFrequencyGrid(4, 100.0, 1000.0);
    if (simpleGrid.edgeFrequenciesHz.size() != 5)
        return fail("simple grid edge count changed");

    const auto logFrequencyStart = ats::interpolateMorphFrequency(simpleGrid, 100.0, 400.0, 0.0);
    const auto logFrequencyMiddle = ats::interpolateMorphFrequency(simpleGrid, 100.0, 400.0, 0.5);
    const auto logFrequencyEnd = ats::interpolateMorphFrequency(simpleGrid, 100.0, 400.0, 1.0);
    if (!logFrequencyStart.valid || !logFrequencyEnd.valid
        || !approxEqual(logFrequencyStart.frequencyHz, 100.0, 1.0e-12)
        || !approxEqual(logFrequencyMiddle.frequencyHz, 200.0, 1.0e-12)
        || !approxEqual(logFrequencyEnd.frequencyHz, 400.0, 1.0e-12))
    {
        return fail("log-frequency interpolation did not preserve endpoints and geometric midpoint");
    }

    ats::MorphFrequencyInterpolationOptions barkInterpolation;
    barkInterpolation.domain = ats::MorphFrequencyInterpolationDomain::BarkCoordinate;
    const auto barkFrequencyStart = ats::interpolateMorphFrequency(simpleGrid, 120.0, 900.0, 0.0, barkInterpolation);
    const auto barkFrequencyMiddle = ats::interpolateMorphFrequency(simpleGrid, 120.0, 900.0, 0.5, barkInterpolation);
    const auto barkFrequencyEnd = ats::interpolateMorphFrequency(simpleGrid, 120.0, 900.0, 1.0, barkInterpolation);
    if (!barkFrequencyMiddle.valid
        || !approxEqual(barkFrequencyStart.frequencyHz, 120.0, 1.0e-12)
        || !approxEqual(barkFrequencyEnd.frequencyHz, 900.0, 1.0e-12)
        || !(barkFrequencyMiddle.frequencyHz > barkFrequencyStart.frequencyHz
             && barkFrequencyMiddle.frequencyHz < barkFrequencyEnd.frequencyHz))
    {
        return fail("Bark-coordinate interpolation did not preserve endpoints and monotonic midpoint");
    }

    double previousFrequency = 0.0;
    int previousBandIndex = -1;
    for (int step = 0; step <= 8; ++step)
    {
        const double amount = static_cast<double>(step) / 8.0;
        const auto sweep = ats::interpolateMorphFrequency(
            simpleGrid,
            simpleGrid.edgeFrequenciesHz[1] - 1.0,
            simpleGrid.edgeFrequenciesHz[3] + 1.0,
            amount);
        if (!sweep.valid || !sweep.crossedBand)
            return fail("frequency interpolation did not report band-edge migration");
        if (step > 0
            && (!(sweep.frequencyHz > previousFrequency)
                || sweep.outputBandIndex < previousBandIndex))
        {
            return fail("frequency interpolation sweep was not monotonic across band edges");
        }
        previousFrequency = sweep.frequencyHz;
        previousBandIndex = sweep.outputBandIndex;
    }

    const auto clampedFrequency = ats::interpolateMorphFrequency(simpleGrid, 200.0, 2000.0, 1.0);
    if (!clampedFrequency.valid
        || !clampedFrequency.clampedToGridBounds
        || clampedFrequency.outputBandIndex != simpleGrid.bandCount - 1
        || !approxEqual(clampedFrequency.frequencyHz, simpleGrid.edgeFrequenciesHz.back(), 1.0e-12))
    {
        return fail("frequency interpolation did not clamp out-of-grid output to grid bounds");
    }

    const auto powerAmplitudeStart = ats::interpolateMorphAmplitude(0.2, 0.6, 0.0);
    const auto powerAmplitudeMiddle = ats::interpolateMorphAmplitude(0.2, 0.6, 0.5);
    const auto powerAmplitudeEnd = ats::interpolateMorphAmplitude(0.2, 0.6, 1.0);
    if (!powerAmplitudeStart.valid || !powerAmplitudeMiddle.valid || !powerAmplitudeEnd.valid
        || !approxEqual(powerAmplitudeStart.amplitude, 0.2, 1.0e-12)
        || !approxEqual(powerAmplitudeMiddle.amplitude, std::sqrt(0.2), 1.0e-12)
        || !approxEqual(powerAmplitudeMiddle.power, 0.2, 1.0e-12)
        || !approxEqual(powerAmplitudeEnd.amplitude, 0.6, 1.0e-12))
    {
        return fail("power amplitude interpolation did not preserve endpoints and power midpoint");
    }

    double previousAmplitude = -1.0;
    for (int step = 0; step <= 8; ++step)
    {
        const double amount = static_cast<double>(step) / 8.0;
        const auto sweep = ats::interpolateMorphAmplitude(0.05, 0.9, amount);
        if (!sweep.valid || sweep.amplitude < previousAmplitude || sweep.amplitude > 0.9 + 1.0e-12)
            return fail("power amplitude interpolation sweep was not monotonic");
        previousAmplitude = sweep.amplitude;
    }

    ats::MorphAmplitudeInterpolationOptions dbAmplitudeOptions;
    dbAmplitudeOptions.domain = ats::MorphAmplitudeInterpolationDomain::Decibels;
    dbAmplitudeOptions.decibelFloor = -80.0;
    const auto dbAmplitudeStart = ats::interpolateMorphAmplitude(1.0, 0.01, 0.0, dbAmplitudeOptions);
    const auto dbAmplitudeMiddle = ats::interpolateMorphAmplitude(1.0, 0.01, 0.5, dbAmplitudeOptions);
    const auto dbAmplitudeEnd = ats::interpolateMorphAmplitude(1.0, 0.01, 1.0, dbAmplitudeOptions);
    if (!dbAmplitudeMiddle.valid
        || !approxEqual(dbAmplitudeStart.amplitude, 1.0, 1.0e-12)
        || !approxEqual(dbAmplitudeMiddle.amplitude, 0.1, 1.0e-12)
        || !approxEqual(dbAmplitudeEnd.amplitude, 0.01, 1.0e-12))
    {
        return fail("dB amplitude interpolation did not preserve endpoints and dB midpoint");
    }

    const auto quietDbStart = ats::interpolateMorphAmplitude(0.0, 1.0, 0.0, dbAmplitudeOptions);
    const auto quietDbQuarter = ats::interpolateMorphAmplitude(0.0, 1.0, 0.25, dbAmplitudeOptions);
    const auto quietDbMiddle = ats::interpolateMorphAmplitude(0.0, 1.0, 0.5, dbAmplitudeOptions);
    const auto quietDbEnd = ats::interpolateMorphAmplitude(0.0, 1.0, 1.0, dbAmplitudeOptions);
    if (!quietDbQuarter.valid || !quietDbQuarter.usedDecibelFloor
        || quietDbStart.amplitude != 0.0
        || !(quietDbQuarter.amplitude > quietDbStart.amplitude
             && quietDbQuarter.amplitude < quietDbMiddle.amplitude
             && quietDbMiddle.amplitude < quietDbEnd.amplitude)
        || quietDbQuarter.amplitude > 0.002
        || !approxEqual(quietDbMiddle.amplitude, 0.01, 1.0e-12)
        || !approxEqual(quietDbEnd.amplitude, 1.0, 1.0e-12))
    {
        return fail("dB amplitude floor did not keep quiet partial interpolation smooth");
    }

    ats::MorphAmplitudeInterpolationOptions raisedFloorOptions = dbAmplitudeOptions;
    raisedFloorOptions.decibelFloor = -40.0;
    const auto raisedFloorMiddle = ats::interpolateMorphAmplitude(0.0, 1.0, 0.5, raisedFloorOptions);
    if (!raisedFloorMiddle.valid || !approxEqual(raisedFloorMiddle.amplitude, 0.1, 1.0e-12))
        return fail("dB amplitude interpolation did not honor configurable floor");

    const auto zeroAmplitudeMiddle = ats::interpolateMorphAmplitude(0.0, 0.0, 0.5, dbAmplitudeOptions);
    if (!zeroAmplitudeMiddle.valid || zeroAmplitudeMiddle.amplitude != 0.0 || zeroAmplitudeMiddle.power != 0.0)
        return fail("zero-to-zero amplitude interpolation should remain silent");

    const std::vector<ats::MorphResidualNoiseBand> noisyBreathySourceResidual {
        { 0, 0.04, 0.16 },
        { 2, 0.01, 0.09 },
    };
    const std::vector<ats::MorphResidualNoiseBand> denseTargetResidual {
        { 0, 0.36, 0.04 },
        { 1, 0.09, 0.25 },
    };
    ats::MorphResidualNoiseOptions residualNoiseOptions;
    residualNoiseOptions.morphAmount = 0.5;
    const auto residualNoiseReport = ats::morphResidualNoiseBands(
        noisyBreathySourceResidual,
        denseTargetResidual,
        residualNoiseOptions);
    if (residualNoiseReport.bands.size() != 3
        || residualNoiseReport.bands[0].bandIndex != 0
        || residualNoiseReport.bands[1].bandIndex != 1
        || residualNoiseReport.bands[2].bandIndex != 2
        || !approxEqual(residualNoiseReport.bands[0].outputPartialNoisePower, 0.2, 1.0e-12)
        || !approxEqual(residualNoiseReport.bands[0].outputOrphanResidualPower, 0.1, 1.0e-12)
        || !approxEqual(residualNoiseReport.bands[0].outputTotalNoisePower, 0.3, 1.0e-12)
        || !approxEqual(residualNoiseReport.bands[1].outputPartialNoisePower, 0.045, 1.0e-12)
        || !approxEqual(residualNoiseReport.bands[1].outputOrphanResidualPower, 0.125, 1.0e-12)
        || !approxEqual(residualNoiseReport.bands[2].outputPartialNoisePower, 0.005, 1.0e-12)
        || !approxEqual(residualNoiseReport.bands[2].outputOrphanResidualPower, 0.045, 1.0e-12))
    {
        return fail("noisy/breathy residual fixture did not morph partial-noise and orphan residual lanes separately");
    }

    residualNoiseOptions.morphAmount = 0.0;
    const auto residualNoiseStart = ats::morphResidualNoiseBands(
        noisyBreathySourceResidual,
        denseTargetResidual,
        residualNoiseOptions);
    residualNoiseOptions.morphAmount = 1.0;
    const auto residualNoiseEnd = ats::morphResidualNoiseBands(
        noisyBreathySourceResidual,
        denseTargetResidual,
        residualNoiseOptions);
    if (!approxEqual(residualNoiseStart.bands[0].outputOrphanResidualPower, 0.16, 1.0e-12)
        || !approxEqual(residualNoiseEnd.bands[0].outputOrphanResidualPower, 0.04, 1.0e-12))
    {
        return fail("orphan residual energy was not preserved at residual/noise morph endpoints");
    }

    residualNoiseOptions.morphAmount = 0.5;
    residualNoiseOptions.gains.partialGain = 0.25;
    residualNoiseOptions.gains.partialNoiseGain = 0.5;
    residualNoiseOptions.gains.orphanResidualGain = 2.0;
    const auto residualNoiseGained = ats::morphResidualNoiseBands(
        noisyBreathySourceResidual,
        denseTargetResidual,
        residualNoiseOptions);
    if (!approxEqual(residualNoiseGained.outputPartialGain, 0.25, 1.0e-12)
        || !approxEqual(residualNoiseGained.outputPartialNoiseGain, 0.5, 1.0e-12)
        || !approxEqual(residualNoiseGained.outputOrphanResidualGain, 2.0, 1.0e-12)
        || !approxEqual(residualNoiseGained.bands[0].outputPartialNoisePower, 0.05, 1.0e-12)
        || !approxEqual(residualNoiseGained.bands[0].outputOrphanResidualPower, 0.4, 1.0e-12))
    {
        return fail("independent residual/noise output gains were not applied to separate power paths");
    }

    ats::MorphBandEnergyOptions energyOptions;
    energyOptions.morphAmount = 0.5;
    const std::vector<ats::MorphCorrespondenceRecord> threeToOneEnergyRecords {
        makeMatchedEnergyRecord(0, 0, 0.5, 0.5),
        makeUnmatchedSourceEnergyRecord(0, 1, 0.3),
        makeUnmatchedSourceEnergyRecord(0, 2, 0.4),
    };
    const auto threeToOneEnergy = ats::calculateBandEnergyReport(threeToOneEnergyRecords, energyOptions);
    if (threeToOneEnergy.bands.size() != 1
        || threeToOneEnergy.warningBandCount != 0
        || !approxEqual(threeToOneEnergy.bands[0].sourceSinusoidalPower, 0.5, 1.0e-12)
        || !approxEqual(threeToOneEnergy.bands[0].targetSinusoidalPower, 0.25, 1.0e-12)
        || !approxEqual(threeToOneEnergy.bands[0].outputSinusoidalPower, 0.375, 1.0e-12)
        || !approxEqual(threeToOneEnergy.bands[0].expectedOutputPower, 0.375, 1.0e-12)
        || threeToOneEnergy.bands[0].exceedsTolerance)
    {
        return fail("3-to-1 band energy report did not conserve default fade-out power");
    }

    const std::vector<ats::MorphCorrespondenceRecord> oneToThreeEnergyRecords {
        makeMatchedEnergyRecord(1, 0, 0.5, 0.5),
        makeUnmatchedTargetEnergyRecord(1, 1, 0.3),
        makeUnmatchedTargetEnergyRecord(1, 2, 0.4),
    };
    const auto oneToThreeEnergy = ats::calculateBandEnergyReport(oneToThreeEnergyRecords, energyOptions);
    if (oneToThreeEnergy.bands.size() != 1
        || oneToThreeEnergy.warningBandCount != 0
        || oneToThreeEnergy.bands[0].bandIndex != 1
        || !approxEqual(oneToThreeEnergy.bands[0].sourceSinusoidalPower, 0.25, 1.0e-12)
        || !approxEqual(oneToThreeEnergy.bands[0].targetSinusoidalPower, 0.5, 1.0e-12)
        || !approxEqual(oneToThreeEnergy.bands[0].outputSinusoidalPower, 0.375, 1.0e-12)
        || !approxEqual(oneToThreeEnergy.bands[0].expectedOutputPower, 0.375, 1.0e-12))
    {
        return fail("1-to-3 band energy report did not conserve default fade-in power");
    }

    ats::MorphBandEnergyOptions residualEnergyOptions = energyOptions;
    residualEnergyOptions.unmatchedPolicies.sourcePolicy = ats::MorphUnmatchedPartialPolicy::MergeIntoResidual;
    const auto residualEnergy = ats::calculateBandEnergyReport(threeToOneEnergyRecords, residualEnergyOptions);
    if (residualEnergy.warningBandCount != 0
        || !approxEqual(residualEnergy.bands[0].outputSinusoidalPower, 0.25, 1.0e-12)
        || !approxEqual(residualEnergy.bands[0].outputResidualPower, 0.125, 1.0e-12)
        || !approxEqual(residualEnergy.bands[0].powerDelta, 0.0, 1.0e-12))
    {
        return fail("merge-into-residual band energy did not reroute unmatched source power");
    }

    ats::MorphBandEnergyOptions discardEnergyOptions = energyOptions;
    discardEnergyOptions.unmatchedPolicies.sourcePolicy = ats::MorphUnmatchedPartialPolicy::Discard;
    const auto discardEnergy = ats::calculateBandEnergyReport(threeToOneEnergyRecords, discardEnergyOptions);
    if (discardEnergy.warningBandCount != 1
        || !discardEnergy.bands[0].exceedsTolerance
        || !approxEqual(discardEnergy.bands[0].outputSinusoidalPower, 0.25, 1.0e-12)
        || !approxEqual(discardEnergy.bands[0].powerDelta, -0.125, 1.0e-12))
    {
        return fail("discard band energy did not emit a power-delta warning");
    }

    const std::vector<ats::MorphPartialFrame> frames {
        { 1, 0, simpleGrid.edgeFrequenciesHz[0], 0.5, true },
        { 1, 1, simpleGrid.edgeFrequenciesHz[1], 0.5, true },
        { 1, 2, simpleGrid.edgeFrequenciesHz.back(), 0.5, true },
        { 1, 3, simpleGrid.edgeFrequenciesHz.front() - 0.01, 0.5, true },
        { 1, 4, simpleGrid.edgeFrequenciesHz.back() + 0.01, 0.5, true },
        { 1, 5, simpleGrid.edgeFrequenciesHz[2], 0.0, true },
        { 1, 6, simpleGrid.edgeFrequenciesHz[2], 0.5, false },
    };
    const auto frameAssignments = ats::assignPartialFramesToFrequencyBands(simpleGrid, frames, 0.0);
    if (frameAssignments.size() != frames.size())
        return fail("frame assignment count mismatch");
    if (frameAssignments[0].kind != ats::MorphBandAssignmentKind::Assigned || frameAssignments[0].bandIndex != 0)
        return fail("lower grid edge should assign to the first band");
    if (frameAssignments[1].kind != ats::MorphBandAssignmentKind::Assigned || frameAssignments[1].bandIndex != 1)
        return fail("internal grid edge should assign to the higher band");
    if (frameAssignments[2].kind != ats::MorphBandAssignmentKind::Assigned || frameAssignments[2].bandIndex != 3)
        return fail("upper grid edge should assign to the final band");
    if (frameAssignments[3].kind != ats::MorphBandAssignmentKind::OutOfRangeLow)
        return fail("low out-of-range frame was not explicit");
    if (frameAssignments[4].kind != ats::MorphBandAssignmentKind::OutOfRangeHigh)
        return fail("high out-of-range frame was not explicit");
    if (frameAssignments[5].kind != ats::MorphBandAssignmentKind::Silent)
        return fail("silent frame was not explicit");
    if (frameAssignments[6].kind != ats::MorphBandAssignmentKind::Inactive)
        return fail("inactive frame was not explicit");
    if (std::string(ats::morphBandAssignmentKindName(ats::MorphBandAssignmentKind::OutOfRangeHigh))
        != "out_of_range_high")
    {
        return fail("band assignment kind name lookup failed");
    }

    const std::vector<ats::MorphPartialTrack> tracks {
        { 7,
          {
              { 7, 0, simpleGrid.edgeFrequenciesHz[1] + 1.0, 1.0, true },
              { 7, 1, simpleGrid.edgeFrequenciesHz[1] + 3.0, 3.0, true },
              { 7, 2, simpleGrid.edgeFrequenciesHz[2], 0.0, true },
          } },
        { 8, { { 8, 0, simpleGrid.edgeFrequenciesHz[2], 0.0, true } } },
        { 9, { { 9, 0, simpleGrid.edgeFrequenciesHz.back() + 10.0, 1.0, true } } },
    };
    const auto centroidAssignments = ats::assignPartialTracksToCentroidBands(simpleGrid, tracks, 0.0);
    if (centroidAssignments.size() != tracks.size())
        return fail("centroid assignment count mismatch");
    if (centroidAssignments[0].kind != ats::MorphBandAssignmentKind::Assigned
        || centroidAssignments[0].partialIndex != 7
        || centroidAssignments[0].contributingFrameCount != 2
        || !approxEqual(centroidAssignments[0].centroidFrequencyHz,
                        simpleGrid.edgeFrequenciesHz[1] + 2.5,
                        1.0e-12))
    {
        return fail("track centroid assignment did not use amplitude-weighted active frames");
    }
    if (centroidAssignments[1].kind != ats::MorphBandAssignmentKind::Silent)
        return fail("silent track centroid was not explicit");
    if (centroidAssignments[2].kind != ats::MorphBandAssignmentKind::OutOfRangeHigh)
        return fail("out-of-range track centroid was not explicit");

    const double bandOneBase = simpleGrid.edgeFrequenciesHz[1] + 1.0;
    const double bandTwoBase = simpleGrid.edgeFrequenciesHz[2] + 1.0;
    const std::vector<ats::MorphPartialFrame> sourceCorrespondenceFrames {
        { 20, 0, bandOneBase + 8.0, 0.8, true },
        { 21, 0, bandOneBase + 2.0, 0.7, true },
        { 22, 0, bandOneBase + 4.0, 0.6, true },
        { 23, 0, bandTwoBase + 2.0, 0.5, true },
        { 24, 0, bandTwoBase + 4.0, 0.0, true },
    };
    const std::vector<ats::MorphPartialFrame> targetCorrespondenceFrames {
        { 30, 0, bandOneBase + 6.0, 0.5, true },
        { 31, 0, bandOneBase + 1.0, 0.9, true },
        { 32, 0, bandTwoBase + 5.0, 0.3, true },
        { 33, 0, bandTwoBase + 7.0, 0.2, true },
        { 34, 0, bandTwoBase + 9.0, 0.4, false },
    };
    const auto sourceCorrespondenceAssignments =
        ats::assignPartialFramesToFrequencyBands(simpleGrid, sourceCorrespondenceFrames, 0.0);
    const auto targetCorrespondenceAssignments =
        ats::assignPartialFramesToFrequencyBands(simpleGrid, targetCorrespondenceFrames, 0.0);
    const auto correspondences =
        ats::buildFrequencyRankCorrespondence(sourceCorrespondenceAssignments, targetCorrespondenceAssignments);
    if (correspondences.size() != 5)
        return fail("frequency-rank correspondence should include matched and unmatched records only");
    if (correspondences[0].kind != ats::MorphCorrespondenceKind::Matched
        || correspondences[0].bandIndex != 1
        || correspondences[0].rank != 0
        || correspondences[0].sourcePartialIndex != 21
        || correspondences[0].targetPartialIndex != 31)
    {
        return fail("frequency-rank correspondence did not pair the lowest in-band frequencies first");
    }
    if (correspondences[1].kind != ats::MorphCorrespondenceKind::Matched
        || correspondences[1].rank != 1
        || correspondences[1].sourcePartialIndex != 22
        || correspondences[1].targetPartialIndex != 30)
    {
        return fail("frequency-rank correspondence did not preserve rank order within the band");
    }
    if (correspondences[2].kind != ats::MorphCorrespondenceKind::UnmatchedSource
        || correspondences[2].rank != 2
        || correspondences[2].sourcePartialIndex != 20
        || correspondences[2].targetPartialIndex != -1)
    {
        return fail("frequency-rank correspondence did not emit the extra source partial explicitly");
    }
    if (correspondences[3].kind != ats::MorphCorrespondenceKind::Matched
        || correspondences[3].bandIndex != 2
        || correspondences[3].sourcePartialIndex != 23
        || correspondences[3].targetPartialIndex != 32)
    {
        return fail("frequency-rank correspondence did not continue deterministically across bands");
    }
    if (correspondences[4].kind != ats::MorphCorrespondenceKind::UnmatchedTarget
        || correspondences[4].rank != 1
        || correspondences[4].sourcePartialIndex != -1
        || correspondences[4].targetPartialIndex != 33)
    {
        return fail("frequency-rank correspondence did not emit the extra target partial explicitly");
    }
    if (std::string(ats::morphCorrespondenceKindName(ats::MorphCorrespondenceKind::UnmatchedTarget))
        != "unmatched_target")
    {
        return fail("correspondence kind name lookup failed");
    }
    ats::MorphCorrespondenceKind correspondenceKind = ats::MorphCorrespondenceKind::Matched;
    if (!ats::morphCorrespondenceKindFromName("unmatched_source", correspondenceKind)
        || correspondenceKind != ats::MorphCorrespondenceKind::UnmatchedSource)
    {
        return fail("correspondence kind parse failed");
    }

    ats::MorphRecipe correspondenceRecipe = recipe;
    correspondenceRecipe.correspondenceRecords = correspondences;
    const std::string serializedCorrespondence = ats::serializeMorphRecipe(correspondenceRecipe);
    if (serializedCorrespondence.find("correspondence.count=5\n") == std::string::npos
        || serializedCorrespondence.find("correspondence.2.kind=unmatched_source\n") == std::string::npos
        || serializedCorrespondence.find("correspondence.4.target_partial_index=33\n") == std::string::npos)
    {
        return fail("serialized recipe missing explicit correspondence records");
    }
    if (!ats::deserializeMorphRecipe(serializedCorrespondence, parsed, &error))
        return fail("failed to deserialize correspondence recipe: " + error);
    if (parsed != correspondenceRecipe)
        return fail("correspondence records did not survive serialize/deserialize round trip");

    // Amplitude-rank weighting: within a band, the loud partials of source and target should be
    // matched together when the weight is 1, instead of being paired purely by frequency rank.
    {
        const std::vector<ats::MorphBandAssignment> ampSource {
            { 500, 0, 100.0, 0.2, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
            { 501, 0, 110.0, 0.9, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        };
        const std::vector<ats::MorphBandAssignment> ampTarget {
            { 600, 0, 101.0, 0.9, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
            { 601, 0, 111.0, 0.2, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        };

        const auto byFreq = ats::buildFrequencyRankCorrespondence(ampSource, ampTarget, 0.0);
        if (byFreq.size() != 2
            || byFreq[0].sourcePartialIndex != 500 || byFreq[0].targetPartialIndex != 600
            || byFreq[1].sourcePartialIndex != 501 || byFreq[1].targetPartialIndex != 601)
        {
            return fail("zero amplitude weight should preserve pure frequency-rank pairing");
        }

        const auto byAmp = ats::buildFrequencyRankCorrespondence(ampSource, ampTarget, 1.0);
        // Loud source (501) pairs with loud target (600); quiet source (500) with quiet target (601).
        bool loudPairMatched = false;
        bool quietPairMatched = false;
        for (const auto& record : byAmp)
        {
            if (record.kind != ats::MorphCorrespondenceKind::Matched)
                continue;
            if (record.sourcePartialIndex == 501 && record.targetPartialIndex == 600)
                loudPairMatched = true;
            if (record.sourcePartialIndex == 500 && record.targetPartialIndex == 601)
                quietPairMatched = true;
        }
        if (!loudPairMatched || !quietPairMatched)
            return fail("amplitude weight of 1 should pair loudest-with-loudest within the band");
    }

    // Per-frame correspondence re-pairing: matched partials should re-pair to the nearest
    // instantaneous log-frequency, and hysteresis should hold an established pairing through a
    // near-tie.
    {
        // No crossing: nearest partners are the in-order pair.
        const std::vector<double> srcA { std::log(100.0), std::log(200.0) };
        const std::vector<double> tgtA { std::log(101.0), std::log(201.0) };
        const auto a = ats::reassignCorrespondenceByLogFrequency(srcA, tgtA, { 0, 1 }, 0.02);
        if (a.size() != 2 || a[0] != 0 || a[1] != 1)
            return fail("re-pairing should keep non-crossing partials in order");

        // Crossing: the closest target swaps, so the assignment inverts.
        const std::vector<double> srcB { std::log(100.0), std::log(200.0) };
        const std::vector<double> tgtB { std::log(205.0), std::log(98.0) };
        const auto b = ats::reassignCorrespondenceByLogFrequency(srcB, tgtB, { 0, 1 }, 0.0);
        if (b.size() != 2 || b[0] != 1 || b[1] != 0)
            return fail("re-pairing should swap partners at a frequency crossing");

        // Hysteresis: a near-tie keeps the previous pairing instead of flipping.
        const std::vector<double> srcC { std::log(100.0), std::log(102.0) };
        const std::vector<double> tgtC { std::log(101.0), std::log(101.5) };
        const auto noStick = ats::reassignCorrespondenceByLogFrequency(srcC, tgtC, { 0, 1 }, 0.0);
        const auto stick = ats::reassignCorrespondenceByLogFrequency(srcC, tgtC, { 1, 0 }, 0.02);
        if (noStick.size() != 2 || noStick[0] != 0 || noStick[1] != 1)
            return fail("re-pairing without hysteresis should pick the lowest-cost assignment");
        if (stick.size() != 2 || stick[0] != 1 || stick[1] != 0)
            return fail("hysteresis should retain the previous pairing across a near-tie");
    }

    const ats::MorphFrequencyGrid comparisonGrid = ats::makeBarkFrequencyGrid(1, 80.0, 700.0);
    const auto vibratoSourceAssignments = ats::assignPartialFramesToFrequencyBands(
        comparisonGrid,
        {
            { 40, 0, 440.0, 0.7, true },
            { 41, 0, 450.0, 0.5, true },
            { 42, 0, 660.0, 0.4, true },
        },
        0.0);
    const auto vibratoTargetAssignments = ats::assignPartialFramesToFrequencyBands(
        comparisonGrid,
        {
            { 50, 0, 442.0, 0.7, true },
            { 51, 0, 662.0, 0.4, true },
        },
        0.0);
    const auto vibratoRank =
        ats::buildFrequencyRankCorrespondence(vibratoSourceAssignments, vibratoTargetAssignments);
    const auto vibratoGreedy =
        ats::buildGreedyNearestCorrespondence(vibratoSourceAssignments, vibratoTargetAssignments);
    bool vibratoMatchedUpperPartial = false;
    bool vibratoLeftMiddleUnmatched = false;
    for (const auto& record : vibratoGreedy)
    {
        if (record.kind == ats::MorphCorrespondenceKind::Matched
            && record.sourcePartialIndex == 42
            && record.targetPartialIndex == 51
            && record.hasCostDiagnostics)
        {
            vibratoMatchedUpperPartial = true;
        }
        if (record.kind == ats::MorphCorrespondenceKind::UnmatchedSource
            && record.sourcePartialIndex == 41)
        {
            vibratoLeftMiddleUnmatched = true;
        }
    }
    if (!vibratoMatchedUpperPartial || !vibratoLeftMiddleUnmatched
        || !(matchedLogCostSum(vibratoGreedy) < matchedLogCostSum(vibratoRank)))
    {
        return fail("greedy nearest matching did not improve the vibrato-style count mismatch fixture");
    }

    const auto inharmonicSourceAssignments = ats::assignPartialFramesToFrequencyBands(
        comparisonGrid,
        {
            { 60, 0, 100.0, 0.5, true },
            { 61, 0, 105.0, 0.5, true },
            { 62, 0, 300.0, 0.5, true },
        },
        0.0);
    const auto inharmonicTargetAssignments = ats::assignPartialFramesToFrequencyBands(
        comparisonGrid,
        {
            { 70, 0, 102.0, 0.5, true },
            { 71, 0, 295.0, 0.5, true },
        },
        0.0);
    const auto inharmonicRank =
        ats::buildFrequencyRankCorrespondence(inharmonicSourceAssignments, inharmonicTargetAssignments);
    const auto inharmonicGreedy =
        ats::buildGreedyNearestCorrespondence(inharmonicSourceAssignments, inharmonicTargetAssignments);
    bool inharmonicMatchedDistantPartial = false;
    bool inharmonicLeftNearDuplicateUnmatched = false;
    for (const auto& record : inharmonicGreedy)
    {
        if (record.kind == ats::MorphCorrespondenceKind::Matched
            && record.sourcePartialIndex == 62
            && record.targetPartialIndex == 71
            && record.hasCostDiagnostics
            && approxEqual(record.totalCost, record.logFrequencyCost + record.amplitudeRankCost, 1.0e-12))
        {
            inharmonicMatchedDistantPartial = true;
        }
        if (record.kind == ats::MorphCorrespondenceKind::UnmatchedSource
            && record.sourcePartialIndex == 61)
        {
            inharmonicLeftNearDuplicateUnmatched = true;
        }
    }
    if (!inharmonicMatchedDistantPartial || !inharmonicLeftNearDuplicateUnmatched
        || !(matchedLogCostSum(inharmonicGreedy) < matchedLogCostSum(inharmonicRank)))
    {
        return fail("greedy nearest matching did not improve the inharmonic-style count mismatch fixture");
    }

    const auto amplitudeBiasSourceAssignments = ats::assignPartialFramesToFrequencyBands(
        comparisonGrid,
        {
            { 80, 0, 100.0, 1.0, true },
            { 81, 0, 105.0, 0.1, true },
        },
        0.0);
    const auto amplitudeBiasTargetAssignments = ats::assignPartialFramesToFrequencyBands(
        comparisonGrid,
        {
            { 90, 0, 101.0, 0.1, true },
            { 91, 0, 104.0, 1.0, true },
        },
        0.0);
    const auto pureGreedy =
        ats::buildGreedyNearestCorrespondence(amplitudeBiasSourceAssignments, amplitudeBiasTargetAssignments);
    ats::MorphGreedyNearestOptions amplitudeBiasedOptions;
    amplitudeBiasedOptions.amplitudeRankWeight = 1.0;
    const auto amplitudeBiasedGreedy = ats::buildGreedyNearestCorrespondence(
        amplitudeBiasSourceAssignments,
        amplitudeBiasTargetAssignments,
        amplitudeBiasedOptions);
    bool pureMatchedByFrequency = false;
    bool biasedMatchedByAmplitudeRank = false;
    for (const auto& record : pureGreedy)
    {
        pureMatchedByFrequency = pureMatchedByFrequency
                                 || (record.kind == ats::MorphCorrespondenceKind::Matched
                                     && record.sourcePartialIndex == 80
                                     && record.targetPartialIndex == 90
                                     && record.hasCostDiagnostics
                                     && approxEqual(record.amplitudeRankCost, 0.0, 1.0e-12));
    }
    for (const auto& record : amplitudeBiasedGreedy)
    {
        biasedMatchedByAmplitudeRank = biasedMatchedByAmplitudeRank
                                       || (record.kind == ats::MorphCorrespondenceKind::Matched
                                           && record.sourcePartialIndex == 80
                                           && record.targetPartialIndex == 91
                                           && record.hasCostDiagnostics
                                           && approxEqual(record.amplitudeRankCost, 0.0, 1.0e-12));
    }
    if (!pureMatchedByFrequency || !biasedMatchedByAmplitudeRank)
        return fail("amplitude-rank bias did not alter greedy nearest matching deterministically");

    ats::MorphAssignmentMatrixOptions disabledMatrixOptions;
    const auto disabledMatrixResult = ats::buildAssignmentMatrixCorrespondence(
        amplitudeBiasSourceAssignments,
        amplitudeBiasTargetAssignments,
        disabledMatrixOptions);
    if (disabledMatrixResult.strategy != ats::MorphCorrespondenceStrategy::GreedyNearest
        || disabledMatrixResult.records != pureGreedy)
    {
        return fail("assignment-matrix matching must stay behind the advanced strategy flag");
    }

    const std::vector<ats::MorphBandAssignment> matrixQualitySource {
        { 120, 0, 100.0, 0.8, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        { 121, 0, 110.0, 0.7, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
    };
    const std::vector<ats::MorphBandAssignment> matrixQualityTarget {
        { 220, 0, 109.0, 0.8, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        { 221, 0, 200.0, 0.7, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
    };
    const auto greedyQuality = ats::buildGreedyNearestCorrespondence(matrixQualitySource, matrixQualityTarget);
    ats::MorphAssignmentMatrixOptions matrixOptions;
    matrixOptions.advancedStrategyEnabled = true;
    matrixOptions.maxCandidatesPerBand = 8;
    const auto matrixQuality = ats::buildAssignmentMatrixCorrespondence(
        matrixQualitySource,
        matrixQualityTarget,
        matrixOptions);
    bool matrixChoseGlobalMinimum = false;
    for (const auto& record : matrixQuality.records)
    {
        matrixChoseGlobalMinimum = matrixChoseGlobalMinimum
                                   || (record.kind == ats::MorphCorrespondenceKind::Matched
                                       && record.sourcePartialIndex == 120
                                       && record.targetPartialIndex == 220
                                       && record.hasCostDiagnostics);
    }
    if (matrixQuality.strategy != ats::MorphCorrespondenceStrategy::AssignmentMatrix
        || matrixQuality.candidateEdgeCount != 4
        || matrixQuality.evaluatedAssignmentCount <= 0
        || matrixQuality.evaluatedAssignmentCount > 2
        || !matrixChoseGlobalMinimum
        || !(matrixQuality.totalMatchedCost < matchedLogCostSum(greedyQuality)))
    {
        return fail("assignment-matrix matcher did not improve the bounded 2x2 quality fixture");
    }

    ats::MorphAssignmentMatrixOptions boundedMatrixOptions = matrixOptions;
    boundedMatrixOptions.maxCandidatesPerBand = 1;
    const auto boundedMatrix = ats::buildAssignmentMatrixCorrespondence(
        {
            { 130, 0, 100.0, 0.9, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
            { 131, 0, 105.0, 0.8, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
            { 132, 0, 110.0, 0.1, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        },
        {
            { 230, 0, 101.0, 0.9, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
            { 231, 0, 106.0, 0.8, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
            { 232, 0, 111.0, 0.1, 0, ats::MorphBandAssignmentKind::Assigned, 1.0 },
        },
        boundedMatrixOptions);
    if (!boundedMatrix.wasCandidateBounded
        || boundedMatrix.sourceCandidateCount != 3
        || boundedMatrix.targetCandidateCount != 3
        || boundedMatrix.candidateEdgeCount != 1
        || boundedMatrix.evaluatedAssignmentCount != 1
        || boundedMatrix.records.size() != 5)
    {
        return fail("assignment-matrix matcher did not keep candidate sets bounded");
    }

    ats::MorphAssignmentMatrixOptions diagnosticMatrixOptions = matrixOptions;
    diagnosticMatrixOptions.confidenceWeight = 0.5;
    diagnosticMatrixOptions.continuityWeight = 0.25;
    ats::MorphCorrespondenceRecord previousDiagnosticMatch;
    previousDiagnosticMatch.kind = ats::MorphCorrespondenceKind::Matched;
    previousDiagnosticMatch.sourcePartialIndex = 140;
    previousDiagnosticMatch.targetPartialIndex = 999;
    diagnosticMatrixOptions.previousFrameCorrespondence = { previousDiagnosticMatch };
    const auto diagnosticMatrix = ats::buildAssignmentMatrixCorrespondence(
        { { 140, 0, 100.0, 0.5, 0, ats::MorphBandAssignmentKind::Assigned, 0.5 } },
        { { 240, 0, 101.0, 0.5, 0, ats::MorphBandAssignmentKind::Assigned, 0.25 } },
        diagnosticMatrixOptions);
    if (diagnosticMatrix.records.size() != 1
        || !diagnosticMatrix.records[0].hasCostDiagnostics
        || diagnosticMatrix.records[0].confidenceCost <= 0.0
        || diagnosticMatrix.records[0].continuityCost <= 0.0
        || !approxEqual(diagnosticMatrix.records[0].totalCost,
                        diagnosticMatrix.records[0].logFrequencyCost
                            + diagnosticMatrix.records[0].amplitudeRankCost
                            + diagnosticMatrix.records[0].confidenceCost
                            + diagnosticMatrix.records[0].continuityCost,
                        1.0e-12))
    {
        return fail("assignment-matrix matcher did not include confidence and continuity costs");
    }

    ats::MorphRecipe greedyRecipe = recipe;
    greedyRecipe.correspondenceRecords = diagnosticMatrix.records;
    const std::string serializedGreedyRecipe = ats::serializeMorphRecipe(greedyRecipe);
    if (serializedGreedyRecipe.find("correspondence.0.has_cost_diagnostics=1\n") == std::string::npos
        || serializedGreedyRecipe.find("correspondence.0.confidence_cost=") == std::string::npos
        || serializedGreedyRecipe.find("correspondence.0.continuity_cost=") == std::string::npos
        || serializedGreedyRecipe.find("correspondence.0.total_cost=") == std::string::npos)
    {
        return fail("correspondence cost diagnostics were not serialized");
    }
    if (!ats::deserializeMorphRecipe(serializedGreedyRecipe, parsed, &error) || parsed != greedyRecipe)
        return fail("correspondence cost diagnostics did not round trip");

    const std::vector<std::vector<ats::MorphCorrespondenceRecord>> sustainedFrames {
        ats::buildGreedyNearestCorrespondence(
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 100, 0, 440.0, 0.7, true },
                                                         { 101, 0, 445.0, 0.6, true },
                                                     },
                                                     0.0),
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 200, 0, 440.2, 0.7, true },
                                                         { 201, 0, 444.8, 0.6, true },
                                                     },
                                                     0.0)),
        ats::buildGreedyNearestCorrespondence(
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 100, 1, 440.1, 0.7, true },
                                                         { 101, 1, 445.1, 0.6, true },
                                                     },
                                                     0.0),
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 200, 1, 440.3, 0.7, true },
                                                         { 201, 1, 444.9, 0.6, true },
                                                     },
                                                     0.0)),
        ats::buildGreedyNearestCorrespondence(
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 100, 2, 440.2, 0.7, true },
                                                         { 101, 2, 445.2, 0.6, true },
                                                     },
                                                     0.0),
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 200, 2, 440.4, 0.7, true },
                                                         { 201, 2, 445.0, 0.6, true },
                                                     },
                                                     0.0)),
    };
    const auto sustainedChurn = ats::calculateCorrespondenceChurnMetrics(sustainedFrames);
    if (sustainedChurn.frameTransitionCount != 2 || sustainedChurn.comparedMatchCount != 4
        || sustainedChurn.changedMatchCount != 0 || sustainedChurn.churnRate != 0.0)
    {
        return fail("sustained correspondence fixture should have zero churn");
    }
    if (ats::smoothCorrespondenceFrames(sustainedFrames) != sustainedFrames)
        return fail("sustained correspondence smoothing should leave stable matches unchanged");

    const std::vector<std::vector<ats::MorphCorrespondenceRecord>> vibratoFrames {
        ats::buildGreedyNearestCorrespondence(
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 110, 0, 440.0, 0.7, true },
                                                         { 111, 0, 445.0, 0.6, true },
                                                     },
                                                     0.0),
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 210, 0, 440.2, 0.7, true },
                                                         { 211, 0, 444.8, 0.6, true },
                                                     },
                                                     0.0)),
        ats::buildGreedyNearestCorrespondence(
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 110, 1, 442.0, 0.7, true },
                                                         { 111, 1, 443.0, 0.6, true },
                                                     },
                                                     0.0),
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 210, 1, 443.1, 0.7, true },
                                                         { 211, 1, 441.9, 0.6, true },
                                                     },
                                                     0.0)),
        ats::buildGreedyNearestCorrespondence(
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 110, 2, 440.0, 0.7, true },
                                                         { 111, 2, 445.0, 0.6, true },
                                                     },
                                                     0.0),
            ats::assignPartialFramesToFrequencyBands(comparisonGrid,
                                                     {
                                                         { 210, 2, 440.2, 0.7, true },
                                                         { 211, 2, 444.8, 0.6, true },
                                                     },
                                                     0.0)),
    };
    const auto rawVibratoChurn = ats::calculateCorrespondenceChurnMetrics(vibratoFrames);
    if (rawVibratoChurn.changedMatchCount != 4 || !approxEqual(rawVibratoChurn.churnRate, 1.0, 1.0e-12))
        return fail("vibrato correspondence fixture should expose raw target flapping");

    ats::MorphCorrespondenceSmoothingOptions smoothingOptions;
    smoothingOptions.hysteresisCostMargin = 0.01;
    const auto smoothedVibratoFrames = ats::smoothCorrespondenceFrames(vibratoFrames, smoothingOptions);
    const auto smoothedVibratoChurn = ats::calculateCorrespondenceChurnMetrics(smoothedVibratoFrames);
    if (smoothedVibratoChurn.changedMatchCount != 0 || smoothedVibratoChurn.churnRate != 0.0)
        return fail("hysteresis smoothing should remove vibrato correspondence flapping");

    bool keptPreviousVibratoPairs = false;
    for (const auto& record : smoothedVibratoFrames[1])
    {
        keptPreviousVibratoPairs = keptPreviousVibratoPairs
                                   || (record.kind == ats::MorphCorrespondenceKind::Matched
                                       && record.sourcePartialIndex == 110
                                       && record.targetPartialIndex == 210
                                       && record.hasCostDiagnostics);
    }
    if (!keptPreviousVibratoPairs)
        return fail("hysteresis smoothing did not preserve previous vibrato pairing");

    ats::MorphUnmatchedPartialPolicy unmatchedPolicy = ats::MorphUnmatchedPartialPolicy::FadeOut;
    if (!ats::morphUnmatchedPartialPolicyFromName("merge_into_residual", unmatchedPolicy)
        || unmatchedPolicy != ats::MorphUnmatchedPartialPolicy::MergeIntoResidual
        || std::string(ats::morphUnmatchedPartialPolicyName(ats::MorphUnmatchedPartialPolicy::Hold)) != "hold")
    {
        return fail("unmatched partial policy lookup failed");
    }

    ats::MorphUnmatchedPartialPolicies policySet;
    policySet.sourcePolicy = ats::MorphUnmatchedPartialPolicy::FadeOut;
    policySet.targetPolicy = ats::MorphUnmatchedPartialPolicy::FadeIn;
    auto policyDecisions = ats::resolveUnmatchedPartialPolicies(correspondences, policySet);
    if (policyDecisions.size() != 2)
        return fail("unmatched policy resolution should skip matched correspondence records");
    if (policyDecisions[0].policy != ats::MorphUnmatchedPartialPolicy::FadeOut
        || !policyDecisions[0].fadesPartial
        || policyDecisions[0].kind != ats::MorphCorrespondenceKind::UnmatchedSource
        || policyDecisions[0].partialIndex != 20)
    {
        return fail("fade-out policy did not resolve the unmatched source partial");
    }
    if (policyDecisions[1].policy != ats::MorphUnmatchedPartialPolicy::FadeIn
        || !policyDecisions[1].fadesPartial
        || policyDecisions[1].kind != ats::MorphCorrespondenceKind::UnmatchedTarget
        || policyDecisions[1].partialIndex != 33)
    {
        return fail("fade-in policy did not resolve the unmatched target partial");
    }

    policySet.sourcePolicy = ats::MorphUnmatchedPartialPolicy::Hold;
    policySet.targetPolicy = ats::MorphUnmatchedPartialPolicy::Hold;
    policyDecisions = ats::resolveUnmatchedPartialPolicies(correspondences, policySet);
    if (!policyDecisions[0].holdsPartial || !policyDecisions[1].holdsPartial)
        return fail("hold policy did not mark unmatched partials as held");

    policySet.sourcePolicy = ats::MorphUnmatchedPartialPolicy::MergeIntoResidual;
    policySet.targetPolicy = ats::MorphUnmatchedPartialPolicy::MergeIntoResidual;
    policyDecisions = ats::resolveUnmatchedPartialPolicies(correspondences, policySet);
    if (!policyDecisions[0].routesToResidual || !policyDecisions[1].routesToResidual
        || !approxEqual(policyDecisions[0].residualPower, 0.64, 1.0e-12)
        || !approxEqual(policyDecisions[1].residualPower, 0.04, 1.0e-12))
    {
        return fail("merge-into-residual policy did not preserve deterministic unmatched partial power");
    }

    policySet.sourcePolicy = ats::MorphUnmatchedPartialPolicy::Discard;
    policySet.targetPolicy = ats::MorphUnmatchedPartialPolicy::Discard;
    policySet.discardOptIn = false;
    if (!ats::unmatchedPartialPoliciesRequireDiscardOptIn(policySet)
        || ats::validateUnmatchedPartialPolicies(policySet, &error))
    {
        return fail("discard unmatched policy must require explicit opt-in");
    }
    ats::MorphRecipe invalidDiscardRecipe = recipe;
    invalidDiscardRecipe.unmatchedPartialPolicies = policySet;
    if (ats::deserializeMorphRecipe(ats::serializeMorphRecipe(invalidDiscardRecipe), parsed, &error))
        return fail("discard unmatched policy was accepted without explicit opt-in");

    policySet.discardOptIn = true;
    if (!ats::validateUnmatchedPartialPolicies(policySet, &error))
        return fail("discard unmatched policy should be valid after explicit opt-in: " + error);
    policyDecisions = ats::resolveUnmatchedPartialPolicies(correspondences, policySet);
    if (!policyDecisions[0].discardsPartial || !policyDecisions[1].discardsPartial)
        return fail("discard policy did not mark unmatched partials as discarded");
    ats::MorphRecipe discardRecipe = recipe;
    discardRecipe.unmatchedPartialPolicies = policySet;
    const std::string serializedDiscardRecipe = ats::serializeMorphRecipe(discardRecipe);
    if (serializedDiscardRecipe.find("unmatched.source_policy=discard\n") == std::string::npos
        || serializedDiscardRecipe.find("unmatched.target_policy=discard\n") == std::string::npos
        || serializedDiscardRecipe.find("unmatched.discard_opt_in=1\n") == std::string::npos)
    {
        return fail("discard unmatched policy was not visible in recipe metadata");
    }
    if (!ats::deserializeMorphRecipe(serializedDiscardRecipe, parsed, &error) || parsed != discardRecipe)
        return fail("discard unmatched policy recipe did not round trip after opt-in");

    const ats::MorphCompatibilityReport validReport = ats::validateMorphSources(recipe);
    if (!validReport.isCompatible())
        return fail("valid recipe sources should be compatible");

    ats::MorphRecipe warningRecipe = recipe;
    warningRecipe.sourceA.hasPhase = false;
    warningRecipe.sourceB.hasResidual = false;
    const ats::MorphCompatibilityReport warningReport = ats::validateMorphSources(warningRecipe);
    bool sawMissingPhase = false;
    bool sawMissingResidual = false;
    for (const auto& issue : warningReport.issues)
    {
        if (issue.code == ats::MorphCompatibilityCode::MissingPhase
            && issue.severity == ats::MorphCompatibilitySeverity::Warning)
        {
            sawMissingPhase = true;
        }
        if (issue.code == ats::MorphCompatibilityCode::MissingResidual
            && issue.severity == ats::MorphCompatibilitySeverity::Warning)
        {
            sawMissingResidual = true;
        }
    }
    if (!warningReport.isCompatible() || !sawMissingPhase || !sawMissingResidual)
        return fail("missing optional phase/residual data should produce deterministic warnings");

    ats::MorphRecipe invalidRecipe = recipe;
    invalidRecipe.sourceB.sampleRate = 44100.0;
    invalidRecipe.sourceB.channelCount = 2;
    invalidRecipe.sourceA.partialCount = 0;
    invalidRecipe.sourceA.highestFrequencyHz = 20.0;
    invalidRecipe.sourceB.lowestFrequencyHz = 20000.0;
    invalidRecipe.sourceB.highestFrequencyHz = 22000.0;
    invalidRecipe.sourceB.frameHopSeconds = 0.02;
    const ats::MorphCompatibilityReport invalidReport = ats::validateMorphSources(invalidRecipe);
    if (invalidReport.isCompatible())
        return fail("invalid recipe sources should not be compatible");

    bool sawSampleRateMismatch = false;
    bool sawChannelMismatch = false;
    bool sawInvalidPartialCount = false;
    bool sawInvalidFrequencyBounds = false;
    bool sawIncompatibleFrameGrid = false;
    for (const auto& issue : invalidReport.issues)
    {
        sawSampleRateMismatch = sawSampleRateMismatch || issue.code == ats::MorphCompatibilityCode::SampleRateMismatch;
        sawChannelMismatch = sawChannelMismatch || issue.code == ats::MorphCompatibilityCode::ChannelCountMismatch;
        sawInvalidPartialCount = sawInvalidPartialCount || issue.code == ats::MorphCompatibilityCode::InvalidPartialCount;
        sawInvalidFrequencyBounds = sawInvalidFrequencyBounds
                                    || issue.code == ats::MorphCompatibilityCode::InvalidFrequencyBounds;
        sawIncompatibleFrameGrid = sawIncompatibleFrameGrid
                                   || issue.code == ats::MorphCompatibilityCode::IncompatibleFrameGrid;
    }
    if (!sawSampleRateMismatch || !sawChannelMismatch || !sawInvalidPartialCount || !sawInvalidFrequencyBounds
        || !sawIncompatibleFrameGrid)
    {
        return fail("expected deterministic compatibility error/warning codes were not emitted");
    }

    if (std::string(ats::morphCompatibilityCodeName(ats::MorphCompatibilityCode::SampleRateMismatch))
        != "sample_rate_mismatch")
    {
        return fail("compatibility code name lookup failed");
    }

    std::string broken = serialized;
    broken.replace(0, std::string("ATS_MORPH_RECIPE 1").size(), "ATS_MORPH_RECIPE 99");
    if (ats::deserializeMorphRecipe(broken, parsed, &error))
        return fail("unsupported schema version was accepted");

    std::cout << "ok: morph recipe schema round trip validated\n";
    return 0;
}

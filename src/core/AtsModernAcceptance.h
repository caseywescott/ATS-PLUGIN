#pragma once

#include "AtsModernModel.h"
#include "AtsModernSynth.h"

#include <string>
#include <vector>

namespace ats
{

struct ModernAtsAudioMetrics
{
    double referenceRms = 0.0;
    double candidateRms = 0.0;
    double rmsError = 0.0;
    double normalizedRmsError = 0.0;
    double peakError = 0.0;
    double spectralDistanceDb = 0.0;
};

struct ModernAtsContinuityMetrics
{
    int partialCount = 0;
    int expectedFrameCount = 0;
    int activeFrameCount = 0;
    int interpolatedFrameCount = 0;
    int rejectedFrameCount = 0;
    int lifecycleBreakCount = 0;
    double continuityScore = 0.0;
    double meanConfidence = 0.0;
};

struct ModernAtsResidualMetrics
{
    double partialNoiseEnergy = 0.0;
    double orphanResidualEnergy = 0.0;
    double totalResidualEnergy = 0.0;
    double orphanResidualRatio = 0.0;
};

struct ModernAtsTransientMetrics
{
    int markerCount = 0;
    int bufferCount = 0;
    double totalWindowSeconds = 0.0;
    double bufferEnergy = 0.0;
    double attackSharpness = 0.0;
    double maxBufferStep = 0.0;
};

struct ModernAtsTrackerReference
{
    int partialId = 0;
    std::vector<double> expectedFrequencyHz;
    double toleranceHz = 1.0;
};

struct ModernAtsTrackerFixtureMetrics
{
    std::string fixtureId;
    ModernAtsContinuityMetrics continuity;
    int identityErrors = 0;
    double meanAbsFrequencyErrorHz = 0.0;
};

struct ModernAtsAcceptanceThresholds
{
    double maxClassicNormalizedRmsError = 1.0e-4;
    double maxClassicSpectralDistanceDb = 0.10;
    int maxCrossingIdentityErrorsOverClassic = 0;
    int maxSustainedIdentityErrorsOverClassic = 0;
    double maxSustainedContinuityRegression = 0.02;
    double minTransientSharpnessImprovement = 0.05;
    double maxTransientClickStep = 0.95;
    double minResidualEnergyRatio = 0.99;
    double maxRenderCpuRatio = 1.0;
};

struct ModernAtsAcceptanceCheck
{
    std::string id;
    bool passed = false;
    double observed = 0.0;
    double threshold = 0.0;
    std::string detail;
};

struct ModernAtsAcceptanceReport
{
    std::vector<ModernAtsAcceptanceCheck> checks;

    bool passed() const;
    std::vector<std::string> failureMessages() const;
};

struct ModernAtsAcceptanceInputs
{
    ModernAtsAudioMetrics classicParityAudio;
    ModernAtsTrackerFixtureMetrics classicCrossingTracker;
    ModernAtsTrackerFixtureMetrics modernCrossingTracker;
    ModernAtsTrackerFixtureMetrics classicSustainedTracker;
    ModernAtsTrackerFixtureMetrics modernSustainedTracker;
    ModernAtsTransientMetrics baselineTransient;
    ModernAtsTransientMetrics protectedTransient;
    ModernAtsResidualMetrics residualMetrics;
    double expectedResidualEnergy = 0.0;
    bool morphRecipeRecallDeterministic = false;
    bool morphRenderDeterministic = false;
    bool renderSamplesFinite = false;
    ModernAtsRealtimeSafetyReport realtimeSafety;
    ModernAtsRenderStats renderStats;
};

ModernAtsAudioMetrics computeModernAtsAudioMetrics(const std::vector<float>& reference,
                                                   const std::vector<float>& candidate);
ModernAtsContinuityMetrics computeModernAtsContinuityMetrics(const ModernAtsModel& model);
ModernAtsResidualMetrics computeModernAtsResidualMetrics(const ModernAtsModel& model);
ModernAtsTransientMetrics computeModernAtsTransientMetrics(const ModernAtsModel& model);
ModernAtsTrackerFixtureMetrics computeModernAtsTrackerFixtureMetrics(
    const ModernAtsModel& model,
    const std::string& fixtureId,
    const std::vector<ModernAtsTrackerReference>& references);

ModernAtsAcceptanceReport evaluateModernAtsAcceptanceGates(
    const ModernAtsAcceptanceInputs& inputs,
    const ModernAtsAcceptanceThresholds& thresholds = ModernAtsAcceptanceThresholds {});

} // namespace ats

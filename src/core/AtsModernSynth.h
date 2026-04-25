#pragma once

#include "AtsModernModel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ats
{

struct ModernAtsSynthConfig
{
    int outputSampleRate = 48000;
    int hopSizeSamples = 0;
    double partialGain = 1.0;
    double orphanResidualGain = 1.0;
    double transientGain = 1.0;
    bool useStoredPhase = true;
    std::uint32_t deterministicNoiseSeed = 1;
    int transientCrossfadeSamples = 64;
    double denormalClamp = 1.0e-20;
};

struct ModernAtsRenderPlan
{
    const ModernAtsModel* model = nullptr;
    ModernAtsSynthConfig config;
    int outputSampleRate = 0;
    int hopSizeSamples = 0;
    int totalSamples = 0;
};

struct ModernAtsRealtimeSafetyReport
{
    bool renderIntoCallerOwnedBuffer = true;
    bool allocatesDuringRender = false;
    bool locksDuringRender = false;
    bool performsFileIoDuringRender = false;
    bool performsAnalysisDuringRender = false;
    std::string summary;
};

struct ModernAtsRenderStats
{
    int totalSamples = 0;
    std::size_t modelStateBytes = 0;
    std::size_t outputBytes = 0;
    double renderSeconds = 0.0;
    double cpuRatioAtRealtime = 0.0;
    float peakAbs = 0.0f;
};

bool prepareModernAtsRenderPlan(const ModernAtsModel& model,
                                const ModernAtsSynthConfig& config,
                                ModernAtsRenderPlan& plan,
                                std::string& errorMessage);

bool renderModernAtsPlanIntoBuffer(const ModernAtsRenderPlan& plan, float* output, int numSamples);
std::vector<float> renderModernAtsModel(const ModernAtsModel& model,
                                        const ModernAtsSynthConfig& config,
                                        ModernAtsRenderStats* stats = nullptr);

ModernAtsRealtimeSafetyReport checkModernAtsRealtimeSafety(const ModernAtsRenderPlan& plan);
bool hasDenormalOrNonFiniteSamples(const float* samples, int numSamples, float denormalThreshold = 1.0e-20f);

} // namespace ats

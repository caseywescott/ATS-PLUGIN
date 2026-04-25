#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ats
{

constexpr int kModernAtsSchemaVersion = 1;
constexpr int kModernAtsDefaultResidualBands = 25;

enum class ModernAtsAnalysisMode
{
    ClassicAts,
    ModernAts,
    Experimental
};

enum class ModernAtsTrackerMode
{
    ClassicRecursiveFrequencySmr,
    ModernAssignment,
    Experimental
};

enum class ModernAtsChannelPolicy
{
    MonoInput,
    DownmixToMonoAverage,
    PerChannel,
    MidSide
};

enum class ModernAtsFrameReason
{
    Matched,
    Born,
    Slept,
    Interpolated,
    Pruned,
    Rejected
};

enum class ModernAtsLifecycleKind
{
    Birth,
    Sleep,
    Wake,
    Death,
    GapFill,
    Pruned
};

const char* toString(ModernAtsAnalysisMode mode);
const char* toString(ModernAtsTrackerMode mode);
const char* toString(ModernAtsChannelPolicy policy);
const char* toString(ModernAtsFrameReason reason);
const char* toString(ModernAtsLifecycleKind kind);

ModernAtsChannelPolicy defaultModernAtsChannelPolicyForInput(int inputChannelCount);

struct ModernAtsHeader
{
    int schemaVersion = kModernAtsSchemaVersion;
    ModernAtsAnalysisMode analysisMode = ModernAtsAnalysisMode::ClassicAts;
    ModernAtsTrackerMode trackerMode = ModernAtsTrackerMode::ClassicRecursiveFrequencySmr;
    int sampleRate = 0;
    int hopSizeSamples = 0;
    int windowSizeSamples = 0;
    int inputChannelCount = 1;
    ModernAtsChannelPolicy channelPolicy = ModernAtsChannelPolicy::MonoInput;
    std::string parameterHash;
    std::string sourceHash;
};

struct ModernAtsPartialFrame
{
    double amplitude = 0.0;
    double frequencyHz = 0.0;
    double phaseRadians = 0.0;
    double smrDb = 0.0;
    double noiseEnergy = 0.0;
    double confidence = 1.0;
    ModernAtsFrameReason reason = ModernAtsFrameReason::Matched;
};

struct ModernAtsPartial
{
    int id = 0;
    std::string label;
    int channel = 0;
    std::vector<ModernAtsPartialFrame> frames;
};

struct ModernAtsFrame
{
    double timeSeconds = 0.0;
    std::vector<int> activePartialIds;
    std::vector<double> orphanResidualBandEnergy;
};

struct ModernAtsTrackLifecycleEvent
{
    int partialId = 0;
    int frameIndex = 0;
    ModernAtsLifecycleKind kind = ModernAtsLifecycleKind::Birth;
    std::string detail;
};

struct ModernAtsAssignmentCost
{
    int frameIndex = 0;
    int partialId = 0;
    int peakIndex = 0;
    double frequencyCost = 0.0;
    double amplitudeCost = 0.0;
    double smrCost = 0.0;
    double slopeCost = 0.0;
    double birthDeathCost = 0.0;
    double spuriousPeakCost = 0.0;
    double totalCost = 0.0;
};

struct ModernAtsTransientMarker
{
    int frameIndex = 0;
    int bufferIndex = -1;
    double onsetSeconds = 0.0;
    double windowStartSeconds = 0.0;
    double windowEndSeconds = 0.0;
    double gain = 1.0;
};

struct ModernAtsTransientBuffer
{
    int markerIndex = 0;
    int sampleRate = 0;
    double gain = 1.0;
    std::vector<float> samples;
};

struct ModernAtsModel
{
    ModernAtsHeader header;
    std::vector<ModernAtsFrame> frames;
    std::vector<ModernAtsPartial> partials;
    std::vector<ModernAtsTrackLifecycleEvent> trackLifecycle;
    std::vector<ModernAtsAssignmentCost> assignmentCosts;
    std::vector<ModernAtsTransientMarker> transients;
    std::vector<ModernAtsTransientBuffer> transientBuffers;
    std::vector<std::string> warnings;
};

bool validateModernAtsModel(const ModernAtsModel& model, std::string& errorMessage);
std::string serializeModernAtsModelToJson(const ModernAtsModel& model);
std::size_t estimateModernAtsModelStateBytes(const ModernAtsModel& model);

} // namespace ats

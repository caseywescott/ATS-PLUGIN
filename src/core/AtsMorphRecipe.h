#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ats
{

constexpr int kMorphRecipeSchemaVersion = 1;

enum class MorphLane
{
    Frequency = 0,
    Amplitude,
    ResidualNoise,
    Time,
    Transient,
    Phase,
    Count
};

enum class MorphFrequencyGridType
{
    Bark,
    Mel
};

struct MorphFrequencyGrid
{
    MorphFrequencyGridType type = MorphFrequencyGridType::Bark;
    int bandCount = 25;
    double lowestFrequencyHz = 0.0;
    double highestFrequencyHz = 22050.0;
    std::vector<double> edgeFrequenciesHz;

    [[nodiscard]] bool operator==(const MorphFrequencyGrid& other) const;
    [[nodiscard]] bool operator!=(const MorphFrequencyGrid& other) const { return !(*this == other); }
};

enum class MorphBandAssignmentKind
{
    Assigned,
    Silent,
    Inactive,
    OutOfRangeLow,
    OutOfRangeHigh,
    InvalidGrid
};

enum class MorphCorrespondenceKind
{
    Matched,
    UnmatchedSource,
    UnmatchedTarget
};

enum class MorphUnmatchedPartialPolicy
{
    FadeOut,
    FadeIn,
    Hold,
    MergeIntoResidual,
    Discard
};

enum class MorphCorrespondenceStrategy
{
    FrequencyRank,
    GreedyNearest,
    AssignmentMatrix
};

enum class MorphFrequencyInterpolationDomain
{
    LogFrequency,
    BarkCoordinate
};

enum class MorphAmplitudeInterpolationDomain
{
    Power,
    Decibels
};

enum class MorphCurveShape
{
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut
};

enum class MorphPhaseStrategy
{
    OscillatorContinuation,
    SourceADonor,
    SourceBDonor,
    RelativeClusterPreservation,
    DirectBlend
};

enum class MorphTransientPolicy
{
    SourceA,
    SourceB,
    Crossfade,
    SustainedOnlyAfterAttack
};

struct MorphPartialFrame
{
    int partialIndex = -1;
    int frameIndex = -1;
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    bool active = true;
};

struct MorphBandAssignment
{
    int partialIndex = -1;
    int frameIndex = -1;
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    int bandIndex = -1;
    MorphBandAssignmentKind kind = MorphBandAssignmentKind::Inactive;
    double confidence = 1.0;
};

struct MorphPartialTrack
{
    int partialIndex = -1;
    std::vector<MorphPartialFrame> frames;
};

struct MorphTrackCentroidAssignment
{
    int partialIndex = -1;
    double centroidFrequencyHz = 0.0;
    double totalAmplitude = 0.0;
    int contributingFrameCount = 0;
    int bandIndex = -1;
    MorphBandAssignmentKind kind = MorphBandAssignmentKind::Inactive;
};

struct MorphSourceMetadata
{
    std::string id;
    std::string hash;
    std::string name;
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
    double frameHopSeconds = 0.0;
    double lowestFrequencyHz = 0.0;
    double highestFrequencyHz = 0.0;
    int channelCount = 1;
    int frameCount = 0;
    int partialCount = 0;
    bool hasPhase = false;
    bool hasResidual = false;

    [[nodiscard]] bool operator==(const MorphSourceMetadata& other) const;
    [[nodiscard]] bool operator!=(const MorphSourceMetadata& other) const { return !(*this == other); }
};

struct MorphLaneSetting
{
    bool linkedToMainMorph = true;
    double amount = 0.0;
    MorphCurveShape curve = MorphCurveShape::Linear;

    [[nodiscard]] bool operator==(const MorphLaneSetting& other) const;
    [[nodiscard]] bool operator!=(const MorphLaneSetting& other) const { return !(*this == other); }
};

using MorphLaneSettings = std::array<MorphLaneSetting, static_cast<size_t>(MorphLane::Count)>;
using MorphLaneAmounts = std::array<double, static_cast<size_t>(MorphLane::Count)>;

struct MorphUnmatchedPartialPolicies
{
    MorphUnmatchedPartialPolicy sourcePolicy = MorphUnmatchedPartialPolicy::FadeOut;
    MorphUnmatchedPartialPolicy targetPolicy = MorphUnmatchedPartialPolicy::FadeIn;
    bool discardOptIn = false;

    [[nodiscard]] bool operator==(const MorphUnmatchedPartialPolicies& other) const;
    [[nodiscard]] bool operator!=(const MorphUnmatchedPartialPolicies& other) const { return !(*this == other); }
};

struct MorphGreedyNearestOptions
{
    double amplitudeRankWeight = 0.0;

    [[nodiscard]] bool operator==(const MorphGreedyNearestOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphGreedyNearestOptions& other) const { return !(*this == other); }
};

struct MorphCorrespondenceSmoothingOptions
{
    double hysteresisCostMargin = 0.0;

    [[nodiscard]] bool operator==(const MorphCorrespondenceSmoothingOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphCorrespondenceSmoothingOptions& other) const { return !(*this == other); }
};

struct MorphFrequencyInterpolationOptions
{
    MorphFrequencyInterpolationDomain domain = MorphFrequencyInterpolationDomain::LogFrequency;
    bool clampToGridBounds = true;

    [[nodiscard]] bool operator==(const MorphFrequencyInterpolationOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphFrequencyInterpolationOptions& other) const { return !(*this == other); }
};

struct MorphAmplitudeInterpolationOptions
{
    MorphAmplitudeInterpolationDomain domain = MorphAmplitudeInterpolationDomain::Power;
    double decibelFloor = -120.0;

    [[nodiscard]] bool operator==(const MorphAmplitudeInterpolationOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphAmplitudeInterpolationOptions& other) const { return !(*this == other); }
};

struct MorphRenderGainSettings
{
    double partialGain = 1.0;
    double partialNoiseGain = 1.0;
    double orphanResidualGain = 1.0;

    [[nodiscard]] bool operator==(const MorphRenderGainSettings& other) const;
    [[nodiscard]] bool operator!=(const MorphRenderGainSettings& other) const { return !(*this == other); }
};

struct MorphPhaseSettings
{
    MorphPhaseStrategy strategy = MorphPhaseStrategy::OscillatorContinuation;

    [[nodiscard]] bool operator==(const MorphPhaseSettings& other) const;
    [[nodiscard]] bool operator!=(const MorphPhaseSettings& other) const { return !(*this == other); }
};

struct MorphPhaseInput
{
    MorphCorrespondenceKind kind = MorphCorrespondenceKind::Matched;
    double sourcePhaseRadians = 0.0;
    double targetPhaseRadians = 0.0;
    bool hasSourcePhase = false;
    bool hasTargetPhase = false;

    [[nodiscard]] bool operator==(const MorphPhaseInput& other) const;
    [[nodiscard]] bool operator!=(const MorphPhaseInput& other) const { return !(*this == other); }
};

struct MorphPhaseResolution
{
    double phaseRadians = 0.0;
    bool valid = false;
    bool usesRendererContinuation = false;
    bool usedSourcePhase = false;
    bool usedTargetPhase = false;
    bool interpolatedPhase = false;
    bool isBirth = false;
    bool isDeath = false;

    [[nodiscard]] bool operator==(const MorphPhaseResolution& other) const;
    [[nodiscard]] bool operator!=(const MorphPhaseResolution& other) const { return !(*this == other); }
};

struct MorphLifecycleFadeSettings
{
    int birthFadeFrames = 4;
    int deathFadeFrames = 4;
    double maxGainStepPerFrame = 0.5;
    MorphCurveShape curve = MorphCurveShape::Linear;

    [[nodiscard]] bool operator==(const MorphLifecycleFadeSettings& other) const;
    [[nodiscard]] bool operator!=(const MorphLifecycleFadeSettings& other) const { return !(*this == other); }
};

struct MorphLifecycleFadeResult
{
    MorphCorrespondenceKind kind = MorphCorrespondenceKind::Matched;
    int lifecycleFrameIndex = 0;
    double amplitudeScale = 1.0;
    double nextAmplitudeScale = 1.0;
    double frameGainStep = 0.0;
    bool isBirth = false;
    bool isDeath = false;
    bool allowsPhaseReset = false;
    bool clickRisk = false;
    bool zipperRisk = false;

    [[nodiscard]] bool operator==(const MorphLifecycleFadeResult& other) const;
    [[nodiscard]] bool operator!=(const MorphLifecycleFadeResult& other) const { return !(*this == other); }
};

struct MorphTransientMarker
{
    double timeSeconds = 0.0;
    double strength = 1.0;
    int frameIndex = -1;

    [[nodiscard]] bool operator==(const MorphTransientMarker& other) const;
    [[nodiscard]] bool operator!=(const MorphTransientMarker& other) const { return !(*this == other); }
};

struct MorphTransientSettings
{
    MorphTransientPolicy policy = MorphTransientPolicy::SustainedOnlyAfterAttack;
    double protectionWindowSeconds = 0.05;
    double maxOnsetAlignmentOffsetSeconds = 0.02;

    [[nodiscard]] bool operator==(const MorphTransientSettings& other) const;
    [[nodiscard]] bool operator!=(const MorphTransientSettings& other) const { return !(*this == other); }
};

struct MorphTransientAlignmentWarning
{
    int sourceMarkerIndex = -1;
    int targetMarkerIndex = -1;
    double sourceTimeSeconds = 0.0;
    double targetTimeSeconds = 0.0;
    double offsetSeconds = 0.0;
    bool exceedsTolerance = false;

    [[nodiscard]] bool operator==(const MorphTransientAlignmentWarning& other) const;
    [[nodiscard]] bool operator!=(const MorphTransientAlignmentWarning& other) const { return !(*this == other); }
};

struct MorphTransientAnalysisReport
{
    int comparedMarkerCount = 0;
    int warningCount = 0;
    std::vector<MorphTransientAlignmentWarning> warnings;

    [[nodiscard]] bool operator==(const MorphTransientAnalysisReport& other) const;
    [[nodiscard]] bool operator!=(const MorphTransientAnalysisReport& other) const { return !(*this == other); }
};

struct MorphCorrespondenceRecord
{
    int bandIndex = -1;
    int rank = -1;
    int sourcePartialIndex = -1;
    int sourceFrameIndex = -1;
    double sourceFrequencyHz = 0.0;
    double sourceAmplitude = 0.0;
    int targetPartialIndex = -1;
    int targetFrameIndex = -1;
    double targetFrequencyHz = 0.0;
    double targetAmplitude = 0.0;
    MorphCorrespondenceKind kind = MorphCorrespondenceKind::Matched;
    bool hasCostDiagnostics = false;
    double logFrequencyCost = 0.0;
    double amplitudeRankCost = 0.0;
    double confidenceCost = 0.0;
    double continuityCost = 0.0;
    double totalCost = 0.0;

    [[nodiscard]] bool operator==(const MorphCorrespondenceRecord& other) const;
    [[nodiscard]] bool operator!=(const MorphCorrespondenceRecord& other) const { return !(*this == other); }
};

struct MorphAssignmentMatrixOptions
{
    bool advancedStrategyEnabled = false;
    int maxCandidatesPerBand = 8;
    double amplitudeRankWeight = 0.0;
    double confidenceWeight = 0.0;
    double continuityWeight = 0.0;
    std::vector<MorphCorrespondenceRecord> previousFrameCorrespondence;

    [[nodiscard]] bool operator==(const MorphAssignmentMatrixOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphAssignmentMatrixOptions& other) const { return !(*this == other); }
};

struct MorphCorrespondenceChurnMetrics
{
    int frameTransitionCount = 0;
    int comparedMatchCount = 0;
    int changedMatchCount = 0;
    double churnRate = 0.0;

    [[nodiscard]] bool operator==(const MorphCorrespondenceChurnMetrics& other) const;
    [[nodiscard]] bool operator!=(const MorphCorrespondenceChurnMetrics& other) const { return !(*this == other); }
};

struct MorphAssignmentMatrixResult
{
    MorphCorrespondenceStrategy strategy = MorphCorrespondenceStrategy::GreedyNearest;
    std::vector<MorphCorrespondenceRecord> records;
    int sourceCandidateCount = 0;
    int targetCandidateCount = 0;
    int candidateEdgeCount = 0;
    int evaluatedAssignmentCount = 0;
    double totalMatchedCost = 0.0;
    bool wasCandidateBounded = false;

    [[nodiscard]] bool operator==(const MorphAssignmentMatrixResult& other) const;
    [[nodiscard]] bool operator!=(const MorphAssignmentMatrixResult& other) const { return !(*this == other); }
};

struct MorphFrequencyInterpolationResult
{
    double frequencyHz = 0.0;
    int sourceBandIndex = -1;
    int targetBandIndex = -1;
    int outputBandIndex = -1;
    bool valid = false;
    bool crossedBand = false;
    bool clampedToGridBounds = false;

    [[nodiscard]] bool operator==(const MorphFrequencyInterpolationResult& other) const;
    [[nodiscard]] bool operator!=(const MorphFrequencyInterpolationResult& other) const { return !(*this == other); }
};

struct MorphAmplitudeInterpolationResult
{
    double amplitude = 0.0;
    double power = 0.0;
    bool valid = false;
    bool usedDecibelFloor = false;

    [[nodiscard]] bool operator==(const MorphAmplitudeInterpolationResult& other) const;
    [[nodiscard]] bool operator!=(const MorphAmplitudeInterpolationResult& other) const { return !(*this == other); }
};

struct MorphBandEnergySnapshot
{
    int bandIndex = -1;
    double sourceSinusoidalPower = 0.0;
    double targetSinusoidalPower = 0.0;
    double outputSinusoidalPower = 0.0;
    double outputResidualPower = 0.0;
    double expectedOutputPower = 0.0;
    double powerDelta = 0.0;
    bool exceedsTolerance = false;

    [[nodiscard]] bool operator==(const MorphBandEnergySnapshot& other) const;
    [[nodiscard]] bool operator!=(const MorphBandEnergySnapshot& other) const { return !(*this == other); }
};

struct MorphBandEnergyOptions
{
    double morphAmount = 0.0;
    double powerDeltaTolerance = 1.0e-6;
    MorphUnmatchedPartialPolicies unmatchedPolicies;
    MorphAmplitudeInterpolationOptions amplitudeInterpolation;

    [[nodiscard]] bool operator==(const MorphBandEnergyOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphBandEnergyOptions& other) const { return !(*this == other); }
};

struct MorphBandEnergyReport
{
    std::vector<MorphBandEnergySnapshot> bands;
    int warningBandCount = 0;

    [[nodiscard]] bool operator==(const MorphBandEnergyReport& other) const;
    [[nodiscard]] bool operator!=(const MorphBandEnergyReport& other) const { return !(*this == other); }
};

struct MorphResidualNoiseBand
{
    int bandIndex = -1;
    double partialNoisePower = 0.0;
    double orphanResidualPower = 0.0;

    [[nodiscard]] bool operator==(const MorphResidualNoiseBand& other) const;
    [[nodiscard]] bool operator!=(const MorphResidualNoiseBand& other) const { return !(*this == other); }
};

struct MorphResidualNoiseOptions
{
    double morphAmount = 0.0;
    MorphRenderGainSettings gains;
    MorphAmplitudeInterpolationOptions interpolation;

    [[nodiscard]] bool operator==(const MorphResidualNoiseOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphResidualNoiseOptions& other) const { return !(*this == other); }
};

struct MorphResidualNoiseBandResult
{
    int bandIndex = -1;
    double sourcePartialNoisePower = 0.0;
    double targetPartialNoisePower = 0.0;
    double outputPartialNoisePower = 0.0;
    double sourceOrphanResidualPower = 0.0;
    double targetOrphanResidualPower = 0.0;
    double outputOrphanResidualPower = 0.0;
    double outputTotalNoisePower = 0.0;

    [[nodiscard]] bool operator==(const MorphResidualNoiseBandResult& other) const;
    [[nodiscard]] bool operator!=(const MorphResidualNoiseBandResult& other) const { return !(*this == other); }
};

struct MorphResidualNoiseReport
{
    std::vector<MorphResidualNoiseBandResult> bands;
    double outputPartialGain = 1.0;
    double outputPartialNoiseGain = 1.0;
    double outputOrphanResidualGain = 1.0;

    [[nodiscard]] bool operator==(const MorphResidualNoiseReport& other) const;
    [[nodiscard]] bool operator!=(const MorphResidualNoiseReport& other) const { return !(*this == other); }
};

struct MorphSnapshotOptions
{
    double morphAmount = 0.0;
    int lifecycleFrameIndex = 0;
    MorphFrequencyInterpolationOptions frequency;
    MorphAmplitudeInterpolationOptions amplitude;
    std::vector<MorphResidualNoiseBand> sourceResidualNoiseBands;
    std::vector<MorphResidualNoiseBand> targetResidualNoiseBands;

    [[nodiscard]] bool operator==(const MorphSnapshotOptions& other) const;
    [[nodiscard]] bool operator!=(const MorphSnapshotOptions& other) const { return !(*this == other); }
};

struct MorphSnapshotPartial
{
    int bandIndex = -1;
    int rank = -1;
    int sourcePartialIndex = -1;
    int sourceFrameIndex = -1;
    int targetPartialIndex = -1;
    int targetFrameIndex = -1;
    MorphCorrespondenceKind kind = MorphCorrespondenceKind::Matched;
    MorphUnmatchedPartialPolicy unmatchedPolicy = MorphUnmatchedPartialPolicy::FadeOut;
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    double power = 0.0;
    bool valid = false;
    bool routedToResidual = false;
    bool discarded = false;
    MorphFrequencyInterpolationResult frequencyInterpolation;
    MorphAmplitudeInterpolationResult amplitudeInterpolation;
    MorphPhaseResolution phase;
    MorphLifecycleFadeResult lifecycle;

    [[nodiscard]] bool operator==(const MorphSnapshotPartial& other) const;
    [[nodiscard]] bool operator!=(const MorphSnapshotPartial& other) const { return !(*this == other); }
};

struct MorphRenderSnapshot
{
    int recipeSchemaVersion = kMorphRecipeSchemaVersion;
    std::string engineVersion;
    double morphAmount = 0.0;
    MorphLaneAmounts laneAmounts{};
    bool requiresOfflinePreparation = true;
    bool safeForAudioThreadPlayback = true;
    std::vector<MorphSnapshotPartial> partials;
    MorphResidualNoiseReport residualNoise;
    MorphBandEnergyReport energy;
    MorphTransientAnalysisReport transientAlignment;

    [[nodiscard]] bool operator==(const MorphRenderSnapshot& other) const;
    [[nodiscard]] bool operator!=(const MorphRenderSnapshot& other) const { return !(*this == other); }
};

struct MorphSnapshotPlaybackSettings
{
    double sampleRate = 44100.0;
    double durationSeconds = 1.0;
    // Fallback carrier for residual power that is not associated with a band.
    // Banded residual/noise power renders through deterministic per-band noise carriers.
    double residualCarrierHz = 60.0;

    [[nodiscard]] bool operator==(const MorphSnapshotPlaybackSettings& other) const;
    [[nodiscard]] bool operator!=(const MorphSnapshotPlaybackSettings& other) const { return !(*this == other); }
};

struct MorphSnapshotPlaybackBuffer
{
    double sampleRate = 44100.0;
    std::vector<float> partialSamples;
    std::vector<float> residualSamples;

    [[nodiscard]] int getNumSamples() const;
    [[nodiscard]] bool operator==(const MorphSnapshotPlaybackBuffer& other) const;
    [[nodiscard]] bool operator!=(const MorphSnapshotPlaybackBuffer& other) const { return !(*this == other); }
};

struct MorphSnapshotPlaybackCursor
{
    int sampleIndex = 0;

    [[nodiscard]] bool operator==(const MorphSnapshotPlaybackCursor& other) const;
    [[nodiscard]] bool operator!=(const MorphSnapshotPlaybackCursor& other) const { return !(*this == other); }
};

struct MorphPerformanceSoftCaps
{
    int maxPartialsPerFrame = 512;
    int maxFrameCount = 4096;
    int maxCandidateEdgesPerBand = 2048;
    int maxEvaluatedAssignments = 4096;
    std::size_t maxAnalysisMemoryBytes = 64u * 1024u * 1024u;
    std::size_t maxSnapshotMemoryBytes = 8u * 1024u * 1024u;

    [[nodiscard]] bool operator==(const MorphPerformanceSoftCaps& other) const;
    [[nodiscard]] bool operator!=(const MorphPerformanceSoftCaps& other) const { return !(*this == other); }
};

struct MorphStrategyPerformanceEstimate
{
    MorphCorrespondenceStrategy strategy = MorphCorrespondenceStrategy::FrequencyRank;
    int sourceCandidateCount = 0;
    int targetCandidateCount = 0;
    int outputRecordCount = 0;
    int candidateEdgeCount = 0;
    int evaluatedAssignmentCount = 0;
    double relativeCpuCost = 0.0;
    bool exceedsCandidateEdgeSoftCap = false;
    bool exceedsAssignmentSoftCap = false;

    [[nodiscard]] bool operator==(const MorphStrategyPerformanceEstimate& other) const;
    [[nodiscard]] bool operator!=(const MorphStrategyPerformanceEstimate& other) const { return !(*this == other); }
};

struct MorphPerformanceProfile
{
    int frameCount = 0;
    int sourceCandidateCount = 0;
    int targetCandidateCount = 0;
    int bandCount = 0;
    std::size_t estimatedAnalysisMemoryBytes = 0;
    std::size_t estimatedSnapshotMemoryBytes = 0;
    bool exceedsPartialSoftCap = false;
    bool exceedsFrameSoftCap = false;
    bool exceedsAnalysisMemorySoftCap = false;
    bool exceedsSnapshotMemorySoftCap = false;
    int warningCount = 0;
    std::vector<MorphStrategyPerformanceEstimate> strategies;

    [[nodiscard]] bool operator==(const MorphPerformanceProfile& other) const;
    [[nodiscard]] bool operator!=(const MorphPerformanceProfile& other) const { return !(*this == other); }
};

struct MorphUnmatchedPartialDecision
{
    MorphUnmatchedPartialPolicy policy = MorphUnmatchedPartialPolicy::FadeOut;
    MorphCorrespondenceKind kind = MorphCorrespondenceKind::UnmatchedSource;
    int bandIndex = -1;
    int rank = -1;
    int partialIndex = -1;
    int frameIndex = -1;
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    double residualPower = 0.0;
    bool fadesPartial = false;
    bool holdsPartial = false;
    bool routesToResidual = false;
    bool discardsPartial = false;

    [[nodiscard]] bool operator==(const MorphUnmatchedPartialDecision& other) const;
    [[nodiscard]] bool operator!=(const MorphUnmatchedPartialDecision& other) const { return !(*this == other); }
};

struct MorphRecipeUnknownField
{
    std::string key;
    std::string value;

    [[nodiscard]] bool operator==(const MorphRecipeUnknownField& other) const;
    [[nodiscard]] bool operator!=(const MorphRecipeUnknownField& other) const { return !(*this == other); }
};

struct MorphRecipe
{
    int schemaVersion = kMorphRecipeSchemaVersion;
    std::string engineVersion = "morph-recipe-v1";
    MorphSourceMetadata sourceA;
    MorphSourceMetadata sourceB;
    MorphFrequencyGrid frequencyGrid;
    double defaultMorphAmount = 0.0;
    MorphLaneSettings lanes{};
    MorphRenderGainSettings outputGains;
    MorphPhaseSettings phase;
    MorphLifecycleFadeSettings lifecycle;
    MorphTransientSettings transient;
    std::vector<MorphTransientMarker> sourceATransients;
    std::vector<MorphTransientMarker> sourceBTransients;
    std::vector<MorphCorrespondenceRecord> correspondenceRecords;
    MorphUnmatchedPartialPolicies unmatchedPartialPolicies;
    std::vector<MorphRecipeUnknownField> unknownFields;

    [[nodiscard]] bool operator==(const MorphRecipe& other) const;
    [[nodiscard]] bool operator!=(const MorphRecipe& other) const { return !(*this == other); }
};

enum class MorphCompatibilitySeverity
{
    Warning,
    Error
};

enum class MorphCompatibilityCode
{
    MissingSourceId,
    MissingSourceHash,
    InvalidSampleRate,
    SampleRateMismatch,
    InvalidDuration,
    InvalidFrameCount,
    InvalidPartialCount,
    InvalidFrameHop,
    IncompatibleFrameGrid,
    InvalidChannelCount,
    ChannelCountMismatch,
    InvalidFrequencyBounds,
    NonOverlappingFrequencyBounds,
    MissingPhase,
    MissingResidual
};

struct MorphCompatibilityIssue
{
    MorphCompatibilitySeverity severity = MorphCompatibilitySeverity::Error;
    MorphCompatibilityCode code = MorphCompatibilityCode::MissingSourceId;
    std::string source;
    std::string message;
};

struct MorphCompatibilityReport
{
    std::vector<MorphCompatibilityIssue> issues;

    [[nodiscard]] bool isCompatible() const;
};

[[nodiscard]] const char* morphLaneName(MorphLane lane);
[[nodiscard]] bool morphLaneFromName(const std::string& name, MorphLane& lane);
[[nodiscard]] const char* morphCurveShapeName(MorphCurveShape shape);
[[nodiscard]] bool morphCurveShapeFromName(const std::string& name, MorphCurveShape& shape);
[[nodiscard]] const char* morphPhaseStrategyName(MorphPhaseStrategy strategy);
[[nodiscard]] bool morphPhaseStrategyFromName(const std::string& name, MorphPhaseStrategy& strategy);
[[nodiscard]] const char* morphTransientPolicyName(MorphTransientPolicy policy);
[[nodiscard]] bool morphTransientPolicyFromName(const std::string& name, MorphTransientPolicy& policy);
[[nodiscard]] const char* morphFrequencyGridTypeName(MorphFrequencyGridType type);
[[nodiscard]] bool morphFrequencyGridTypeFromName(const std::string& name, MorphFrequencyGridType& type);
[[nodiscard]] const char* morphBandAssignmentKindName(MorphBandAssignmentKind kind);
[[nodiscard]] const char* morphCorrespondenceKindName(MorphCorrespondenceKind kind);
[[nodiscard]] bool morphCorrespondenceKindFromName(const std::string& name, MorphCorrespondenceKind& kind);
[[nodiscard]] const char* morphUnmatchedPartialPolicyName(MorphUnmatchedPartialPolicy policy);
[[nodiscard]] bool morphUnmatchedPartialPolicyFromName(const std::string& name, MorphUnmatchedPartialPolicy& policy);
[[nodiscard]] const char* morphCompatibilityCodeName(MorphCompatibilityCode code);
[[nodiscard]] double applyMorphCurve(double amount, MorphCurveShape shape);
[[nodiscard]] double evaluateMorphLaneAmount(const MorphRecipe& recipe,
                                             MorphLane lane,
                                             double mainMorphAmount);
[[nodiscard]] MorphLaneAmounts evaluateMorphLaneAmounts(const MorphRecipe& recipe, double mainMorphAmount);
[[nodiscard]] MorphPhaseResolution resolveMorphPhase(const MorphPhaseInput& input,
                                                     double morphAmount,
                                                     const MorphPhaseSettings& settings = {});
[[nodiscard]] MorphLifecycleFadeResult evaluateMorphLifecycleFade(
    MorphCorrespondenceKind kind,
    int lifecycleFrameIndex,
    const MorphLifecycleFadeSettings& settings = {});
[[nodiscard]] MorphTransientAnalysisReport analyzeMorphTransientAlignment(
    const std::vector<MorphTransientMarker>& sourceMarkers,
    const std::vector<MorphTransientMarker>& targetMarkers,
    const MorphTransientSettings& settings = {});
[[nodiscard]] MorphFrequencyGrid makeBarkFrequencyGrid(int bandCount, double lowestFrequencyHz, double highestFrequencyHz);
[[nodiscard]] MorphFrequencyGrid makeMelFrequencyGrid(int bandCount, double lowestFrequencyHz, double highestFrequencyHz);
[[nodiscard]] int findFrequencyBandIndex(const MorphFrequencyGrid& grid, double frequencyHz);
[[nodiscard]] MorphBandAssignment assignPartialFrameToFrequencyBand(const MorphFrequencyGrid& grid,
                                                                    const MorphPartialFrame& frame,
                                                                    double silentAmplitudeThreshold = 0.0);
[[nodiscard]] std::vector<MorphBandAssignment> assignPartialFramesToFrequencyBands(
    const MorphFrequencyGrid& grid,
    const std::vector<MorphPartialFrame>& frames,
    double silentAmplitudeThreshold = 0.0);
[[nodiscard]] std::vector<MorphTrackCentroidAssignment> assignPartialTracksToCentroidBands(
    const MorphFrequencyGrid& grid,
    const std::vector<MorphPartialTrack>& tracks,
    double silentAmplitudeThreshold = 0.0);
// amplitudeRankWeight in [0,1] biases the within-band pairing toward amplitude order: 0 pairs
// purely by frequency rank (the original behavior), 1 pairs loudest source partial with loudest
// target partial, and intermediate values blend the two rankings. Weighting by amplitude keeps the
// dominant partials of A and B matched (so the loud, perceptually salient energy glides as one
// partial) instead of being split across mismatched ranks when the two spectra differ in count.
[[nodiscard]] std::vector<MorphCorrespondenceRecord> buildFrequencyRankCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    double amplitudeRankWeight = 0.0);
[[nodiscard]] std::vector<MorphCorrespondenceRecord> buildGreedyNearestCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphGreedyNearestOptions& options = {});
[[nodiscard]] MorphAssignmentMatrixResult buildAssignmentMatrixCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphAssignmentMatrixOptions& options = {});
// Re-pair k source partials to k target partials by nearest log-frequency, given each side's
// instantaneous log-frequencies at one moment in time. This is the per-frame counterpart of the
// static lifetime-centroid pairing: as partials glide and cross between frames, the correspondence
// is recomputed so the matched pair always joins the two partials that are actually closest in
// pitch *now*, rather than by their lifetime-average position. previousAssignment[i] is the target
// index source i held last frame; keeping it is preferred unless another target is closer by more
// than hysteresisMargin (in log-Hz), which suppresses frame-to-frame flicker between near-ties.
// Returns a permutation: result[i] is the target index assigned to source i. Sizes must match and
// be equal (k==k); on any size mismatch the previous assignment (or identity) is returned so the
// caller can fall back safely.
[[nodiscard]] std::vector<int> reassignCorrespondenceByLogFrequency(
    const std::vector<double>& sourceLogFrequencies,
    const std::vector<double>& targetLogFrequencies,
    const std::vector<int>& previousAssignment,
    double hysteresisMargin);
[[nodiscard]] MorphFrequencyInterpolationResult interpolateMorphFrequency(
    const MorphFrequencyGrid& grid,
    double sourceFrequencyHz,
    double targetFrequencyHz,
    double morphAmount,
    const MorphFrequencyInterpolationOptions& options = {});
[[nodiscard]] MorphAmplitudeInterpolationResult interpolateMorphAmplitude(
    double sourceAmplitude,
    double targetAmplitude,
    double morphAmount,
    const MorphAmplitudeInterpolationOptions& options = {});
[[nodiscard]] MorphBandEnergyReport calculateBandEnergyReport(
    const std::vector<MorphCorrespondenceRecord>& records,
    const MorphBandEnergyOptions& options = {});
[[nodiscard]] MorphResidualNoiseReport morphResidualNoiseBands(
    const std::vector<MorphResidualNoiseBand>& sourceBands,
    const std::vector<MorphResidualNoiseBand>& targetBands,
    const MorphResidualNoiseOptions& options = {});
[[nodiscard]] MorphRenderSnapshot evaluateMorphRecipeSnapshot(
    const MorphRecipe& recipe,
    const MorphSnapshotOptions& options = {});
[[nodiscard]] MorphSnapshotPlaybackBuffer renderMorphSnapshotPlaybackBuffer(
    const MorphRenderSnapshot& snapshot,
    const MorphSnapshotPlaybackSettings& settings = {});
int mixMorphSnapshotPlaybackBlock(const MorphSnapshotPlaybackBuffer& playback,
                                  MorphSnapshotPlaybackCursor& cursor,
                                  float partialGain,
                                  float residualGain,
                                  float outputTrim,
                                  bool residualOnly,
                                  float* output,
                                  int numSamples);
[[nodiscard]] MorphPerformanceProfile profileMorphPerformance(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    int frameCount,
    const MorphAssignmentMatrixOptions& assignmentOptions = {},
    const MorphPerformanceSoftCaps& softCaps = {});
[[nodiscard]] std::vector<std::vector<MorphCorrespondenceRecord>> smoothCorrespondenceFrames(
    const std::vector<std::vector<MorphCorrespondenceRecord>>& frames,
    const MorphCorrespondenceSmoothingOptions& options = {});
[[nodiscard]] MorphCorrespondenceChurnMetrics calculateCorrespondenceChurnMetrics(
    const std::vector<std::vector<MorphCorrespondenceRecord>>& frames);
[[nodiscard]] bool unmatchedPartialPoliciesRequireDiscardOptIn(const MorphUnmatchedPartialPolicies& policies);
[[nodiscard]] bool validateUnmatchedPartialPolicies(const MorphUnmatchedPartialPolicies& policies,
                                                    std::string* errorMessage = nullptr);
[[nodiscard]] std::vector<MorphUnmatchedPartialDecision> resolveUnmatchedPartialPolicies(
    const std::vector<MorphCorrespondenceRecord>& records,
    const MorphUnmatchedPartialPolicies& policies);
[[nodiscard]] MorphRecipe makeDefaultMorphRecipe();
[[nodiscard]] MorphCompatibilityReport validateMorphSources(const MorphRecipe& recipe);
[[nodiscard]] std::string serializeMorphRecipe(const MorphRecipe& recipe);
[[nodiscard]] bool deserializeMorphRecipe(const std::string& text, MorphRecipe& recipe, std::string* errorMessage);

} // namespace ats

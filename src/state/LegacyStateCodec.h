#pragma once

#include "AtsParams.h"
#include "../core/AtsStateCodec.h"
#include "AtsPrimitives.h"

#include <array>
#include <optional>

namespace ats
{

struct ResidualBandState
{
    std::int32_t bandIndex = -1;
    double partialNoisePower = 0.0;
    double orphanResidualPower = 0.0;
};

struct ResidualState
{
    bool hasResidual = false;
    std::int32_t activeBandCount = 0;
    std::int32_t activeBandFrameCount = 0;
    double totalEnergy = 0.0;
    std::int32_t peakBandIndex = -1;
    double peakBandEnergy = 0.0;
    std::vector<ResidualBandState> bands;
};

struct VariationState
{
    bool hasSnapshot = false;
    std::string name;
    double morphAmount = 0.0;
    double performanceMacro = 0.5;
    double smoothingMs = 40.0;
    std::array<double, 6> laneAmounts {};
    std::array<bool, 6> laneLinked { true, true, true, true, true, true };
};

struct SmearnovState
{
    double blurMs = 0.0;
    double sharpen = 0.0;
    double exaggerateDb = 0.0;
    double maxDb = 0.0;
    std::int32_t targets = 0;
    std::int32_t scope = 0;
    double weightDetFlux = 0.0;
    double weightResFlux = 0.0;
    double weightHfcFlux = 0.0;
    double weightBirth = 0.0;
    double weightPhaseDev = 0.0;
    double thresholdLambda = 0.0;
    double thresholdAutoScale = 0.0;
    double preMarginMs = 0.0;
    std::int32_t peakWindowFrames = 0;
    std::int32_t minGapFrames = 0;
    std::int32_t maxAttackFrames = 0;
    bool usePhaseDeviation = false;
    bool useRpcaFallback = false;
    std::int32_t controlTab = 0;
    double mix = 100.0;
};

struct LegacyState
{
    AnalysisParams analysis;
    SynthParams synth;
    FrequencyShapeSettings frequencyShape;
    double outputTrim = 1.0;
    std::vector<std::byte> atsModel;
    bool residualOnly = false;
    std::int32_t classicPreviewMode = 0;
    std::string morphRecipe;
    double morphAmount = 0.0;
    double morphPerformanceMacro = 0.5;
    double morphSmoothingMs = 40.0;
    std::array<double, 6> morphLaneAmounts {};
    std::array<bool, 6> morphLaneLinked { true, true, true, true, true, true };
    std::int32_t morphGridType = 0;
    std::int32_t morphCorrespondenceStrategy = 3;
    std::int32_t morphQuality = 1;
    std::int32_t morphUnmatchedSourcePolicy = 0;
    std::int32_t morphUnmatchedTargetPolicy = 1;
    bool morphDiscardOptIn = false;
    std::int32_t morphFrequencyDomain = 0;
    std::int32_t morphAmplitudeDomain = 0;
    std::int32_t morphPhaseStrategy = 0;
    std::int32_t morphTransientPolicy = 0;
    std::array<VariationState, 3> variations;
    std::array<std::string, 2> morphSourcePaths;
    std::array<ResidualState, 2> residualStates;
    SmearnovState smearnov;
    std::array<std::vector<std::byte>, 2> morphSoundData;
    std::vector<float> morphTimeline { 0.0f, 1.0f };
    std::string timbreJson;
    std::vector<float> smearnovOnsets;
};

class LegacyStateCodec
{
public:
    static constexpr std::uint32_t kCurrentVersion = 18;
    static constexpr std::uint32_t kMinimumReadableVersion = 2;

    [[nodiscard]] static std::vector<std::byte> encode(const LegacyState& state,
                                                        std::uint32_t version = kCurrentVersion);
    [[nodiscard]] static std::optional<LegacyState> decode(const void* data, std::size_t size);
};

} // namespace ats

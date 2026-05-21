#pragma once

#include "AtsModernModel.h"

#include <string>
#include <vector>

namespace ats
{

constexpr int kModernAtsMorphRecipeVersion = 1;

enum class MorphUnmatchedPolicy
{
    FadeOut,
    FadeIn,
    Nearest,
    Hold,
    Discard
};

enum class MorphCorrespondenceStrategy
{
    Explicit,
    HarmonicNumber,
    NearestFrequency,
    Automatic
};

enum class MorphTransientMode
{
    SourceOnly,
    TargetOnly,
    BlendBoth,
    FollowMorph
};

const char* toString(MorphUnmatchedPolicy policy);
const char* toString(MorphCorrespondenceStrategy strategy);
const char* toString(MorphTransientMode mode);

struct MorphEnvelopePoint
{
    double position = 0.0;
    double value = 0.0;
};

struct MorphEnvelope
{
    std::string id;
    std::vector<MorphEnvelopePoint> points;
};

struct MorphEnvelopeSet
{
    MorphEnvelope frequency;
    MorphEnvelope amplitude;
    MorphEnvelope partialNoise;
    MorphEnvelope orphanResidual;
    MorphEnvelope time;
    MorphEnvelope transientTiming;
    MorphEnvelope transientGain;
    MorphEnvelope transientSourceMix;
};

struct MorphPartialCorrespondence
{
    int sourcePartialId = -1;
    int targetPartialId = -1;
    std::string label;
    std::string sourceLabel;
    std::string targetLabel;
    MorphUnmatchedPolicy sourceUnmatchedPolicy = MorphUnmatchedPolicy::FadeOut;
    MorphUnmatchedPolicy targetUnmatchedPolicy = MorphUnmatchedPolicy::FadeIn;
};

struct MorphRecipe
{
    int schemaVersion = kModernAtsMorphRecipeVersion;
    std::string recipeId;
    std::string sourceModelId;
    std::string targetModelId;
    MorphCorrespondenceStrategy correspondenceStrategy = MorphCorrespondenceStrategy::Automatic;
    MorphUnmatchedPolicy sourceUnmatchedPolicy = MorphUnmatchedPolicy::FadeOut;
    MorphUnmatchedPolicy targetUnmatchedPolicy = MorphUnmatchedPolicy::FadeIn;
    MorphTransientMode transientMode = MorphTransientMode::FollowMorph;
    bool protectTransientWindows = true;
    double sourceFundamentalHz = 0.0;
    double targetFundamentalHz = 0.0;
    double sourceHarmonicity = 0.0;
    double targetHarmonicity = 0.0;
    double maxNearestCents = 700.0;
    MorphEnvelopeSet envelopes;
    std::vector<MorphPartialCorrespondence> correspondences;
};

struct MorphFixtureMetrics
{
    int outputPartials = 0;
    int outputFrames = 0;
    int transientMarkers = 0;
    double attackWindowSeconds = 0.0;
    double orphanResidualEnergy = 0.0;
};

double evaluateMorphEnvelope(const MorphEnvelope& envelope, double position, double fallbackValue);
MorphEnvelope makeConstantMorphEnvelope(const std::string& id, double value);
MorphEnvelope makeLinearMorphEnvelope(const std::string& id, double startValue, double endValue);
MorphEnvelopeSet makeDefaultMorphEnvelopeSet();

bool validateMorphRecipe(const MorphRecipe& recipe, std::string& errorMessage);
std::string serializeMorphRecipeToJson(const MorphRecipe& recipe);

std::vector<MorphPartialCorrespondence> buildAutomaticCorrespondence(
    const ModernAtsModel& source,
    const ModernAtsModel& target,
    const MorphRecipe& recipe);

ModernAtsModel morphModernAtsModels(const ModernAtsModel& source,
                                    const ModernAtsModel& target,
                                    const MorphRecipe& recipe,
                                    double morphPosition,
                                    MorphFixtureMetrics* metrics = nullptr);

} // namespace ats

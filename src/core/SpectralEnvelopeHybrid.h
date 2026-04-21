#pragma once

#include "EnvelopeEvaluation.h"
#include "RegularizedCepstrum.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ats
{

enum class HybridFrequencyMapping
{
    DirectHz,
    NormalizedNyquist,
    PerceptualWarp,
    RangeWarp,
    Correspondence,
    HarmonicNumber
};

enum class HybridNormalizationPolicy
{
    None,
    FrameRms,
    EnergyPreserving,
    LoudnessLike
};

enum class HybridResidualPolicy
{
    Unchanged,
    TargetColored,
    DecolorThenColor,
    Crossfade,
    IndependentMorph
};

enum class HybridTransientPolicy
{
    PreserveSource,
    TargetColor,
    CrossfadeSource,
    Bypass
};

struct HybridCorrespondencePoint
{
    double sourceFrequencyHz = 0.0;
    double targetFrequencyHz = 0.0;
    double confidence = 1.0;
};

struct SpectralEnvelopeTransferRecipe
{
    static constexpr std::uint32_t kVersion = 1;

    std::uint32_t version = kVersion;
    std::string recipeId;
    std::string sourceModelId;
    std::string targetModelId;

    std::string estimatorId = RegularizedCepstrumEnvelopeEstimator::kId;
    std::uint32_t estimatorVersion = RegularizedCepstrumEnvelopeEstimator::kVersion;
    int envelopeOrder = 16;
    double regularization = 1.0e-3;
    CepstrumPenalty penalty = CepstrumPenalty::SecondDerivative;
    EnvelopeFrequencyAxis warpAxis = EnvelopeFrequencyAxis::Bark;

    double sourceDecolorAmount = 1.0;
    double targetColorAmount = 1.0;
    double envelopeMix = 1.0;
    double amplitudeFloor = 1.0e-12;

    HybridFrequencyMapping mapping = HybridFrequencyMapping::DirectHz;
    EnvelopeExtrapolationPolicy extrapolation = EnvelopeExtrapolationPolicy::Clamp;
    HybridNormalizationPolicy partialNormalization = HybridNormalizationPolicy::FrameRms;
    HybridNormalizationPolicy residualNormalization = HybridNormalizationPolicy::None;
    HybridResidualPolicy residualPolicy = HybridResidualPolicy::Unchanged;
    HybridTransientPolicy transientPolicy = HybridTransientPolicy::PreserveSource;

    bool harmonicMappingEnabled = false;
    bool harmonicityReliable = false;
    double sourceF0Hz = 0.0;
    double targetF0Hz = 0.0;
    std::vector<HybridCorrespondencePoint> correspondence;

    [[nodiscard]] std::string serialize() const;
    static bool deserialize(const std::string& text,
                            SpectralEnvelopeTransferRecipe& out,
                            std::string* errorMessage = nullptr);
};

[[nodiscard]] const char* toString(HybridFrequencyMapping mapping);
[[nodiscard]] const char* toString(HybridNormalizationPolicy policy);
[[nodiscard]] const char* toString(HybridResidualPolicy policy);
[[nodiscard]] const char* toString(HybridTransientPolicy policy);

bool parseHybridFrequencyMapping(const std::string& text, HybridFrequencyMapping& out);
bool parseHybridNormalizationPolicy(const std::string& text, HybridNormalizationPolicy& out);
bool parseHybridResidualPolicy(const std::string& text, HybridResidualPolicy& out);
bool parseHybridTransientPolicy(const std::string& text, HybridTransientPolicy& out);

} // namespace ats

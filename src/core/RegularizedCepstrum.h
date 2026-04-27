#pragma once

#include "SpectralEnvelope.h"
#include "SpectralEnvelopeEstimator.h"

#include <string>
#include <vector>

namespace ats
{

enum class CepstrumPenalty
{
    None,
    FirstDerivative,
    SecondDerivative
};

struct RdcFitPoint
{
    double frequencyHz = 0.0;
    double logAmplitudeDb = 0.0;
    double weight = 1.0;
};

struct RdcFitOptions
{
    EnvelopeAxisSpec frequencyAxis = { EnvelopeFrequencyAxis::Bark };
    int order = 16;
    double regularization = 1.0e-3;
    CepstrumPenalty penalty = CepstrumPenalty::SecondDerivative;
    double ridge = 1.0e-10;
    bool reduceOrderForSparseFrames = true;
    bool retryIllConditionedWithRegularization = true;
    bool fallbackToConstantEnvelope = true;
    bool useBicOrderSelection = true;
    double sparseRegularizationFloor = 1.0e-2;
    double illConditionedRegularizationFloor = 1.0;
    double illConditionedThreshold = 1.0e7;
};

struct RdcFitResult
{
    bool ok = false;
    FrameEnvelope envelope;
    EnvelopeDiagnostics diagnostics;
    int effectiveOrder = 0;
    std::string errorMessage;
};

struct AtsPartialWeightOptions
{
    double amplitudeFloor = 1.0e-12;
    double minAmplitudeDb = -120.0;
    double maxAmplitudeDb = 0.0;
    double minSmrDb = -10.0;
    double maxSmrDb = 30.0;
    double minWeight = 1.0e-4;
    double maxWeight = 1.0;
    bool useSmr = true;
    bool useAmplitude = true;
    bool useConfidence = true;
    bool useSelection = true;
};

struct RegularizedCepstrumEstimatorOptions
{
    RdcFitOptions fit;
    AtsPartialWeightOptions weights;
    bool autoSwitchToTrueEnvelope = true;
    int autoSwitchThreshold = 40;
    bool enableCrossFrameVirtualPoints = true;
    double crossFrameVirtualPointWeightScale = 0.05;
    double crossFrameVirtualPointMinGap = 0.1;
    int crossFrameVirtualPointMaxCount = 6;
};

[[nodiscard]] const char* toString(CepstrumPenalty penalty);
[[nodiscard]] bool parseCepstrumPenalty(const std::string& text, CepstrumPenalty& out);

[[nodiscard]] double amplitudeToDb(double amplitude, double floor);
[[nodiscard]] double computeAtsPartialWeight(const EnvelopePartialPoint& partial,
                                             const AtsPartialWeightOptions& options,
                                             bool* usable = nullptr);
bool buildRdcFitPointsFromPartials(const EnvelopeFrameInput& frame,
                                   const AtsPartialWeightOptions& options,
                                   std::vector<RdcFitPoint>& out,
                                   EnvelopeDiagnostics* diagnostics = nullptr);

bool fitRegularizedDiscreteCepstrum(const std::vector<RdcFitPoint>& points,
                                    const RdcFitOptions& options,
                                    RdcFitResult& result);

[[nodiscard]] double evaluateRegularizedDiscreteCepstrum(const FrameEnvelope& envelope,
                                                         double frequencyHz,
                                                         bool* ok = nullptr);

class RegularizedCepstrumEnvelopeEstimator final : public SpectralEnvelopeEstimator
{
public:
    static constexpr const char* kId = "rdc/weighted";
    static constexpr std::uint32_t kVersion = 1;

    explicit RegularizedCepstrumEnvelopeEstimator(RegularizedCepstrumEstimatorOptions options = {});

    [[nodiscard]] std::string id() const override;
    [[nodiscard]] std::uint32_t version() const override;
    bool prepare(const EnvelopeEstimatorContext& context, std::string* errorMessage = nullptr) override;
    bool estimate(const EnvelopeFrameInput& frame, FrameEnvelope& out, std::string* errorMessage = nullptr) override;
    [[nodiscard]] double evaluate(const FrameEnvelope& envelope, double frequencyHz, bool* ok = nullptr) const override;
    [[nodiscard]] EnvelopeDiagnostics diagnostics() const override;

private:
    RegularizedCepstrumEstimatorOptions options;
    EnvelopeEstimatorContext context;
    EnvelopeDiagnostics lastDiagnostics;
    FrameEnvelope previousEnvelope;
    std::string previousSourceModelId;
    std::uint64_t previousFrameIndex = 0;
    bool hasPreviousEnvelope = false;
    bool prepared = false;
};

} // namespace ats

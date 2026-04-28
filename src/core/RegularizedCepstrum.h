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
    bool isVirtual = false; // true for cross-frame gap-fill points; excluded from BIC n_eff
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
    int minBicOrder = 0; // hard lower bound for BIC order search (0 = no floor)
    double sparseRegularizationFloor = 1.0e-2;
    double illConditionedRegularizationFloor = 1.0;
    double illConditionedThreshold = 1.0e7;
    // IRLS iterations for Huber-loss robustness against outlier partials.
    // 0 disables IRLS (pure L2). 4 gives convergence in most cases.
    int irlsMaxIterations = 4;
    // GCV-based lambda selection: search a log-space grid and choose the lambda
    // that minimises the Generalized Cross-Validation score
    //   GCV(λ) = RSS(λ) / (n_eff · (1 − df(λ)/n_eff)²)
    // where df(λ) = tr(M_λ⁻¹ · M), M = XᵀWX, M_λ = M + λP.
    // Disabled by default; enable via useGcvLambda = true.
    bool   useGcvLambda      = false;
    int    gcvCandidateCount = 10;
    double gcvLambdaMin      = 1.0e-5;
    double gcvLambdaMax      = 1.0e+1;
};

struct RdcFitResult
{
    bool ok = false;
    FrameEnvelope envelope;
    EnvelopeDiagnostics diagnostics;
    int effectiveOrder = 0;
    std::string errorMessage;
    // GCV fields — non-zero only when RdcFitOptions::useGcvLambda = true.
    double gcvScore   = 0.0;
    bool   gcvWasUsed = false;
};

struct AtsPartialWeightOptions
{
    double amplitudeFloor = 1.0e-12;
    double minAmplitudeDb = -120.0;
    double maxAmplitudeDb = 30.0; // extended above 0 dB so high-amplitude partials receive higher weight
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
    // Gate virtual points when real partials have shifted by more than this many dB
    // on average relative to the previous frame's envelope.  On onset/decay frames
    // where spectral content changes rapidly the previous-frame support is stale and
    // injecting it degrades rather than stabilises the fit.
    double crossFrameVirtualPointMaxDeltaDb = 12.0;
    // When BIC order selection is enabled, enforce a minimum order equal to
    // ceil(fitPointCount * bicMinOrderPartialDensity).  Prevents order collapse
    // to 1-3 on dense frames that carry meaningful formant or timbral structure.
    double bicMinOrderPartialDensity = 0.15;
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

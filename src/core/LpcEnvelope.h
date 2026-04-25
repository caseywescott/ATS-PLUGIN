#pragma once

#include "RegularizedCepstrum.h"
#include "SpectralEnvelopeEstimator.h"

#include <string>
#include <vector>

namespace ats
{

struct LpcEnvelopeEstimatorOptions
{
    EnvelopeAxisSpec frequencyAxis;
    AtsPartialWeightOptions weights;
    int sampleCount = 128;
    int order = 12;
    double regularization = 1.0e-6;
    double amplitudeFloor = 1.0e-12;
    int minFitPointCount = 8;
    double sparseDensityWarningThreshold = 0.12;
    double maxReflectionCoefficient = 0.98;
};

struct LpcEnvelopeFitResult
{
    bool ok = false;
    FrameEnvelope envelope;
    EnvelopeDiagnostics diagnostics;
    int effectiveOrder = 0;
    std::vector<double> predictorCoefficients;
    double predictionError = 0.0;
    std::string errorMessage;
};

bool fitLpcEnvelopeFromPoints(const std::vector<RdcFitPoint>& points,
                              const LpcEnvelopeEstimatorOptions& options,
                              LpcEnvelopeFitResult& result);

[[nodiscard]] double evaluateLpcEnvelope(const FrameEnvelope& envelope,
                                         double frequencyHz,
                                         bool* ok = nullptr);

class LpcEnvelopeEstimator final : public SpectralEnvelopeEstimator
{
public:
    static constexpr const char* kId = "lpc/all-pole";
    static constexpr std::uint32_t kVersion = 1;

    explicit LpcEnvelopeEstimator(LpcEnvelopeEstimatorOptions options = {});

    [[nodiscard]] std::string id() const override;
    [[nodiscard]] std::uint32_t version() const override;
    bool prepare(const EnvelopeEstimatorContext& context, std::string* errorMessage = nullptr) override;
    bool estimate(const EnvelopeFrameInput& frame, FrameEnvelope& out, std::string* errorMessage = nullptr) override;
    [[nodiscard]] double evaluate(const FrameEnvelope& envelope, double frequencyHz, bool* ok = nullptr) const override;
    [[nodiscard]] EnvelopeDiagnostics diagnostics() const override;

private:
    LpcEnvelopeEstimatorOptions options;
    EnvelopeEstimatorContext context;
    EnvelopeDiagnostics lastDiagnostics;
    bool prepared = false;
};

} // namespace ats

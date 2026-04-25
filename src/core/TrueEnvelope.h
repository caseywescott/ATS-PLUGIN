#pragma once

#include "RegularizedCepstrum.h"
#include "SpectralEnvelopeEstimator.h"

#include <string>
#include <vector>

namespace ats
{

enum class TrueEnvelopeLifterWindow
{
    Rectangular,
    Hamming
};

struct TrueEnvelopeEstimatorOptions
{
    EnvelopeAxisSpec frequencyAxis;
    AtsPartialWeightOptions weights;
    int sampleCount = 128;
    int lifterOrder = 16;
    int iterations = 32;
    double convergenceDb = 1.0e-3;
    int minFitPointCount = 6;
    double sparseDensityWarningThreshold = 0.12;
    TrueEnvelopeLifterWindow lifterWindow = TrueEnvelopeLifterWindow::Hamming;
};

struct TrueEnvelopeFitResult
{
    bool ok = false;
    FrameEnvelope envelope;
    EnvelopeDiagnostics diagnostics;
    int iterationsUsed = 0;
    std::string errorMessage;
};

[[nodiscard]] const char* toString(TrueEnvelopeLifterWindow window);
[[nodiscard]] bool parseTrueEnvelopeLifterWindow(const std::string& text, TrueEnvelopeLifterWindow& out);

bool fitTrueEnvelopeFromPoints(const std::vector<RdcFitPoint>& points,
                               const TrueEnvelopeEstimatorOptions& options,
                               TrueEnvelopeFitResult& result);

[[nodiscard]] double evaluateTrueEnvelope(const FrameEnvelope& envelope,
                                          double frequencyHz,
                                          bool* ok = nullptr);

class TrueEnvelopeEstimator final : public SpectralEnvelopeEstimator
{
public:
    static constexpr const char* kId = "true-envelope/iterative";
    static constexpr std::uint32_t kVersion = 1;

    explicit TrueEnvelopeEstimator(TrueEnvelopeEstimatorOptions options = {});

    [[nodiscard]] std::string id() const override;
    [[nodiscard]] std::uint32_t version() const override;
    bool prepare(const EnvelopeEstimatorContext& context, std::string* errorMessage = nullptr) override;
    bool estimate(const EnvelopeFrameInput& frame, FrameEnvelope& out, std::string* errorMessage = nullptr) override;
    [[nodiscard]] double evaluate(const FrameEnvelope& envelope, double frequencyHz, bool* ok = nullptr) const override;
    [[nodiscard]] EnvelopeDiagnostics diagnostics() const override;

private:
    TrueEnvelopeEstimatorOptions options;
    EnvelopeEstimatorContext context;
    EnvelopeDiagnostics lastDiagnostics;
    bool prepared = false;
};

} // namespace ats

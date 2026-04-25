#pragma once

#include "SpectralEnvelopeEstimator.h"

#include <string>
#include <vector>

namespace ats
{

struct F0AdaptiveEnvelopeOptions
{
    EnvelopeAxisSpec frequencyAxis;
    int sampleCount = 128;
    double amplitudeFloor = 1.0e-12;
    double minF0Hz = 40.0;
    double maxF0Hz = 1200.0;
    double minF0Confidence = 0.85;
    int minSpectrumPointCount = 16;
    double smoothingBandwidthInF0 = 1.5;
    double minimumBandwidthHz = 80.0;
};

struct F0AdaptiveEnvelopeInput
{
    std::string sourceModelId;
    std::uint64_t frameIndex = 0;
    double sampleRate = 0.0;
    bool waveformAvailable = false;
    double fundamentalFrequencyHz = 0.0;
    double f0Confidence = 0.0;
    std::vector<EnvelopeSpectrumPoint> spectrum;
};

struct F0AdaptiveEnvelopeFitResult
{
    bool ok = false;
    FrameEnvelope envelope;
    EnvelopeDiagnostics diagnostics;
    std::string errorMessage;
};

bool fitF0AdaptiveEnvelope(const F0AdaptiveEnvelopeInput& input,
                           const F0AdaptiveEnvelopeOptions& options,
                           F0AdaptiveEnvelopeFitResult& result);

[[nodiscard]] double evaluateF0AdaptiveEnvelope(const FrameEnvelope& envelope,
                                                double frequencyHz,
                                                bool* ok = nullptr);

class F0AdaptiveEnvelopeEstimator final : public SpectralEnvelopeEstimator
{
public:
    static constexpr const char* kId = "f0-adaptive/waveform-prototype";
    static constexpr std::uint32_t kVersion = 1;

    explicit F0AdaptiveEnvelopeEstimator(F0AdaptiveEnvelopeOptions options = {});

    [[nodiscard]] std::string id() const override;
    [[nodiscard]] std::uint32_t version() const override;
    bool prepare(const EnvelopeEstimatorContext& context, std::string* errorMessage = nullptr) override;
    bool estimate(const EnvelopeFrameInput& frame, FrameEnvelope& out, std::string* errorMessage = nullptr) override;
    [[nodiscard]] double evaluate(const FrameEnvelope& envelope, double frequencyHz, bool* ok = nullptr) const override;
    [[nodiscard]] EnvelopeDiagnostics diagnostics() const override;

private:
    F0AdaptiveEnvelopeOptions options;
    EnvelopeEstimatorContext context;
    EnvelopeDiagnostics lastDiagnostics;
    bool prepared = false;
};

} // namespace ats

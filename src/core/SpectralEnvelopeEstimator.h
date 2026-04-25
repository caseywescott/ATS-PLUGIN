#pragma once

#include "SpectralEnvelope.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ats
{

struct EnvelopePartialPoint
{
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    double smr = 0.0;
    double confidence = 1.0;
    bool selected = true;
};

struct EnvelopeSpectrumPoint
{
    double frequencyHz = 0.0;
    double amplitude = 0.0;
};

struct EnvelopeEstimatorContext
{
    double sampleRate = 0.0;
    EnvelopeAxisSpec frequencyAxis;
    int maxOrder = 0;
    double amplitudeFloor = 1.0e-12;
};

struct EnvelopeFrameInput
{
    std::string sourceModelId;
    std::uint64_t frameIndex = 0;
    double sampleRate = 0.0;
    std::vector<EnvelopePartialPoint> partials;
    bool waveformAvailable = false;
    double fundamentalFrequencyHz = 0.0;
    double f0Confidence = 0.0;
    std::vector<EnvelopeSpectrumPoint> waveformSpectrum;
};

class SpectralEnvelopeEstimator
{
public:
    virtual ~SpectralEnvelopeEstimator() = default;

    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual std::uint32_t version() const = 0;
    virtual bool prepare(const EnvelopeEstimatorContext& context, std::string* errorMessage = nullptr) = 0;
    virtual bool estimate(const EnvelopeFrameInput& frame, FrameEnvelope& out, std::string* errorMessage = nullptr) = 0;
    [[nodiscard]] virtual double evaluate(const FrameEnvelope& envelope,
                                          double frequencyHz,
                                          bool* ok = nullptr) const = 0;
    [[nodiscard]] virtual std::string serialize(const FrameEnvelope& envelope) const;
    [[nodiscard]] virtual EnvelopeDiagnostics diagnostics() const = 0;
};

class EnvelopeEstimatorRegistry
{
public:
    using Factory = std::function<std::unique_ptr<SpectralEnvelopeEstimator>()>;

    bool registerFactory(std::string id, Factory factory);
    [[nodiscard]] std::unique_ptr<SpectralEnvelopeEstimator> create(const std::string& id) const;
    [[nodiscard]] bool contains(const std::string& id) const;
    [[nodiscard]] std::vector<std::string> ids() const;

private:
    std::vector<std::pair<std::string, Factory>> factories;
};

class NoOpEnvelopeEstimator final : public SpectralEnvelopeEstimator
{
public:
    static constexpr const char* kId = "sample/noop";
    static constexpr std::uint32_t kVersion = 1;

    [[nodiscard]] std::string id() const override;
    [[nodiscard]] std::uint32_t version() const override;
    bool prepare(const EnvelopeEstimatorContext& context, std::string* errorMessage = nullptr) override;
    bool estimate(const EnvelopeFrameInput& frame, FrameEnvelope& out, std::string* errorMessage = nullptr) override;
    [[nodiscard]] double evaluate(const FrameEnvelope& envelope, double frequencyHz, bool* ok = nullptr) const override;
    [[nodiscard]] EnvelopeDiagnostics diagnostics() const override;

private:
    EnvelopeEstimatorContext context;
    EnvelopeDiagnostics lastDiagnostics;
    bool prepared = false;
};

[[nodiscard]] EnvelopeEstimatorRegistry createDefaultEnvelopeEstimatorRegistry();

} // namespace ats

#pragma once

#include <vector>

namespace ats
{

struct SpectralEnvelopePoint
{
    double frequencyHz = 0.0;
    double amplitude = 0.0;
};

class BarkSpectralEnvelope
{
public:
    static constexpr int kDefaultBins = 64;
    static constexpr double kDefaultMaxBark = 24.0;
    static constexpr double kDefaultSigmaBins = 3.0;
    static constexpr double kDefaultAmplitudeFloor = 1.0e-12;

    void build(const std::vector<SpectralEnvelopePoint>& points,
               int binCount = kDefaultBins,
               double sigmaBins = kDefaultSigmaBins,
               double amplitudeFloor = kDefaultAmplitudeFloor);

    [[nodiscard]] double sampleAtHz(double frequencyHz) const;
    [[nodiscard]] bool isValid() const noexcept { return valid; }
    [[nodiscard]] int getBinCount() const noexcept { return static_cast<int>(ampBins.size()); }
    [[nodiscard]] double getAmplitudeFloor() const noexcept { return amplitudeFloor; }

    [[nodiscard]] static double hzToBark(double frequencyHz);

private:
    std::vector<double> ampBins;
    double maxBark = kDefaultMaxBark;
    double amplitudeFloor = kDefaultAmplitudeFloor;
    bool valid = false;
};

struct SpectralEnvelopeTransferResult
{
    double outputAmplitude = 0.0;
    double transferGainDb = 0.0;
    bool ok = false;
};

[[nodiscard]] SpectralEnvelopeTransferResult applySpectralEnvelopeTransfer(
    double sourceAmplitude,
    double sourceEnvelopeAmplitude,
    double targetEnvelopeAmplitude,
    double amount,
    double amplitudeFloor = BarkSpectralEnvelope::kDefaultAmplitudeFloor);

struct HybridizedPartialRenderResult
{
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    bool active = false;
};

enum class HybridizedPartialKind
{
    Matched,
    UnmatchedSource,
    UnmatchedTarget
};

[[nodiscard]] HybridizedPartialRenderResult resolveHybridizedPartialRender(
    HybridizedPartialKind kind,
    double sourceFrequencyHz,
    double sourceAmplitude,
    double targetFrequencyHz,
    double targetAmplitude,
    double morphAmount,
    double nearestTargetHz = 0.0,
    double nearestSourceHz = 0.0);

} // namespace ats

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
    static constexpr double kDefaultAmplitudeFloor = 1.0e-12;
    static constexpr int kMaxLifterOrder = 24;

    // Estimate the amplitude envelope from a set of (frequency, amplitude) spectral peaks
    // using the True Envelope cepstral method (Röbel/Rodet; Caetano/Rodet) on a Bark grid:
    // an iterative log-domain cepstral smoothing that rides the peaks rather than averaging
    // across them. lifterOrder <= 0 selects the cepstral order automatically from the
    // partials' median Bark spacing so the envelope smooths the harmonic comb while keeping
    // formant structure.
    void build(const std::vector<SpectralEnvelopePoint>& points,
               int binCount = kDefaultBins,
               int lifterOrder = 0,
               double amplitudeFloor = kDefaultAmplitudeFloor);

    [[nodiscard]] double sampleAtHz(double frequencyHz) const;
    // Sample directly at a Bark coordinate (0..maxBark). Used by formant-aligned morphing,
    // which warps the Bark axis and so needs to read the envelope at arbitrary Bark positions.
    [[nodiscard]] double sampleAtBark(double bark) const;
    [[nodiscard]] bool isValid() const noexcept { return valid; }
    [[nodiscard]] int getBinCount() const noexcept { return static_cast<int>(ampBins.size()); }
    [[nodiscard]] double getAmplitudeFloor() const noexcept { return amplitudeFloor; }
    [[nodiscard]] double getPeakAmplitude() const noexcept { return peakAmplitude; }
    [[nodiscard]] double getMaxBark() const noexcept { return maxBark; }

    // Bark positions of the envelope's formant peaks (local maxima whose topographic
    // prominence is at least minProminenceDb), ascending. Used to align formants across two
    // envelopes before morphing.
    [[nodiscard]] std::vector<double> findPeakBarks(double minProminenceDb = 3.0) const;

    [[nodiscard]] static double hzToBark(double frequencyHz);

private:
    std::vector<double> ampBins;
    std::vector<double> logBins; // log(ampBins[i]); precomputed for sampleAtBark interpolation
    double maxBark = kDefaultMaxBark;
    double amplitudeFloor = kDefaultAmplitudeFloor;
    double peakAmplitude = kDefaultAmplitudeFloor;
    bool valid = false;
};

// Formant-aligned envelope morph (Caetano/Rodet-style): instead of blending two envelopes at
// the same frequency (which double-images / dips a formant that moves between A and B), it
// matches the envelopes' formant peaks, warps the frequency axis so corresponding formants
// glide together, and only then blends amplitude in the log domain. Borrowed envelopes must
// outlive this object.
class FormantAlignedEnvelopeMorph
{
public:
    static constexpr double kDefaultRelativeFloor = 0.00398107; // -48 dB below each peak

    void build(const BarkSpectralEnvelope& a,
               const BarkSpectralEnvelope& b,
               double relativeFloor = kDefaultRelativeFloor,
               double minProminenceDb = 3.0);

    [[nodiscard]] bool isValid() const noexcept { return valid; }
    [[nodiscard]] int getMatchedFormantCount() const noexcept { return matchedFormants; }

    // morphAmount in [0,1]: 0 -> envelope A, 1 -> envelope B.
    [[nodiscard]] double sampleAtHz(double frequencyHz, double morphAmount) const;
    [[nodiscard]] double sampleAtBark(double bark, double morphAmount) const;

private:
    const BarkSpectralEnvelope* envA = nullptr;
    const BarkSpectralEnvelope* envB = nullptr;
    // Warp anchors on each source axis (Bark), including the 0 and maxBark endpoints. anchorA[k]
    // in A corresponds to anchorB[k] in B; both are ascending so any convex combination is too.
    std::vector<double> anchorA;
    std::vector<double> anchorB;
    double floorA = BarkSpectralEnvelope::kDefaultAmplitudeFloor;
    double floorB = BarkSpectralEnvelope::kDefaultAmplitudeFloor;
    double maxBark = BarkSpectralEnvelope::kDefaultMaxBark;
    int matchedFormants = 0;
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

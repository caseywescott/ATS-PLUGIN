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

// Fit a Bark-domain Regularized Discrete Cepstrum (cosine-series) envelope to one frame's
// active partials and re-impose it on their amplitudes, flattening harmonic-specific
// deviations onto the smooth spectral envelope. This is the destructive counterpart of the
// RDC envelope display: it edits the polynomial-fitted curve into the stored amplitudes.
//
// freqsHz / amps are parallel arrays (one entry per partial in the frame); amps is modified
// in place. Partials with amplitude <= amplitudeFloor are treated as inactive: they are
// excluded from the fit and left untouched. smoothness in [0,1] maps to the cosine-series
// order and second-derivative regularization (0 = most detailed fit, 1 = smoothest).
// minHz/maxHz set the Bark normalization span; pass the model's frequency range so the
// basis is identical across frames. Returns false (and leaves amps unchanged) if fewer than
// three active partials are available to fit.
bool reimposeRdcEnvelopeFrame(const std::vector<double>& freqsHz,
                              std::vector<double>& amps,
                              double smoothness,
                              double minHz,
                              double maxHz,
                              double amplitudeFloor = BarkSpectralEnvelope::kDefaultAmplitudeFloor);

// Soft-knee ceiling on an envelope-transfer boost. The hybrid env transfer can push a partial's
// amplitude far above its analyzed value when B's envelope is much louder than A's at that
// frequency; an unbounded boost amplifies analysis noise into audible whistles, so the result is
// capped at maxBoostRatio times the source amplitude. A hard std::min() clip kinks the gain curve
// (zero derivative the instant the limit is hit), which clicks when a partial rides the ceiling
// across chunks. This applies the standard soft-knee limiter law in the dB domain: the boost
// passes through untouched until kneeDb below the ceiling, then bends smoothly (continuous value
// and slope) to asymptote at the ceiling. Returns the capped output amplitude. sourceAmplitude<=0
// or non-finite inputs return a safe 0 / passthrough.
[[nodiscard]] double softCapEnvelopeBoost(double outputAmplitude,
                                          double sourceAmplitude,
                                          double maxBoostRatio,
                                          double kneeDb = 6.0);

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

// Resolve a single partial's blended frequency and amplitude with independent
// frequency-lane and amplitude-lane morph amounts.
[[nodiscard]] HybridizedPartialRenderResult resolveHybridizedPartialRender(
    HybridizedPartialKind kind,
    double sourceFrequencyHz,
    double sourceAmplitude,
    double targetFrequencyHz,
    double targetAmplitude,
    double freqMorphAmount,
    double ampMorphAmount,
    double nearestTargetHz = 0.0,
    double nearestSourceHz = 0.0);

// Convenience overload: same amount for frequency and amplitude (backward compat).
[[nodiscard]] inline HybridizedPartialRenderResult resolveHybridizedPartialRender(
    HybridizedPartialKind kind,
    double sourceFrequencyHz,
    double sourceAmplitude,
    double targetFrequencyHz,
    double targetAmplitude,
    double morphAmount,
    double nearestTargetHz = 0.0,
    double nearestSourceHz = 0.0)
{
    return resolveHybridizedPartialRender(kind, sourceFrequencyHz, sourceAmplitude,
                                          targetFrequencyHz, targetAmplitude,
                                          morphAmount, morphAmount,
                                          nearestTargetHz, nearestSourceHz);
}

} // namespace ats

#pragma once

#include <vector>

extern "C" {
#include "atsa.h"
}

namespace ats
{

/** Tuning for Smearnov onset detection (see docs/SMEARNOV_SPEC.md §2). */
struct SmearnovDetectParams
{
    // Fusion weights for the normalized detection functions D1..D5.
    double weightDetFlux = 1.0;   // D1: deterministic (partial) energy flux
    double weightResFlux = 0.8;   // D2: residual band-energy flux
    double weightHfcFlux = 0.6;   // D3: residual high-frequency-content flux
    double weightBirth = 0.7;     // D4: partial-birth rate
    double weightPhaseDev = 0.4;  // D5: phase deviation (type 2/4 only)

    // D5 is only meaningful when the model carries phase; off by default.
    bool usePhaseDeviation = false;

    // Perceptual weighting of D1 by SMR (signal-to-mask ratio), when present.
    bool useSmrWeighting = true;
    double smrWeightFloor = 0.25; // weight at SMR=0; ramps to 1 at max SMR

    // Running median/MAD normalization window radius (frames) per Di.
    int normWindowRadiusFrames = 8;

    // Peak picking.
    int peakWindowRadiusFrames = 6;  // w: local-max + adaptive-threshold window
    double thresholdLambda = 0.0;    // additive threshold; <=0 -> auto from global MAD
    double thresholdAutoScale = 1.5; // multiplier on global MAD when lambda auto
    double onsetFloor = 1.0e-6;      // absolute minimum combined strength to accept
    double minGapSeconds = 0.03;     // refractory gap between onsets
    int minGapFramesOverride = 0;    // >0 forces the gap in frames

    // Attack-window scan.
    double preMarginSeconds = 0.005; // pre-onset margin folded into the window
    int maxAttackFrames = 0;         // 0 -> uncapped (still bounded by next onset)
};

/** One detected onset and its attack window [preFrame, peakFrame] (inclusive). */
struct AtsOnset
{
    int frame = 0;        // f0: onset frame
    int preFrame = 0;     // f_pre: pre-onset margin start
    int peakFrame = 0;    // f_peak: attack end (post-onset energy peak)
    double strength = 0.0; // combined detection value D[f0]
};

enum class SmearnovTarget
{
    Partials,
    Residual,
    Both
};

/** Attack-smear transform settings (see docs/SMEARNOV_SPEC.md §3). */
struct SmearnovParams
{
    double blurMs = 0.0;
    double sharpenAmount = 0.0;
    double exaggerateDb = 0.0;
    double maxDb = 12.0;
    SmearnovTarget targets = SmearnovTarget::Both;
};

/** Optional per-frame detection curves, for visualization and tests. */
struct SmearnovDetectionFunctions
{
    std::vector<double> combined;  // D[f] after normalization + fusion
    std::vector<double> threshold; // adaptive threshold thr[f]
};

/** Centre frequency (Hz) of Bark critical band `band` (0-based). */
[[nodiscard]] double criticalBandCentreHz(int band);

/** Detects onsets directly from the ATS model's partials + residual.
 *  `functionsOut` (optional) receives the combined detection curve and threshold. */
[[nodiscard]] std::vector<AtsOnset> detectOnsets(const ATS_SOUND* sound,
                                                 const SmearnovDetectParams& params,
                                                 SmearnovDetectionFunctions* functionsOut = nullptr);

/** Reshape partial amplitudes and/or residual band energy inside onset attack windows. */
[[nodiscard]] bool smearAttacks(ATS_SOUND* sound,
                                const std::vector<AtsOnset>& onsets,
                                const SmearnovParams& params);

} // namespace ats

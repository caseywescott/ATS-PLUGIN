#pragma once

#include "AtsEngine.h"

#include <cstddef>
#include <vector>

namespace ats
{

/**
    Analysis-by-synthesis tuning of the four resolution settings (lowest and
    highest frequency, window cycles, hop size).

    Each evaluation analyses the input, resynthesises it (partials and
    residual, unity gains) and scores the result against the analysed span
    of the input with a multi-resolution spectral loss, plus a small penalty
    per partial so equal-sounding models prefer fewer partials.  The search
    is coordinate descent from the request's settings, and keeps a change
    only when it lowers the score by more than kMinImprovement, so the
    result is never worse than the starting point.

    The lowest frequency also sets the lowest detected bin, so raising it
    can push a fundamental into the noise residual while the loss still
    falls (broadband noise roughly matches a tone's band).  The search never
    raises it above kFundamentalGuard x the lowest partial of the starting
    model within kStrongPartialDb of the loudest one.

    Two residual-quality terms guard the same failure from the other side:
    the residual r = input - phase-locked partials-only resynthesis should be
    noise-like, so sinusoidal energy left in it is penalised.
    - Residual whiteness (X. Serra, "Musical Sound Modeling with Sinusoids
      plus Noise", 1997, residual analysis): the normalised autocorrelation
      of r summed over the first kWhitenessLags lags is ~0 for white noise
      and large for tonal content; `AutoTuneRequest::whitenessWeight` x
      residualWhiteness() is added to the score.
    - Residual tonality (T. Jehan & B. Schoner, "An Audio-Driven Perceptually
      Meaningful Timbre Synthesizer", ICMC 2001, after J. D. Johnston,
      "Transform Coding of Audio Signals Using Perceptual Noise Criteria",
      1988): the spectral flatness per critical band of r mapped to a
      tonality coefficient in [0, 1], 0 for white noise and 1 for a pure
      tone; `AutoTuneRequest::tonalityWeight` x residualTonality() is added.
    A weight of 0 disables its term, and with both at 0 the score is exactly
    the spectral loss plus the parsimony term (no second resynthesis).

    Adapted from pyats (analysis/autotune.py).  pyats warm-starts from a
    rule-based suggestion; on this engine those rules made three of four
    test recordings worse (docs/STATUS.md), so the search starts from the
    current settings instead.
*/
struct AutoTuneRequest
{
    const double* samples = nullptr;
    std::size_t sampleCount = 0;
    int sampleRate = 0;
    AnalysisParams start {};
    /** Maximum number of analysis + render evaluations, the first included. */
    int budget = 40;
    /** Weight of the residual-whiteness term (Serra 1997); 0 disables it. */
    double whitenessWeight = 0.15;
    /** Weight of the residual-tonality term (Jehan & Schoner 2001); 0 disables it. */
    double tonalityWeight = 0.15;
};

struct AutoTuneStep
{
    AnalysisParams parameters {};
    /** Combined score: spectralLoss + parsimony + weighted whiteness and tonality. */
    double loss = 0.0;
    int partials = 0;
    double spectralLoss = 0.0;
    /** Serra's residual correlation coefficient, in [0, 1]; 0 when disabled. */
    double whiteness = 0.0;
    /** Mean critical-band tonality of the residual, in [0, 1]; 0 when disabled. */
    double tonality = 0.0;
};

struct AutoTuneResult
{
    AnalysisParams parameters {};
    /** Upper bound applied to the lowest frequency (Hz); 0 when none. */
    double lowestFrequencyCap = 0.0;
    /** Combined score of the chosen settings (see AutoTuneStep::loss). */
    double loss = 0.0;
    double initialLoss = 0.0;
    /** Components of `loss` for the chosen settings. */
    double spectralLoss = 0.0;
    double whiteness = 0.0;
    double tonality = 0.0;
    int evaluations = 0;
    /** The starting point, then every accepted improvement, in order. */
    std::vector<AutoTuneStep> history;

    [[nodiscard]] bool improved() const noexcept { return loss < initialLoss; }
};

class AnalysisAutoTune
{
public:
    static constexpr double kParsimonyWeight = 0.2;
    static constexpr double kParsimonyReference = 100.0;
    static constexpr double kMinImprovement = 1.0e-4;
    static constexpr double kStrongPartialDb = 30.0;
    static constexpr double kFundamentalGuard = 0.9;
    /** residualWhiteness(): block length and autocorrelation lags L (Serra 1997). */
    static constexpr std::size_t kWhitenessBlock = 2048;
    static constexpr std::size_t kWhitenessLags = 64;
    /** residualTonality(): Hann window, hop, SFM normalisation (Johnston 1988:
        -60 dB is fully tonal), band-exclusion floor below the frame's loudest
        band, and the minimum band width in bins (see the .cpp). */
    static constexpr int kTonalityFftSize = 2048;
    static constexpr int kTonalityHop = 512;
    static constexpr double kTonalitySfmDb = -60.0;
    static constexpr double kTonalityFloorDb = -60.0;
    static constexpr int kTonalityMinBandBins = 48;

    /** Worker-domain; progress reports evaluations done / budget in [0, 1]. */
    [[nodiscard]] static JobResult<AutoTuneResult> run(JobGeneration generation,
                                                        const CancellationToken& cancellation,
                                                        const AutoTuneRequest& request,
                                                        AtsEngine::Progress progress = {});

    /**
        Multi-resolution spectral loss between two signals after RMS
        normalisation, truncated to the shorter one: for FFT sizes 512, 1024
        and 2048 (Hann, hop n/4), spectral convergence plus mean absolute
        log-magnitude difference.  0 for identical signals.
    */
    [[nodiscard]] static double spectralLoss(const double* reference, std::size_t referenceCount,
                                             const double* candidate, std::size_t candidateCount);

    /**
        Serra's residual correlation coefficient, c = sum_{l=1}^{L-1}
        |r_xx(l)| / ((L-1) r_xx(0)), computed per kWhitenessBlock-sample
        block on the mean-removed, r_xx(0)-normalised residual (so the value
        is level-independent) and averaged over the non-silent blocks.
        ~0.02 for white noise, ~0.7 for a pure tone; 0 for an empty input.
    */
    [[nodiscard]] static double residualWhiteness(const double* residual, std::size_t count);

    /**
        Mean tonality coefficient of the residual over critical bands and
        STFT frames (Hann kTonalityFftSize, hop kTonalityHop): per band
        SFM_dB = 10 log10(geometric mean / arithmetic mean) of the power
        spectrum and alpha = min(SFM_dB / kTonalitySfmDb, 1).  Bands more
        than kTonalityFloorDb below the frame's loudest band are excluded.
        ~0.05 for white noise, > 0.6 for a pure tone; 0 for an empty input.
    */
    [[nodiscard]] static double residualTonality(const double* residual, std::size_t count,
                                                 int sampleRate);

    /** Candidate values tried for one setting, current value excluded; a
        lowest frequency above `lowestFrequencyCap` (when > 0) is not tried. */
    [[nodiscard]] static std::vector<AnalysisParams> neighbours(const AnalysisParams& from,
                                                                int sampleRate,
                                                                double lowestFrequencyCap = 0.0);
};

} // namespace ats

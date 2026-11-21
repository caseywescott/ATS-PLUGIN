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
};

struct AutoTuneStep
{
    AnalysisParams parameters {};
    double loss = 0.0;
    int partials = 0;
};

struct AutoTuneResult
{
    AnalysisParams parameters {};
    /** Upper bound applied to the lowest frequency (Hz); 0 when none. */
    double lowestFrequencyCap = 0.0;
    double loss = 0.0;
    double initialLoss = 0.0;
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

    /** Candidate values tried for one setting, current value excluded; a
        lowest frequency above `lowestFrequencyCap` (when > 0) is not tried. */
    [[nodiscard]] static std::vector<AnalysisParams> neighbours(const AnalysisParams& from,
                                                                int sampleRate,
                                                                double lowestFrequencyCap = 0.0);
};

} // namespace ats

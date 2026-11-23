#pragma once

#include "AtsModel.h"

#include <vector>

namespace ats
{

/**
    Vibrato analysis and depth / rate control on an ATS model.

    After Herrera & Bonada, "Vibrato extraction and parameterization in the
    Spectral Modeling Synthesis framework" (DAFx-98): a pitch track is split
    into a slow carrier and a modulation component; the modulation is
    analysed in the frequency domain for its rate and depth, and the
    partials are re-synthesised as carrier + scaled modulation.  Detection
    thresholds follow Kaminskyj (2001): vibrato is a modulation of more than
    ~20 cents peak-to-peak at 5-8 Hz (the defaults here accept 3-12 Hz; set
    minRateHz / maxRateHz to 5 / 8 for Kaminskyj's stricter band).

    Analysis:
      1. f0_est = median over frames of the lowest active partial's frequency.
         Per frame the pitch is the amplitude-weighted mean of
         frq[p][f] / round(frq[p][f] / f0_est) over active partials.
      2. pitchCents = 1200 log2(pitch / median pitch).
      3. carrierCents = moving median of pitchCents over an odd window of
         frames (~carrierWindowSeconds; edges shrink the window).  A median
         shorter than one vibrato period leaks part of the vibrato into the
         carrier, so the analysis is two-pass: the first pass estimates the
         rate from the median-only carrier, the second refines the carrier
         with a moving mean of exactly one period (fractional frame length),
         which nulls the vibrato fundamental and all its harmonics while
         keeping the median's robustness to onsets.  applyVibrato builds its
         per-partial carriers the same way from the model's detected rate.
      4. modulation = pitchCents - carrierCents, Hann-windowed, zero-padded to
         >= 1024 points (>= 4x the frame count), FFT; the dominant peak in
         [minRateHz, maxRateHz] with parabolic interpolation gives rateHz;
         depthCents = sqrt(2) * rms of the modulation inside the Hann main
         lobe around the peak (Parseval), i.e. the peak deviation of an
         equivalent sinusoid.  extentCents = 95th - 5th percentile of the
         time-domain modulation (peak-to-peak).  prominenceDb = peak
         magnitude over the median magnitude of all bins >= 1 Hz.
      5. detected = the in-range peak is a local maximum and the dominant
         peak of the whole modulation spectrum (>= 1 Hz), extentCents >=
         minExtentCents, rate inside the range, prominenceDb >=
         minProminenceDb.

    Transform (applyVibrato): per partial, over its active frames, carrier =
    moving median of frq and of amp; modulation = value - carrier.
      frq' = carrier + depth * freqMod
      amp' = ampCarrier + depth * ampMod   (only when scaleAmplitudeModulation;
             Herrera notes the amplitude ripple of a partial sweeping through
             formants is not tremolo, but removing it with the pitch
             modulation gives a convincingly "straight" tone, so it defaults on)
    rateScale != 1 reads the modulation at position i * rateScale (linear
    interpolation, wrapped over the partial's active span) so the vibrato
    becomes faster or slower.  Phases are zeroed and slopes dropped
    (has_slopes = 0): render without phase information.
*/
struct VibratoAnalysisParams
{
    double carrierWindowSeconds = 0.12;
    double minRateHz = 3.0;
    double maxRateHz = 12.0;
    double minExtentCents = 20.0;
    double minProminenceDb = 6.0;
};

struct VibratoAnalysis
{
    bool detected = false;
    double rateHz = 0.0;
    double depthCents = 0.0;      // peak deviation of the equivalent sinusoid
    double extentCents = 0.0;     // peak-to-peak (95th - 5th percentile)
    double prominenceDb = 0.0;
    double fundamentalHz = 0.0;
    std::vector<double> pitchCents;   // per frame, relative to the median pitch (held across inactive frames)
    std::vector<double> carrierCents; // per frame
};

[[nodiscard]] VibratoAnalysis analyseVibrato(const ATS_SOUND* sound, const VibratoAnalysisParams& params = {});

struct VibratoParams
{
    double depth = 1.0;           // 0..2: 0 removes the vibrato, 1 unchanged, 2 doubles it
    double rateScale = 1.0;       // 0.25..4
    double carrierWindowSeconds = 0.12;
    bool scaleAmplitudeModulation = true;

    [[nodiscard]] bool active() const noexcept { return depth != 1.0 || rateScale != 1.0; }
};

/** The transformed copy; null when the params are inactive or the model is empty. */
[[nodiscard]] AtsSoundPtr applyVibrato(const ATS_SOUND* sound, const VibratoParams& params);

} // namespace ats

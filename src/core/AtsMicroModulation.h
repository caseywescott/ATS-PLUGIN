#pragma once

#include "AtsModel.h"

#include <cstdint>

namespace ats
{

/**
    Micro-modulation ("animation"): low-rate random amplitude modulation
    (shimmer) and frequency modulation (jitter) added to a model copy.

    K. Jensen, "The Timbre Model" (and "Timbre Models of Musical Sounds",
    1999) describes the irregularity of each partial's amplitude and
    frequency as low-pass filtered noise with a component shared by all
    partials plus an independent component; Torvmark ("Timbral
    microstructures") finds that decorrelated AM on the lowest partials is
    what makes a steady tone sound alive, and that modulation below ~15 Hz
    reads as fluctuation while 15-300 Hz reads as roughness.

    Method, per partial p and frame f:

      n_p[f] = sqrt(c) * g_shared[f] + sqrt(1 - c) * g_p[f]          (c = correlation)
               -> unit-variance Gaussian, correlation coefficient c between partials
      y_p[f] = y_p[f-1] + a * (n_p[f] - y_p[f-1]),  a = 1 - exp(-2 pi rateHz hop)
               (one-pole low-pass along frames: modulation bandwidth ~ rateHz)
      y_p scaled back to unit variance (analytic a/(2-a) gain, then an
      empirical zero-mean / unit-rms normalisation over the partial's active
      frames when it has >= 4 of them, so the realised rms deviation equals
      the requested depth)
      amp'[f] = amp[f] * 10^(w_p * shimmerDb * y_p[f] / 20) * k_amp
      frq'[f] = frq[f] * 2^(w_p * jitterCents * y_p[f] / 1200) * k_frq

    k_amp / k_frq = exp(-sigma^2 / 2) in natural-log units are the lognormal
    mean corrections, so the expected amplitude / frequency equals the
    original.  Gaussians are Box-Muller from a splitmix64 hash of (seed,
    partial, frame, channel): deterministic, order independent, no std::rand.
    Separate noise streams drive amplitude and frequency.

    w_p is the rank emphasis (Torvmark): the 3 lowest partials by mean
    frequency get weight 1, all others 1 - lowPartialEmphasis.

    Only active frames (amp > 0 and frq > 0) are modulated.  pha,
    frq_slope / amp_slope and band_energy are left untouched: for small
    depths the stored phases remain approximately valid (the per-frame
    frequency deviation is a few cents), callers wanting exact phase
    coherence may drop them.
*/
struct MicroModulationParams
{
    double shimmerDb = 0.0;      // 0..6 dB rms amplitude deviation
    double jitterCents = 0.0;    // 0..50 cents rms frequency deviation
    double rateHz = 4.0;         // modulation bandwidth, 0.5..15 (4 Hz = peak fluctuation sensitivity)
    double correlation = 0.3;    // 0 independent .. 1 fully shared across partials
    double lowPartialEmphasis = 0.5; // 0: all partials full depth; 1: only the 3 lowest partials
    std::uint64_t seed = 1;

    [[nodiscard]] bool active() const noexcept { return shimmerDb > 0.0 || jitterCents > 0.0; }
};

/** The modulated copy; null when the params are inactive or the model is empty. */
[[nodiscard]] AtsSoundPtr applyMicroModulation(const ATS_SOUND* sound, const MicroModulationParams& params);

} // namespace ats

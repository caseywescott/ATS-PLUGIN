#pragma once

#include "AtsModel.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ats
{

/**
    Bark-band residual noise with controllable density.

    ATSA's residual-only path (synthesis.c, do_synthesis / the grain path)
    renders each of the 25 critical bands as ONE sinusoid at the band centre
    whose amplitude is ENG_RMS(band_energy) = sqrt(e / (window_size * 0.04)),
    linearly interpolated between frames, multiplied by `randi` (uniform noise
    in [-1, 1], linearly interpolated, renewed at the band-width rate) and by
    the residual gain.  That is a sinusoid with a random, piecewise-linear
    amplitude: a narrow-band noise whose envelope fluctuates strongly.

    Hanna & Desainte-Catherine (DAFx-00, "Statistical approach for sound
    modeling") model noise as a sum of sinusoids with random amplitudes and
    phases per band; Hanna (DAFx-01, "Influence of frequency distribution on
    intensity fluctuations of noise") shows that the intensity fluctuation of
    such a band falls as the number of sinusoids per band grows: a few
    sinusoids give sparse, "grainy" noise, many give dense, smooth noise.

    This renderer generalises the ATSA path to N = carriersPerBand sinusoids
    per band at deterministic jittered frequencies

        f_k = low + (k + 0.5 + jitter_k) / N * (high - low),   jitter_k in [-0.3, 0.3]
        (jitter_k = 0 for N = 1, so density 1 is exactly ATSA's band centre)

    each with its own randi amplitude noise renewed at (band width / N) Hz, a
    random start phase, and amplitude ENG_RMS * gain / sqrt(N).  Because the
    randi streams and start phases are independent, the expected band power
    is N * (A^2 / N) * E[sin^2] * E[randi^2] = A^2 / 9 for any N, so the band
    level matches the single-carrier reference (bandReferenceRms) while the
    envelope fluctuation shrinks with N.  (E[randi^2] = 2/9 for a linearly
    interpolated uniform; E[sin^2] = 1/2.)

    What density changes, measured on a single steady band (see the test):
    density 1 is ATSA's sinc^2-shaped hump centred in the band (out-of-band
    leakage about -26 dB, in-band flatness ~0.8); density 4 / 16 fill the
    band progressively (-43 / -61 dB, flatness ~0.9 / ~0.97) and sound
    denser and less tonal.  The short-term (10 ms) envelope fluctuation is
    about the same for every density: a signal that fills a band of width
    B observed over T seconds cannot fluctuate less than Gaussian noise,
    ~1/(2 sqrt(B T)), and ATSA's single randi carrier is already at that
    floor.  "Grain" at low density therefore comes from the narrower,
    peaked spectrum and the fast (band-rate) randi AM, not from a larger
    envelope variance.

    All randomness is a splitmix64 hash of (seed, band, carrier, segment), so
    a render is deterministic and independent of call order.
*/
struct ResidualSynthParams
{
    double sampleRate = 44100.0;
    int carriersPerBand = 1;            // 1..64
    double gain = 1.0;                  // residual gain (ATSA's `ramp`)
    double startTime = 0.0;             // seconds in model time
    double duration = 0.0;              // 0 = to the end of the model
    double timeFactor = 1.0;            // uniform stretch of the frame axis
    std::uint64_t seed = 0x5EED;
    bool skipBandsWithPartials = false; // a band holding an active partial in the frame contributes nothing
                                        // (the partials carry that energy in ATSA's combined path)
};

/**
    Renders the model's band_energy as band-limited noise and ADDS it into
    `out` (grown with zeros to the render length if shorter).  The render
    length is duration * timeFactor * sampleRate samples (duration 0 = from
    startTime to the end of the model).  Returns the number of samples
    written; 0 for a null / noiseless model or empty range.
*/
std::size_t renderResidualBands(const ATS_SOUND* sound, const ResidualSynthParams& params, std::vector<double>& out);

/**
    Expected RMS of a steady band with energy `bandEnergy` under this
    renderer at gain 1 and any density: ENG_RMS(e, windowSize) / 3, where
    1/3 = sqrt(E[sin^2] * E[randi^2]) = sqrt(1/2 * 2/9).  For level
    calibration and tests.
*/
[[nodiscard]] double bandReferenceRms(double bandEnergy, int windowSize);

} // namespace ats

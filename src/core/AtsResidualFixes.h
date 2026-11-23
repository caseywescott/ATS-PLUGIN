#pragma once

/*
 * AtsResidualFixes — three corrective passes for the stochastic (residual)
 * part of an ATS model, after X. Serra, "Musical Sound Modeling with Sinusoids
 * plus Noise", in: Musical Signal Processing (Roads et al., eds.), Swets &
 * Zeitlinger, 1997, §"Residual analysis" / §"Analysis problems":
 *
 *  1. clampResidualToSource — the residual is the original minus the
 *     deterministic re-synthesis; when the partial tracker mis-tracks an
 *     attack the subtraction *adds* energy, so the residual must never carry
 *     more band energy than the original signal itself. We measure the source's
 *     Bark-band energy per frame with the model's own window/hop (same value
 *     convention as ATSA's residual-analysis.c, see the .cpp) and clamp.
 *  2. highPassResidual — residual analyses carry spurious low-frequency energy
 *     (DC drift, tracking error at the lowest partials); Serra recommends
 *     high-passing the residual. We apply a smooth log-frequency taper to the
 *     per-band energies instead of filtering audio.
 *  3. reverseFrames / reversedSamples — analysing the sound backwards in time
 *     lets the tracker lock onto partials in the steady state before it reaches
 *     the (now final) attack. reversedSamples feeds the analyser; reverseFrames
 *     flips the resulting model back to forward time.
 */

#include "AtsModel.h"

#include <cstddef>
#include <vector>

extern "C" {
#include "atsa.h"
}

namespace ats
{

struct ResidualClampResult
{
    int framesChanged = 0;               // frames in which at least one band was clamped
    int bandsChanged = 0;                // (band, frame) cells clamped
    double energyRemovedFraction = 0.0;  // removed / total band energy before the clamp
};

/** Serra: the residual must never carry more energy than the original in any
 *  band/frame. Computes the source's Bark-band energy per frame (Hann-windowed
 *  FFT centred at time[0][f], window = sound->window_size at `sampleRate`,
 *  |X|^2 summed per band and normalised to ATSA's band_energy convention — see
 *  the .cpp for the exact mapping) and sets
 *      band_energy[b][f] = min(band_energy[b][f], headroom * source[b][f]).
 *  In place. `samples` is the mono original the model was analysed from.
 *  Does nothing (zero result) when the model has no residual or inputs are invalid. */
ResidualClampResult clampResidualToSource(ATS_SOUND* sound,
                                          const double* samples,
                                          std::size_t sampleCount,
                                          int sampleRate,
                                          double headroom = 1.0);

/** Serra: residual analyses carry too much low-frequency energy. Attenuates the
 *  band energies whose centre frequency lies below `cutoffHz` with a smooth
 *  (raised-cosine in log2 frequency) taper: energy gain 0 at and below
 *  cutoffHz / 2^transitionOctaves, 1 at and above cutoffHz. In place. Returns
 *  true if any value changed. Note the taper is multiplicative, so a second call
 *  attenuates bands inside the transition region again (like running a real
 *  filter twice); fully stopped and fully passed bands are idempotent. */
bool highPassResidual(ATS_SOUND* sound, double cutoffHz, double transitionOctaves = 1.0);

/** Serra: analysing backwards in time lets the tracker lock onto partials before
 *  it reaches the attack. Returns a new sound whose per-frame arrays (frq, amp,
 *  smr, res, band_energy, and frq_slope / amp_slope negated) are in reversed
 *  frame order, with time[0] regenerated as an increasing axis of the same hop,
 *  phases zeroed and has_slopes left as is. Empty pointer on null input. */
[[nodiscard]] AtsSoundPtr reverseFrames(const ATS_SOUND* sound);

/** Convenience for reverse analysis: returns `samples` reversed. */
[[nodiscard]] std::vector<double> reversedSamples(const double* samples, std::size_t sampleCount);

} // namespace ats

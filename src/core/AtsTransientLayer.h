#pragma once

#include <array>
#include <vector>

extern "C" {
#include "atsa.h"
}

#include "AtsTransientSmear.h"

namespace ats
{

/** One extracted transient event: the attack energy that frame-rate envelope
 *  interpolation would otherwise temporally smear, stored additively alongside
 *  the sinusoidal + band-noise model (see DSP_FIDELITY_PRD §6.5).
 *
 *  All fields are zero-initialized, so a default-constructed event injects
 *  nothing — loaded/legacy models with no transient layer reproduce exactly. */
struct AtsTransientEvent
{
    double timeSeconds = 0.0;   // onset time (model frame time)
    int frameIndex = 0;         // onset frame f0
    int preFrame = 0;           // attack-window start
    int peakFrame = 0;          // attack-window end (post-onset energy peak)
    double windowSeconds = 0.0; // attack-window duration (peakFrame - preFrame)
    double strength = 0.0;      // detection strength D[f0]

    // Attack energy GAINED across the window, each clamped to the model's own
    // in-window maximum (dynamic-range bound — cannot inject more than present).
    double deterministicEnergy = 0.0;                      // from partial amps
    std::array<double, ATSA_CRITICAL_BANDS> bandEnergy {}; // per-Bark residual
};

/** Re-injection tuning. `gain` scales the total injected energy (0 = silent,
 *  1 = the full extracted attack energy). */
struct TransientLayerParams
{
    double gain = 1.0;
    double attackSeconds = 0.003;   // raised-cosine rise up to the onset peak
    double maxDecaySeconds = 0.040; // cap on the post-onset decay tail
    unsigned int seed = 0x5eed1234u; // deterministic band-noise phases
};

/** Total energy carried by an event (sum of band + deterministic descriptors). */
[[nodiscard]] double transientEventEnergy(const AtsTransientEvent& event);

/** Extract a transient event per onset: the net attack-energy rise in each Bark
 *  band and across the partials, clamped to the in-window maximum so the layer
 *  is bounded by the model's own dynamic range. */
[[nodiscard]] std::vector<AtsTransientEvent> extractTransientLayer(
    const ATS_SOUND* sound,
    const std::vector<AtsOnset>& onsets);

/** Render the transient layer into `out` (length `nSamples`): a critical-band-
 *  shaped, energy-normalized burst per event, windowed with a fast raised-cosine
 *  attack and a bounded decay so it tapers to zero at both edges (no clicks).
 *  Zeros `out` first, then sums each event additively. */
[[nodiscard]] bool renderTransientLayer(const std::vector<AtsTransientEvent>& events,
                                        const TransientLayerParams& params,
                                        int sampleRate,
                                        double* out,
                                        int nSamples);

} // namespace ats

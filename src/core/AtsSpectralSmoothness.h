#pragma once

#include "AtsModel.h"

namespace ats
{

/**
    Spectral-smoothness cleanup of a harmonic model, after Klapuri (2003),
    "Multiple fundamental frequency estimation based on harmonicity and
    spectral smoothness" (IEEE Trans. Speech & Audio Processing 11(6)).
    Klapuri observes that the amplitude envelope across the harmonics of one
    source is smooth, and that a harmonic sticking far above its neighbours
    is usually a leaked or doubled component from another source.  He
    replaces each harmonic amplitude by min(amplitude, smoothed amplitude),
    where the smoothed value is a moving average over neighbouring
    harmonics.

    Here, per frame, every active partial gets a harmonic index
    h = round(f / f0) >= 1 (f0 from params.fundamentalHz or the model's
    estimated fundamental).  Its neighbours are the other active partials
    within +/- one octave of h (0 < |log2(h'/h)| < 1), weighted
    triangularly, w = 1 - |log2(h'/h)|, so that weight falls linearly to 0
    at one octave.  The smooth reference is the triangular-weighted mean of
    the neighbour amplitudes, taken side by side: the weighted mean of the
    lower neighbours and that of the upper neighbours (each with its
    weighted-mean log2 h position) are interpolated linearly in the
    (log2 h, dB) plane at the partial's own log2 h.  This two-sided form
    keeps any log-linear harmonic envelope (for instance 1/h, -6 dB/oct) a
    fixed point, whereas a one-sided mean would pull a decaying envelope
    down at its low end.  Partials without a neighbour on both sides (in
    particular the fundamental and the second harmonic, whose only lower
    neighbour sits exactly one octave away) are left untouched.

    amp' = amp + strength * (min(amp, reference) - amp), with the reduction
    limited to maxReductionDb.  Partials are never boosted.
*/
struct SpectralSmoothnessParams
{
    double strength = 1.0;      // 0..1
    double fundamentalHz = 0.0; // 0 -> estimate from the model
    double maxReductionDb = 24.0;
};

/** The smoothed copy (amps changed, everything else as in the source).
    Null when strength <= 0 or the model is empty. */
[[nodiscard]] AtsSoundPtr applySpectralSmoothness(const ATS_SOUND* sound, const SpectralSmoothnessParams& params);

} // namespace ats

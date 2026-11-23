#pragma once

#include "AtsModel.h"

#include <cstdint>

namespace ats
{

/**
    Rough and growl voice emulation in the partial domain, after Loscos &
    Bonada (DAFx-04, "Emulating rough and growl voice in spectral domain").
    Both effects add sub-harmonic partials to a harmonic model; the originals
    are left untouched and the new partials are appended after them.

    rough: every active partial with f in [f0, maxHz] gets a copy at
      f / divisor (period doubling for divisor 2, the classic "rough"
      diplophonia) with amplitude gainDb relative to the source.  Loscos &
      Bonada model the irregularity of the vocal folds with a slowly varying
      jitter of the sub-harmonic period and amplitude, so each copy carries a
      per-frame deterministic jitter of +/- periodJitterPercent on frequency
      and +/- ampJitterDb on amplitude: a hash of (seed, partial, frame) gives
      uniform noise in [-1, 1] which is smoothed along frames by a one-pole
      low-pass at about 20 Hz (the bounds still hold, the typical excursion
      is smaller than the maximum).  One copy per source partial; copies that
      would fall below 20 Hz are not created.

    growl: between consecutive harmonics m and m + 1 (by harmonic index
      round(f / f0)), growlOrder - 1 sub-harmonics are added at
      f0 * (m + k / growlOrder), k = 1 .. growlOrder - 1, realised as the
      linear interpolation of the two neighbouring harmonics' frequencies so
      the series follows the model's own inharmonicity.  Their amplitude is
      the dB-domain interpolation of the two neighbours' amplitudes plus
      gainDb (negative: below the harmonics).  A sub-harmonic is silent in
      frames where either neighbouring harmonic is inactive.

    The result is a new sound with partials + addedPartials partials, every
    per-frame array and the band energies copied, slopes dropped
    (has_slopes = 0), phases of the new partials zero and `av` filled with
    the per-partial mean frequency / amplitude.
*/
enum class SubharmonicMode
{
    rough,
    growl
};

struct SubharmonicParams
{
    SubharmonicMode mode = SubharmonicMode::rough;
    int divisor = 2;                  // rough: f / divisor (>= 2)
    int growlOrder = 4;               // growl: sub-harmonics per gap + 1 (2..5)
    double gainDb = -6.0;             // added partial level relative to the source
    double periodJitterPercent = 3.0; // rough: +/- frequency jitter
    double ampJitterDb = 3.0;         // rough: +/- amplitude jitter
    double maxHz = 8000.0;            // rough: highest source partial copied
    double fundamentalHz = 0.0;       // 0 -> estimate from the model
    std::uint64_t seed = 7;
};

struct SubharmonicResult
{
    AtsSoundPtr sound;
    int addedPartials = 0;
};

/** Null sound when the model is empty or nothing could be added. */
[[nodiscard]] SubharmonicResult applySubharmonics(const ATS_SOUND* sound, const SubharmonicParams& params);

} // namespace ats

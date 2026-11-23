#pragma once

#include "AtsModel.h"

namespace ats
{

/**
    Character-preserving transposition in the model domain.

    A plain frequency factor moves every partial and drags the spectral
    envelope (formants) with it, which is why large transpositions sound
    like a different, smaller or larger instrument.  Serra & Bonada (DAFx-98,
    "Sound transformations based on the SMS high level attributes") note that
    f0, partial count and spectral tilt co-vary in natural sounds, and
    O'Leary & Griffith (DAFx-24, "timbral consistency across pitch") measure
    which descriptors should stay put.  This transform scales the partial
    frequencies and then re-shapes the amplitudes so the envelope stays where
    it was:

    - preserveEnvelope (formant preservation, Herrera & Bonada): for each
      frame a BarkSpectralEnvelope E (True Envelope) is fitted to the frame's
      active partials and amp' = amp * E(f * factor) / E(f).  The ratio is
      floored at -48 dB and capped at +12 dB so a partial moving into a
      spectral hole fades rather than vanishes and one moving under a
      formant does not amplify analysis noise.  Frames with fewer than three
      active partials are scaled plainly (no envelope).
    - tiltDbPerOctavePerOctave: an additional spectral tilt of
      tilt * log2(factor) dB per octave about pivotHz, evaluated at the
      transposed frequency; a negative value darkens the sound as it goes
      up, mimicking the natural loss of high partials.
    - ceiling: partials transposed above ceilingHz (0 -> 0.95 * srate / 2)
      fade out linearly in log-frequency over ceilingFadeOctaves and are
      silent beyond.
*/
struct TransposeCharacterParams
{
    double frequencyFactor = 1.0;
    bool preserveEnvelope = true;
    double tiltDbPerOctavePerOctave = 0.0;
    double pivotHz = 1000.0;
    double ceilingHz = 0.0;
    double ceilingFadeOctaves = 0.25;

    /** True when the factor is not 1 and at least one of envelope
        preservation, tilt or an explicit ceiling is requested. */
    [[nodiscard]] bool active() const noexcept;
};

/**
    The transposed copy: frq scaled by the factor, amps re-shaped as above.
    Phases are zeroed and within-frame slopes dropped.  Null when the params
    are inactive or the model is empty; the caller then renders the original
    with its own frequency factor.  When a copy is returned the caller must
    render it with frequency factor 1.
*/
[[nodiscard]] AtsSoundPtr applyTransposeCharacter(const ATS_SOUND* sound, const TransposeCharacterParams& params);

} // namespace ats

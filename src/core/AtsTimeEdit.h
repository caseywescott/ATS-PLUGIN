#pragma once

#include "AtsModel.h"

namespace ats
{

/**
    Structural time edits on an ATS model (adapted from pyats clipboard.py).
    Each returns a new model with a regenerated time axis (frame k at
    k * frame_size / srate) and recomputed partial averages; the input is
    never modified.  Frame ranges are inclusive and clamped to the model.

    paste inserts the clip at `atFrame` (later frames shift right) and
    crossfades over `crossfadeFrames` at each seam: equal-power for
    amplitude, linear for residual band energy, and linear for frequency
    where both sides of a seam are active (pyats blends toward 0 Hz when a
    neighbour is silent, which glides the partial).
*/
class AtsTimeEdit
{
public:
    /** Frames [first, last] as a new model; null for an empty range. */
    [[nodiscard]] static AtsSoundPtr copy(const ATS_SOUND* sound, int first, int last);
    /** The model without frames [first, last]; null if nothing would remain. */
    [[nodiscard]] static AtsSoundPtr remove(const ATS_SOUND* sound, int first, int last);
    /** Only frames [first, last]; the same as copy(). */
    [[nodiscard]] static AtsSoundPtr crop(const ATS_SOUND* sound, int first, int last)
    { return copy(sound, first, last); }
    /** `clip` inserted at `atFrame`; null when the partial counts differ. */
    [[nodiscard]] static AtsSoundPtr paste(const ATS_SOUND* sound, const ATS_SOUND* clip,
                                           int atFrame, int crossfadeFrames);
};

} // namespace ats

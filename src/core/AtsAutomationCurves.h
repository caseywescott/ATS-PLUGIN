#pragma once

#include "AtsModel.h"

#include <vector>

namespace ats
{

/** A breakpoint curve over normalised output time [0, 1]; linear between
    points, held beyond the ends.  Empty = the curve's identity value. */
struct AutomationCurve
{
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
    };
    std::vector<Point> points;

    [[nodiscard]] bool empty() const noexcept { return points.empty(); }
    /** Value at x; `identity` when the curve is empty. */
    [[nodiscard]] double valueAt(double x, double identity) const noexcept;
    /** Sorted by x, x and y clamped, at most kMaxPoints; NaN points dropped. */
    void normalise(float minY, float maxY);

    static constexpr std::size_t kMaxPoints = 64;
};

/**
    Per-sound automation curves applied at render time (adapted from pyats
    timewarp.py, pitchmap.py and spectralwarp.py).  All are over normalised
    output time:

    - timeWarp: output position -> analysis position (both 0..1), same total
      length; the empty curve is the identity.  Plain speed changes stay with
      the time-stretch parameter.
    - pitchSemitones: transposition in semitones, clamped to +/-24.
    - spectralWarpGamma: f' = anchor * (f / anchor)^gamma, gamma clamped to
      [0.5, 2]; gamma > 1 spreads partials sharp of the anchor, < 1 compresses
      them toward it.  anchorHz <= 0 uses the model's estimated fundamental.
*/
struct SoundCurves
{
    AutomationCurve timeWarp;
    AutomationCurve pitchSemitones;
    AutomationCurve spectralWarpGamma;
    double warpAnchorHz = 0.0;

    static constexpr float kMaxSemitones = 24.0f;
    static constexpr float kMinGamma = 0.5f;
    static constexpr float kMaxGamma = 2.0f;

    /** True when any curve changes the sound. */
    [[nodiscard]] bool active() const noexcept;
    /** Applies normalise() to every curve with its range. */
    void normalise();
};

class AtsAutomationCurves
{
public:
    /**
        The model resampled through the time warp and re-pitched / warped
        per frame.  Phases are invalid afterwards (render without them) and
        within-frame slopes are dropped.  Null when the curves are inactive
        or the model is empty; the caller then renders the model as is.
    */
    [[nodiscard]] static AtsSoundPtr apply(const ATS_SOUND* sound, const SoundCurves& curves);

    /** Median over frames of the lowest active partial frequency (pyats
        estimate_fundamental); 0 when the model has no active partial. */
    [[nodiscard]] static double estimateFundamental(const ATS_SOUND* sound);
};

} // namespace ats

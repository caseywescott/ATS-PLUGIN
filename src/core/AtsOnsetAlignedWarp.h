#pragma once

// Onset-aligned time warp for two-source morphing.
//
// Hatch, "High-Level Audio Morphing Strategies" (McGill, 2004), §4.2 temporal
// alignment: when two sounds are morphed, their attacks must coincide or the
// result smears both. Onsets of A and B are paired in order (the k-th with the
// k-th), each pair is given a target time on the morph output
//
//     t_k = alpha * (tB_k - tA_k) + tA_k                         (Hatch, eq. §4.2)
//
// and each source is warped piecewise-linearly so its onset lands at t_k. The
// output duration follows the same interpolation, D = alpha * (DB - DA) + DA.
// Attack protection: the ear resolves roughly 5 ms of attack detail, so a window
// of attackHoldSeconds after each onset is left unwarped (slope 1 in seconds,
// the source's own attack passes through unchanged) by inserting a second
// anchor at the end of the window. The local slope d(source time)/d(output
// time) is clamped to [1 / maxWarpRatio, maxWarpRatio] by moving anchors'
// source times minimally (alternating forward/backward projection with the
// 0 -> 0 and 1 -> 1 endpoints fixed) so no segment stretches or compresses a
// source beyond the ratio; an infeasible configuration (the total ratio DS / D
// itself outside the window) is clamped as closely as the endpoints allow.
//
// Inputs are onset times in seconds (from Smearnov detectOnsets:
// AtsOnset::frame * hop, converted by the caller) and the two durations. When
// the onset counts differ, onsets are paired greedily by nearest normalised
// time (t / duration); unpaired onsets are ignored and crossing pairs dropped
// so both warps stay monotonic.
//
// Output: two AutomationCurves in the SoundCurves::timeWarp convention
// (x = output time / D, y = source time / its duration, identity when empty).
// A warp whose anchors all lie on the diagonal is returned empty.

#include "AtsAutomationCurves.h" // AutomationCurve

#include <vector>

namespace ats
{

struct OnsetAlignedWarpParams
{
    double morphAmount = 0.5;         // alpha in [0, 1]: 0 = A's timing, 1 = B's timing
    double durationA = 0.0;           // seconds
    double durationB = 0.0;           // seconds
    double attackHoldSeconds = 0.005; // Hatch: ~5 ms of attack left unwarped after each onset
    double maxWarpRatio = 4.0;        // local slope clamp [1/ratio, ratio]
};

struct OnsetAlignedWarp
{
    AutomationCurve warpA;                  // output time (0..1 of D) -> A time (0..1 of durationA)
    AutomationCurve warpB;                  // output time (0..1 of D) -> B time (0..1 of durationB)
    double outputDuration = 0.0;            // D = alpha * (DB - DA) + DA, seconds
    std::vector<double> targetOnsetSeconds; // t_k on the output, one per paired onset
    int pairedOnsets = 0;
};

[[nodiscard]] OnsetAlignedWarp buildOnsetAlignedWarp(const std::vector<double>& onsetsASeconds,
                                                     const std::vector<double>& onsetsBSeconds,
                                                     const OnsetAlignedWarpParams& params);

} // namespace ats

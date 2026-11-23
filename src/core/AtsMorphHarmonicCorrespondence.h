#pragma once

// Harmonic-locked partial correspondence for spectral morphing.
//
// Hatch, "High-Level Audio Morphing Strategies" (McGill, 2004), §4.4 harmonic
// alignment; cf. Loris channelization (Fitz & Haken). Instead of pairing by
// frequency rank or nearest frequency inside a critical band, each active
// partial is labelled with its harmonic number h = round(f / f0) against its
// own source's fundamental, and partials of the two sources that carry the
// same label are paired. Pitch is normalised out of the comparison: harmonic h
// of A at f_A is compared against f_B * f0_A / f0_B, so a 200 Hz series and a
// 300 Hz series pair harmonic-for-harmonic (200<->300, 400<->600, ...) and the
// morph glides each harmonic as one partial rather than splitting it across
// neighbouring ranks. A pair is accepted when the normalised frequency ratio
// lies within [1/ratioThreshold, ratioThreshold] (Hatch: about one semitone
// for clean harmonic material, ~1.2 for noisy or inharmonic sources). Partials
// left over on either side pair with a zero-amplitude partner (Hatch: an
// unmatched partial fades against silence), emitted as UnmatchedSource /
// UnmatchedTarget records exactly like the rank / greedy builders.
//
// Octave wrap (Hatch §4.4): when the two fundamentals are an octave or more
// apart and octaveWrap is set, both sides are labelled against the lower
// fundamental so harmonic h of the lower source pairs with harmonic h / 2^k of
// the upper one at the same absolute frequency (the upper source's partials
// take even labels only). Without octaveWrap the pitch-normalised harmonic
// number pairing above applies.
//
// Fundamental estimation (when the options carry no f0 for a side): the median
// over frames of the lowest active frequency in the frame (pyats
// estimate_fundamental), refined by the amplitude-weighted mean of f / round(f /
// f0). When a side has fewer than two active partials or its estimate falls
// below 20 Hz, no harmonic labelling is reliable and both sides are labelled on
// a static grid of `gridHz` instead (Hatch: static-grid labelling when no pitch
// is available); usedGrid reports this.
//
// Lives in its own translation unit, like AtsMorphTransportCorrespondence, so it
// can be wired into the render path without touching the rank / greedy / matrix
// code.

#include "AtsMorphRecipe.h" // MorphBandAssignment, MorphCorrespondenceRecord

#include <vector>

namespace ats
{

struct HarmonicCorrespondenceOptions
{
    double fundamentalSourceHz = 0.0; // 0 -> estimate from the assignments
    double fundamentalTargetHz = 0.0; // 0 -> estimate from the assignments
    double ratioThreshold = 1.0595;   // Hatch: ~one semitone; noisy material needs ~1.2
    bool octaveWrap = false;          // Hatch: match h of A to h*2^k of B when an octave or more apart
    double gridHz = 100.0;            // Hatch: static-grid labelling when no f0 is reliable
};

struct HarmonicCorrespondenceResult
{
    std::vector<MorphCorrespondenceRecord> records;
    double fundamentalSourceHz = 0.0; // f0 used for the source labels (gridHz when usedGrid)
    double fundamentalTargetHz = 0.0; // f0 used for the target labels (gridHz when usedGrid)
    int matched = 0;
    int unmatchedSource = 0;
    int unmatchedTarget = 0;
    bool usedGrid = false;
};

// Matched records carry bandIndex from the source assignment, rank = harmonic
// label h, hasCostDiagnostics = true with logFrequencyCost = totalCost =
// |log(normalised ratio)|. Unmatched records carry their own side's band index
// and label, the opposite side left at zero amplitude. Records are ordered by
// label, matched pairs first (by source frequency), then unmatched source, then
// unmatched target partials.
[[nodiscard]] HarmonicCorrespondenceResult buildHarmonicLockedCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const HarmonicCorrespondenceOptions& options = {});

} // namespace ats

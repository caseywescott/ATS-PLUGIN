#pragma once

// Morph-domain adapter that turns the generic 1D optimal-transport solver
// (AtsMorphTransport) into a partial correspondence for spectral morphing
// (PRD 7.1). It is the drop-in alternative to buildFrequencyRankCorrespondence:
// same MorphBandAssignment inputs, but instead of pairing min(count) partials
// by frequency rank and abruptly birthing/killing the surplus, it moves
// *energy* between the source and target partial distributions per critical
// band along the log-frequency axis. Count mismatch is absorbed by splitting or
// merging a partial's energy, so a morph sweep is pop-free. Transport is run
// per band so each band's energy budget is conserved independently (the per-band
// energy bookkeeping the morph relies on).
//
// This lives in its own translation unit so it can be wired into the render path
// without disturbing the existing rank/greedy/matrix correspondence code.

#include "AtsMorphRecipe.h" // MorphBandAssignment, MorphCorrespondenceKind

#include <vector>

namespace ats
{

struct MorphTransportCorrespondenceOptions
{
    // Drop couplings whose transported energy is below this fraction of the
    // band's total energy, so a long tail of negligible split edges does not
    // accumulate. Applied per band.
    double minMassFraction = 1e-4;
};

// One weighted correspondence edge. Mirrors the source/target fields of
// MorphCorrespondenceRecord and adds `mass` = the transported energy
// (amplitude^2) on this edge. A Matched edge carries energy from a source
// partial to a target partial; UnmatchedSource / UnmatchedTarget edges carry the
// leftover energy that has no counterpart in the band (fade-out / fade-in).
struct MorphTransportEdge
{
    int bandIndex = -1;
    int sourcePartialIndex = -1;
    int sourceFrameIndex = -1;
    double sourceFrequencyHz = 0.0;
    double sourceAmplitude = 0.0;
    int targetPartialIndex = -1;
    int targetFrameIndex = -1;
    double targetFrequencyHz = 0.0;
    double targetAmplitude = 0.0;
    double mass = 0.0; // transported energy (amplitude^2) on this edge.
    MorphCorrespondenceKind kind = MorphCorrespondenceKind::Matched;
};

struct MorphTransportCorrespondence
{
    std::vector<MorphTransportEdge> edges;
    double transportCost = 0.0;    // sum mass * |d(log frequency)|.
    double transportedMass = 0.0;  // total matched (transported) energy.
    double sourceMass = 0.0;       // total candidate source energy.
    double targetMass = 0.0;       // total candidate target energy.
};

[[nodiscard]] MorphTransportCorrespondence buildOptimalTransportCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphTransportCorrespondenceOptions& options = {});

// Adapter to the existing render path: runs the optimal-transport correspondence
// and projects each MorphTransportEdge onto a MorphCorrespondenceRecord so it can
// be consumed identically to buildFrequencyRankCorrespondence's output. Each
// edge's transported energy (mass = amplitude^2) becomes a per-edge amplitude of
// sqrt(mass) on the participating side(s); a source/target partial split across
// several edges therefore conserves energy (sum of edge energies == matched
// energy). Matched edges carry both sides; UnmatchedSource/UnmatchedTarget edges
// fade out/in (the opposite side's amplitude is left at zero).
[[nodiscard]] std::vector<MorphCorrespondenceRecord> buildOptimalTransportRecords(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphTransportCorrespondenceOptions& options = {});

} // namespace ats

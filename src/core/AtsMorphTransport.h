#pragma once

// 1D optimal-transport correspondence for spectral morphing (PRD 7.1).
//
// Greedy nearest-neighbour or frequency-rank pairing produces abrupt
// birth/death of partials when the source and target spectra have different
// partial counts, which is audible as clicks/pops during a morph sweep. The
// optimal-transport (Monge-Kantorovich) plan on the perceptual frequency line
// instead moves *mass* (amplitude/energy) from the source distribution to the
// target distribution, splitting or merging a partial's mass across several
// counterparts. On the line, with any convex ground cost, the exact optimal
// plan is the monotone (north-west-corner) coupling of the two sorted
// distributions, computed in O(n + m) after the sort. It is deterministic,
// conserves mass, and preserves order (no partial crossings), which is exactly
// the pop-free, low-churn behaviour the morph wants.

#include <vector>

namespace ats
{

// One weighted sample of a spectrum on a perceptual frequency axis.
struct TransportPoint
{
    double position = 0.0; // perceptual coordinate (e.g. Bark or log-Hz); the
                           // caller chooses the axis. Cost is measured here.
    double mass = 0.0;     // amplitude or energy weight; must be >= 0.
    int index = -1;        // caller's index into its own partial list.
};

// One edge of the transport plan: `mass` units move from `sourceIndex` at
// `sourcePosition` to `targetIndex` at `targetPosition`. A single source point
// can appear in several couplings (its mass split across targets) and likewise
// a single target can receive from several sources (mass merged).
struct TransportCoupling
{
    int sourceIndex = -1;
    int targetIndex = -1;
    double mass = 0.0;
    double sourcePosition = 0.0;
    double targetPosition = 0.0;
};

struct TransportPlan
{
    std::vector<TransportCoupling> couplings;
    double sourceMass = 0.0;    // total input mass before normalization.
    double targetMass = 0.0;    // total output mass before normalization.
    double transportedMass = 0.0; // sum of coupling masses (== 1 when normalized).
    double transportCost = 0.0; // sum mass * |dposition| (1-Wasserstein, in plan units).
};

struct TransportOptions
{
    // Rescale each side to unit total mass before planning. This makes the
    // balanced OT well-defined even when the source and target carry different
    // total energy (count mismatch handled by mass splitting). When false, the
    // two totals must match for a complete plan; any surplus mass on the larger
    // side is left untransported (reported via transportedMass).
    bool normalizeMass = true;

    // Mass below this fraction of the (post-normalization) total is treated as
    // exhausted, avoiding a long tail of numerically-tiny couplings.
    double massEpsilon = 1e-12;
};

// Build the exact 1D optimal-transport plan between two weighted point sets.
// Points need not be pre-sorted; the result is sorted-stable and deterministic.
// Points with non-positive or non-finite mass are ignored.
[[nodiscard]] TransportPlan buildOptimalTransportPlan(
    const std::vector<TransportPoint>& source,
    const std::vector<TransportPoint>& target,
    const TransportOptions& options = {});

} // namespace ats

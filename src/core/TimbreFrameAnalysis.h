#pragma once

#include "SpectralEnvelope.h"
#include "SpectralEnvelopeEstimator.h"
#include "TimbreDescriptors.h"
#include "TimbreEnergyDistribution.h"
#include "TimbreFrameAdapter.h"

namespace ats
{

// Compute per-frame energy and noisiness descriptors and write them into
// `frame`. Operates on the validated frame samples emitted by
// validateEnvelopeFrameInput. The analyzer is JUCE-independent and
// allocation-bounded relative to the input partial/band counts; it never
// runs inside processBlock.
//
// Outputs (each field's status reflects available input):
//   * partialEnergy: sum of amplitude^2 across selected partials.
//   * residualEnergy: sum of band energy. MissingResidual when none present.
//   * combinedEnergy: partialEnergy + residualEnergy.
//   * noisiness: residualEnergy / combinedEnergy. MissingResidual when no
//     residual data is available; never inferred to zero.
//   * orphanResidualRatio: orphan-flagged residual energy divided by total
//     residual energy. MissingResidual when no residual data is available.
//   * bandwiseNoisiness: per-bin noisiness on the configured comparison grid.
//
// `harmonicToNoiseRatio` is reported as a compatibility view in dB:
//   HNR_db = 10 * log10(partialEnergy / residualEnergy)
// stored into combinedEnergy.coverage at present; it is reported back to the
// caller via the optional out pointer below for downstream consumers.
struct EnergyAnalysisResult
{
    double harmonicToNoiseRatioDb = 0.0;
    TimbreDescriptorStatus harmonicToNoiseStatus = TimbreDescriptorStatus::EmptyInput;
};

EnergyAnalysisResult analyzeEnergyAndNoisinessDescriptors(
    const ValidatedFrameSamples& samples,
    const TimbreDescriptorOptions& options,
    TimbreDescriptorFrame& frame);

// Compute centroid, spread, skewness, kurtosis, and rolloff descriptors on
// the partial, residual, and combined energy distributions. Uses the shared
// energy distribution helpers from TimbreEnergyDistribution.
//
// Sparse domains (fewer than two energy-bearing samples) report TooSparse
// for the higher-order moments; centroid and rolloff are still emitted in
// that case when at least one sample is present.
//
// All normalized moments are invariant to uniform gain. Hz-domain centroids,
// spreads, and rolloffs scale linearly with frequency, never with amplitude.
void analyzeCentroidMomentsDescriptors(
    const ValidatedFrameSamples& samples,
    const TimbreDescriptorOptions& options,
    TimbreDescriptorFrame& frame);

// Grid-shape policy used by analyzeGridShapeDescriptors. The defaults match
// the values serialized in the report schema; callers may override per call
// when running comparison sweeps.
struct GridShapeOptions
{
    // Floor added to grid bins before geometric/arithmetic mean computation
    // so flatness/crest/entropy are well-defined for sparse bins.
    double binFloor = 1.0e-12;
    // Threshold for the strong-peak / complexity counters. Bins whose energy
    // is at least `complexityThresholdRatio` times the maximum bin contribute
    // to the count.
    double complexityThresholdRatio = 0.1;
    // Maximum number of frequency bins used for the spectrum_flatness_vector
    // descriptor. The vector is built by grouping the comparison grid into
    // approximately equal sub-bands.
    int flatnessVectorBands = 8;
};

// Compute the grid-shape per-frame descriptors on the combined (partial +
// residual) energy distribution projected onto the configured comparison
// grid. The flatness, crest, entropy, decrease, strong_peak_ratio, and
// spectral_complexity descriptors live in the combined domain; the per-band
// spectrum_flatness_vector mirrors the MPEG-7 audio spectrum flatness shape.
//
// All normalized descriptors are invariant to uniform gain. Sparse grids
// report TooSparse rather than emitting an undefined zero. Floors,
// thresholds, and grid policy are serialized via the descriptor manifest
// units and the surrounding TimbreDescriptorOptions.
void analyzeGridShapeDescriptors(
    const ValidatedFrameSamples& samples,
    const TimbreDescriptorOptions& options,
    const GridShapeOptions& shapeOptions,
    TimbreDescriptorFrame& frame);

} // namespace ats

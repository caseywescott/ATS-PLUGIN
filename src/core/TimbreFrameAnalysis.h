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

// Options for the fitted-envelope tilt analyzer. The formula is named and
// versioned via TimbreDescriptorMetadata; no silent formula substitution.
struct EnvelopeTiltOptions
{
    // dB-floor below which an envelope amplitude is treated as the floor; the
    // analyzer converts amplitudes to dB via 20 * log10(max(amplitude, floor))
    double amplitudeFloor = 1.0e-9;
    // Reference frequency in Hz used as the regression pivot (log2 axis).
    double referenceHz = 1000.0;
    // Minimum number of usable envelope samples before reporting Available.
    int minSampleCount = 4;
    // Maximum frequency span (octaves) used in the fit. The fit considers the
    // intersection of the configured comparison axis range and any reliable
    // span reported by the envelope diagnostics.
    double maxOctaves = 12.0;
};

// Output diagnostics for the tilt analyzer. The descriptor itself goes into
// frame.tiltDbPerOctave; this struct exposes the auxiliary regression
// metadata so callers can route it into reports/UI without re-running the
// regression.
struct EnvelopeTiltDiagnostics
{
    double referenceHz = 0.0;
    double fitSpanOctaves = 0.0;
    double interceptDb = 0.0;
    double residualRmsDb = 0.0;
    int usedSampleCount = 0;
};

// Compute fitted-envelope tilt in dB-per-octave via robust least-squares
// regression on the uniformly sampled envelope. `sampler` is the same
// evaluator function used by sampleFittedEnvelopeOntoGrid (typically
// SpectralEnvelopeEstimator::evaluate); pass nullptr to fall back to
// MissingEnvelope.
//
// Returns MissingEnvelope when the envelope is unreliable or sparser than
// minSampleCount; returns TooSparse when sample selection yields fewer than
// two usable points; otherwise returns Available with regression confidence
// derived from the residual fit error.
EnvelopeTiltDiagnostics analyzeFittedEnvelopeTilt(
    const FrameEnvelope& envelope,
    const TimbreDescriptorOptions& options,
    const EnvelopeTiltOptions& tiltOptions,
    const EnvelopeSamplerFn& sampler,
    TimbreDescriptorFrame& frame);

// Options for envelope deviation / adjacent-partial irregularity. The
// irregularity formula is named via TimbreDescriptorMetadata's
// adjacent_partial_irregularity entry; this struct only carries numerical
// thresholds.
struct EnvelopeDeviationOptions
{
    // Amplitude floor used when computing dB deviations against the envelope.
    double amplitudeFloor = 1.0e-9;
    // Minimum number of partials before the deviation RMS is reported as
    // Available; sparser frames return TooSparse.
    int minPartialsForDeviation = 3;
    // Minimum number of partials before adjacent_partial_irregularity is
    // reported as Available. The named formula requires at least three
    // partials so the central-bin local mean is well-defined.
    int minPartialsForIrregularity = 3;
};

// Compute envelope-deviation RMS in dB for partials against the fitted
// envelope (sampler evaluates the envelope) and the named
// adjacent_partial_irregularity descriptor:
//
//     local_mean_i = (a_{i-1} + a_i + a_{i+1}) / 3
//     irregularity = sum |a_i - local_mean_i| / sum a_i
//
// (Computed over selected partials sorted by frequency in linear amplitude.)
// This is intentionally distinct from harmonic spectral deviation, which is
// not enabled until reliable F0 / harmonic assignment lands in a later
// commit.
//
// dominant_partial_fraction = max(energy_i) / sum(energy_i) is also computed
// here because it shares the same partial-sort plumbing.
void analyzeEnvelopeDeviationAndIrregularity(
    const ValidatedFrameSamples& samples,
    const TimbreDescriptorOptions& options,
    const EnvelopeDeviationOptions& deviationOptions,
    const FrameEnvelope& envelope,
    const EnvelopeSamplerFn& sampler,
    TimbreDescriptorFrame& frame);

} // namespace ats

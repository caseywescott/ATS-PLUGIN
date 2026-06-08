#pragma once

#include "SpectralEnvelope.h"
#include "TimbreDescriptors.h"
#include "TimbreEnergyDistribution.h"
#include "TimbreFrameAdapter.h"

#include <string>
#include <vector>

namespace ats
{

// Normalization policy applied to per-frame grids before flux / variation /
// stationarity computations. The selected policy is serialized into reports
// so two runs over the same series must produce byte-identical output.
enum class SeriesFrameNormalization
{
    // No frame-wise normalization. Flux / variation see absolute energy.
    None,
    // Divide each frame grid by the L1 norm (sum of bin energies) so flux
    // becomes a shape-change metric independent of frame level.
    L1,
    // Divide each frame grid by the L2 norm. Useful when comparing cosine-
    // distance metrics where vectors should sit on a unit sphere.
    L2,
};

// Per-frame validated input for the series analyzer. The series analyzer
// builds one of these arrays from a TimbreSeriesInput by routing each
// EnvelopeFrameInput through validateEnvelopeFrameInput; callers that
// already have ValidatedFrameSamples can pass them directly.
//
// Each entry carries the absolute frame time so transient-boundary
// resets and flux computations remain valid even when frames are missing
// or unevenly spaced; the analyzer rejects unevenly spaced frames per the
// frameHopSeconds tolerance in TimbreSeriesAlignmentOptions.
struct SeriesFrameContext
{
    ValidatedFrameSamples samples;
    double timeSeconds = 0.0;
    bool transientBoundary = false;
};

struct TimbreSeriesAlignmentOptions
{
    // Tolerance for verifying that consecutive frame hops match
    // seriesOptions.frameHopSeconds. Specified as a fraction of frameHop;
    // 0.05 means up to 5 % drift is tolerated per hop.
    double frameHopToleranceFraction = 0.05;
    // Minimum number of frames required before the analyzer reports
    // Available; series shorter than this report TooSparse for series
    // descriptors.
    int minimumFrameCount = 2;
    // Normalization policy applied to per-frame grids before flux/variation.
    SeriesFrameNormalization frameNormalization = SeriesFrameNormalization::L1;
    // Whether to reset/gate cross-frame descriptors at transient boundaries.
    // Defaults match TimbreDescriptorSeriesOptions::resetFluxAtTransientBoundaries
    // but the alignment layer needs its own copy because that option is set
    // per-call.
    bool resetAtTransientBoundaries = true;
    // Comparison grid size override (defaults to TimbreDescriptorOptions's
    // comparisonGridSize when <= 0).
    int comparisonGridSizeOverride = 0;
};

// Result of validating and aligning a series. status reports the highest-
// severity issue found:
//   * Available           - alignment ok.
//   * EmptyInput          - zero frames.
//   * TooSparse           - fewer frames than minimumFrameCount.
//   * Undefined           - source-id mismatch, axis mismatch, frame-hop drift
//                           outside tolerance, or non-monotonic frame times.
struct SeriesAlignmentReport
{
    TimbreDescriptorStatus status = TimbreDescriptorStatus::EmptyInput;
    int frameCount = 0;
    int transientBoundaryCount = 0;
    double frameHopSeconds = 0.0;
    double durationSeconds = 0.0;
    std::string errorMessage;
};

// Validate a frame list against the documented contract before computing any
// series descriptor. The contract:
//   * frame times are strictly increasing,
//   * consecutive hops match seriesOptions.frameHopSeconds within tolerance,
//   * every frame's sourceModelId matches the first frame's sourceModelId,
//   * every ValidatedFrameSamples uses the same residualBandsPresent and
//     waveformAvailable assumption as the first frame (downstream descriptors
//     may still gate per-frame on individual fields).
// Returns an alignment report; the analyzer's status reflects whether it can
// proceed.
SeriesAlignmentReport validateSeriesAlignment(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreDescriptorSeriesOptions& seriesOptions,
    const TimbreSeriesAlignmentOptions& alignmentOptions);

// Build the per-frame combined comparison grid that flux / variation /
// stationarity descriptors operate on, applying the configured frame
// normalization. The vector length equals the input frame count; each
// inner vector has alignmentOptions.comparisonGridSizeOverride (or the
// options.comparisonGridSize default) bins. Bins are linear-energy values
// after normalization.
//
// Domain selects which energy distribution feeds the grid:
//   * Partials       - partial energy only
//   * ResidualBands  - residual band energy only
//   * Combined       - partial + residual energy
// Other domains return an empty result.
struct SeriesGridStack
{
    std::vector<std::vector<double>> grids;
    std::vector<double> frameEnergyBeforeNormalization;
    int gridSize = 0;
    TimbreDescriptorDomain domain = TimbreDescriptorDomain::Combined;
    SeriesFrameNormalization normalization = SeriesFrameNormalization::None;
};

SeriesGridStack buildSeriesGridStack(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreSeriesAlignmentOptions& alignmentOptions,
    TimbreDescriptorDomain domain);

// Convenience: convert a TimbreSeriesInput's EnvelopeFrameInput frames into a
// SeriesFrameContext list ordered by index, using validateEnvelopeFrameInput
// for each frame and the optional transient boundary marker vector. Frame
// times default to frame_index * frameHopSeconds when frameHopSeconds > 0.
std::vector<SeriesFrameContext> buildSeriesFrameContexts(
    const TimbreSeriesInput& series,
    const TimbreDescriptorOptions& options,
    const TimbreDescriptorSeriesOptions& seriesOptions,
    const std::vector<bool>& transientBoundaries = {});

// Top-level series analyzer entry point. Validates alignment, normalizes
// frames, and dispatches descriptor-specific helpers (added in later
// commits). For Commit 15 the entry point only validates and records source
// identity / frame hop / boundary count into the descriptor series; later
// commits fill the trajectory and scalar descriptors.
SeriesAlignmentReport analyzeTimbreDescriptorSeries(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreDescriptorSeriesOptions& seriesOptions,
    const TimbreSeriesAlignmentOptions& alignmentOptions,
    TimbreDescriptorSeries& output);

// Compute positive_flux and spectral_variation trajectories for the chosen
// domain (Partials / ResidualBands / Combined). The output trajectories have
// the same length as `frames`; entry [0] always has status TooSparse since
// flux is undefined without a predecessor, and any frame immediately after a
// transient boundary also reports TooSparse so flux is gated at marked
// boundaries.
//
// Trajectories follow the formulas in master spec section 5.7:
//
//     positive_flux_t = sqrt(sum(max(0, E_t[k] - E_(t-1)[k])^2))
//     spectral_variation_t =
//         1 - dot(E_t, E_(t-1)) / (norm(E_t) * norm(E_(t-1)))
//
// E_t is the per-frame grid after applying alignmentOptions.frameNormalization
// (so spectral_variation is shape-only when L1 / L2 normalization is used).
//
// Both vectors are written into `outFlux` and `outVariation` directly.
// status / confidence per entry follow the standard descriptor contract.
void computeFluxAndVariationTrajectories(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TimbreSeriesAlignmentOptions& alignmentOptions,
    TimbreDescriptorDomain domain,
    std::vector<TimbreDescriptorValue>& outFlux,
    std::vector<TimbreDescriptorValue>& outVariation);

// Options for the temporal envelope analyzer. Thresholds are absolute dB
// values referenced against the peak energy in the analysis window.
struct TemporalEnvelopeOptions
{
    // Lower threshold marking the start of the attack (dB below peak).
    double attackStartThresholdDb = -40.0;
    // Upper threshold marking the end of the attack (dB below peak).
    double attackEndThresholdDb = -3.0;
    // Threshold defining effective_duration / decay search bounds.
    double effectiveDurationThresholdDb = -40.0;
    // Minimum number of frames before temporal descriptors are reported.
    int minimumFrameCount = 3;
    // Maximum number of independent peaks tolerated. When more than one
    // peak exceeds the effective-duration threshold, the descriptors report
    // Undefined (the spec calls this "multi-event rejection").
    int maximumIndependentPeaks = 1;
    // Energy floor used when converting frame energy to dB.
    double energyFloor = 1.0e-18;
};

// Diagnostics returned alongside the temporal envelope analyzer so callers
// can inspect the inferred event boundaries without parsing the descriptor
// values.
struct TemporalEnvelopeDiagnostics
{
    double peakEnergy = 0.0;
    double peakTimeSeconds = 0.0;
    double attackStartSeconds = 0.0;
    double attackEndSeconds = 0.0;
    double decayStartSeconds = 0.0;
    double decayEndSeconds = 0.0;
    int independentPeakCount = 0;
};

// Compute the per-event temporal descriptors (temporal_centroid_seconds,
// log_attack_time, attack_slope_db_per_second, decay_slope_db_per_second,
// effective_duration_seconds) from the per-frame combined energy envelope
// derived from frames.
//
// The frame energy is the L1 norm of the Combined-domain grid (so partial +
// residual contributions are included), measured before any per-frame
// normalization. Event boundaries follow the documented threshold policy in
// TemporalEnvelopeOptions; descriptors return Undefined when multiple
// independent peaks are detected (multi-event rejection) and TooSparse when
// the series carries too few frames.
//
// Outputs are written directly into the descriptor series fields; the
// returned diagnostics describe the inferred event boundaries.
TemporalEnvelopeDiagnostics analyzeTemporalEnvelopeShape(
    const std::vector<SeriesFrameContext>& frames,
    const TimbreDescriptorOptions& options,
    const TemporalEnvelopeOptions& temporalOptions,
    TimbreDescriptorSeries& series);

// Options for the partial-track lifecycle analyzer. Onset and release
// boundaries are inferred from per-track amplitude crossings relative to
// each track's peak; the resulting offsets are weighted by track energy.
struct PartialLifecycleOptions
{
    // Amplitude threshold (linear amplitude, relative to the track peak) used
    // to mark onset and release for each track.
    double trackOnsetThresholdRatio = 0.1;
    double trackReleaseThresholdRatio = 0.1;
    // Minimum number of qualifying tracks required for the descriptor to
    // report Available. Below this, status is TooSparse.
    int minimumTrackCount = 2;
    // Minimum integrated amplitude on a track before it qualifies; tiny
    // tracks contribute nothing and are excluded from the energy-weighted
    // statistic.
    double minimumTrackAmplitudeIntegral = 1.0e-6;
};

// Diagnostics describing the inferred event onset/release references and
// the per-track sample counts that contributed.
struct PartialLifecycleDiagnostics
{
    double eventOnsetSeconds = 0.0;
    double eventReleaseSeconds = 0.0;
    int qualifyingTrackCount = 0;
    int onsetReferenceTrackId = -1;
    int releaseReferenceTrackId = -1;
};

// Compute partial_onset_asynchrony_seconds and
// partial_release_asynchrony_seconds. The analyzer:
//   1. Derives per-track peak amplitudes and onset/release times via the
//      configured thresholds (interpolated between adjacent track samples).
//   2. Picks the earliest onset time as the event onset reference and the
//      latest release time as the event release reference.
//   3. Computes the energy-weighted standard deviation of each track's
//      onset_offset_i = t_onset_i - t_event_onset (and similarly for
//      release), per master spec section 5.11.
//
// Tracks shorter than two valid samples, or with integrated amplitude below
// minimumTrackAmplitudeIntegral, are excluded. When fewer than
// minimumTrackCount tracks qualify, both descriptors report TooSparse.
// When no tracks are available, the analyzer reports EmptyInput.
PartialLifecycleDiagnostics analyzePartialTrackLifecycle(
    const std::vector<PartialTrack>& tracks,
    const PartialLifecycleOptions& lifecycleOptions,
    TimbreDescriptorSeries& series);

// Options for the partial modulation analyzer. Rate estimation uses zero-
// crossing analysis of each track's detrended trajectory; the operations
// remain bounded and JUCE-independent.
struct PartialModulationOptions
{
    // Sample-rate of the per-frame trajectory in Hz (1 / frameHopSeconds).
    // Set explicitly so the analyzer can clamp reported rates to the series
    // Nyquist.
    double trajectoryRateHz = 0.0;
    // Minimum number of valid track samples before a track contributes.
    int minimumSampleCount = 6;
    // Minimum number of qualifying tracks before the descriptor reports
    // Available; below this, the descriptor reports TooSparse.
    int minimumTrackCount = 1;
    // Minimum AM depth (relative to mean amplitude) before a track is
    // considered to carry modulation. Tracks with depth below this are
    // excluded from the energy-weighted mean.
    double minimumAmDepth = 0.05;
    // Minimum FM extent (Hz) before a track contributes to the FM rate.
    double minimumFmExtentHz = 1.0;
};

// Diagnostics for the modulation analyzer; useful for UI / report inspection.
struct PartialModulationDiagnostics
{
    int qualifyingAmTrackCount = 0;
    int qualifyingFmTrackCount = 0;
    double seriesNyquistHz = 0.0;
    double weightedAmDepth = 0.0;
    double weightedFmExtentHz = 0.0;
};

// Compute partial_am_rate_hz and partial_fm_rate_hz from track amplitude /
// frequency trajectories. The analyzer:
//   1. Detrends each track's amplitude trajectory by subtracting its mean.
//   2. Counts zero-crossings to estimate the dominant modulation rate; the
//      rate equals zero_crossings / (2 * duration).
//   3. Repeats step 2 on the frequency trajectory after subtracting its
//      mean to estimate the FM rate.
//   4. Reports the energy-weighted mean across qualifying tracks, clamped
//      to the series Nyquist rate.
//
// The analyzer reports TooSparse when no track has enough samples or when
// no track exceeds the configured modulation depth / extent gates. Tracks
// without meaningful modulation contribute zero to the weighted mean rather
// than corrupting it.
PartialModulationDiagnostics analyzePartialModulation(
    const std::vector<PartialTrack>& tracks,
    const PartialModulationOptions& modulationOptions,
    TimbreDescriptorSeries& series);

} // namespace ats

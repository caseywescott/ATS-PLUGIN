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

} // namespace ats

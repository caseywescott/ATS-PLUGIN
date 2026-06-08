#pragma once

#include "SpectralEnvelope.h"
#include "SpectralEnvelopeEstimator.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ats
{

// One validated partial sample. amplitude is linear, energy is amplitude^2.
// confidence and selected mirror the input EnvelopePartialPoint; selected
// partials with non-finite or sub-floor amplitude are dropped during
// validation.
struct PartialEnergySample
{
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    double energy = 0.0;
    double confidence = 1.0;
    bool selected = true;
};

// One validated residual band sample. energy is the stored band energy.
// orphanBand mirrors the input field so the orphan-residual-ratio descriptor
// can be computed without re-reading the source.
struct ResidualEnergySample
{
    double frequencyHz = 0.0;
    double energy = 0.0;
    double confidence = 1.0;
    bool orphanBand = false;
};

// Output of validateEnvelopeFrameInput. Carries the validated partial and
// residual lists plus coverage diagnostics; invalidPartialCount and
// invalidResidualCount equal the number of points dropped during validation.
struct ValidatedFrameSamples
{
    std::string sourceModelId;
    std::uint64_t frameIndex = 0;
    double sampleRate = 0.0;

    std::vector<PartialEnergySample> partials;
    std::vector<ResidualEnergySample> residualBands;

    double partialEnergyTotal = 0.0;
    double residualEnergyTotal = 0.0;
    double minPartialFrequencyHz = 0.0;
    double maxPartialFrequencyHz = 0.0;
    double minResidualFrequencyHz = 0.0;
    double maxResidualFrequencyHz = 0.0;

    int invalidPartialCount = 0;
    int invalidResidualCount = 0;

    bool waveformAvailable = false;
    bool residualBandsPresent = false;
    double fundamentalFrequencyHz = 0.0;
    double f0Confidence = 0.0;
};

// Honors selection masks and the amplitudeFloor. Partials with non-finite
// frequency/amplitude or amplitude below amplitudeFloor are dropped; the same
// finite-and-positive check is applied to residual bands.
//
// The output partials and residualBands are sorted by frequency ascending so
// downstream descriptors (rolloff, irregularity, etc.) can rely on the order
// without re-sorting.
ValidatedFrameSamples validateEnvelopeFrameInput(
    const EnvelopeFrameInput& frame,
    double amplitudeFloor = 1.0e-12);

// Raw partial-arrays-side extraction helpers. These intentionally accept
// pointer-and-length data so plugin code (which holds JUCE-flavored ATS_SOUND
// pointers) can call the adapter without dragging JUCE or ATSA headers into
// the descriptor core. The PluginProcessor's collectFrame lambda is the
// canonical caller and is replaced with this helper.
struct PartialExtractionInput
{
    int frameIndex = 0;
    int partialCount = 0;
    const double* const* amplitudeByPartial = nullptr; // amp[partial][frame]
    const double* const* frequencyByPartial = nullptr; // freq[partial][frame]
    double defaultConfidence = 1.0;
    double defaultSmr = 20.0;
};

struct ResidualExtractionInput
{
    int frameIndex = 0;
    int bandCount = 0;
    const double* const* energyByBand = nullptr; // band_energy[band][frame]
    std::function<double(int bandIndex)> bandCenterFrequencyHz;
    std::function<bool(int bandIndex)> bandIsOrphan; // optional; defaults to false
    double defaultConfidence = 1.0;
};

// Populates an EnvelopeFrameInput from raw partial + (optional) residual
// arrays. The output is the same shape produced by the plugin's local
// collectFrame lambda but is JUCE-independent and reusable.
void populateEnvelopeFrameInput(
    const PartialExtractionInput& partials,
    const ResidualExtractionInput* residual,
    double sampleRate,
    std::string sourceModelId,
    bool waveformAvailable,
    double fundamentalFrequencyHz,
    double f0Confidence,
    double amplitudeFloor,
    EnvelopeFrameInput& out);

// Track / series input model for descriptors that need partial identity
// across frames (partial onset/release asynchrony, AM/FM, coherence).
struct PartialTrackSample
{
    double timeSeconds = 0.0;
    double amplitude = 0.0;
    double frequencyHz = 0.0;
    bool present = false;
};

struct PartialTrack
{
    std::uint64_t trackId = 0;
    std::vector<PartialTrackSample> samples;
};

// Aggregate descriptor series input. Frames is the per-frame descriptor input
// list; tracks is the per-partial trajectory list; transientBoundaryTimes is
// an optional list of absolute times (in seconds) where the consumer should
// reset/gate cross-frame descriptors.
struct TimbreSeriesInput
{
    std::string sourceModelId;
    double frameHopSeconds = 0.0;

    std::vector<EnvelopeFrameInput> frames;
    std::vector<PartialTrack> tracks;
    std::vector<double> transientBoundaryTimes;
};

// Lightweight validation for a track set. Returns the number of tracks dropped
// (e.g. empty / all-absent / non-finite) and writes the kept tracks into out.
struct ValidatedTrackSet
{
    std::vector<PartialTrack> tracks;
    int droppedTrackCount = 0;
};

ValidatedTrackSet validatePartialTracks(
    const std::vector<PartialTrack>& tracks,
    double amplitudeFloor = 1.0e-12);

} // namespace ats

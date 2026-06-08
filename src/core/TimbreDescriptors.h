#pragma once

#include "SpectralEnvelope.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ats
{

// Stable enum identifying which input data populated a descriptor value.
// Serialized to and from the strings produced by toString / parseDomain.
enum class TimbreDescriptorDomain
{
    Partials,
    ResidualBands,
    Combined,
    FittedEnvelope,
    WaveformSpectrum,
    TemporalSeries
};

// Status reported alongside every descriptor value. A value carrying any
// status other than Available may not be treated as a numeric measurement.
enum class TimbreDescriptorStatus
{
    Available,
    EmptyInput,
    TooSparse,
    MissingResidual,
    MissingEnvelope,
    MissingReliableF0,
    MissingWaveform,
    Undefined,
    Unsupported
};

// Coverage record that accompanies an aggregated descriptor. Each field has a
// finite default so that an EmptyInput status is still serializable.
struct TimbreDescriptorCoverage
{
    int activePointCount = 0;
    double supportedFrequencySpanHz = 0.0;
    double representedEnergyRatio = 0.0;
};

// One descriptor measurement: numeric value, domain, status, confidence, and
// (optional) coverage. Vector-valued descriptors (e.g. flatness vector) use
// TimbreDescriptorVector below.
struct TimbreDescriptorValue
{
    double value = 0.0;
    TimbreDescriptorDomain domain = TimbreDescriptorDomain::Combined;
    TimbreDescriptorStatus status = TimbreDescriptorStatus::EmptyInput;
    double confidence = 0.0;
    TimbreDescriptorCoverage coverage{};
};

// Vector-valued descriptor (e.g. spectrum_flatness_vector, bandwise_noisiness,
// subband_centroids). Always carries the same status/confidence/coverage as
// its scalar siblings so downstream consumers can apply uniform gates.
struct TimbreDescriptorVector
{
    std::vector<double> values;
    TimbreDescriptorDomain domain = TimbreDescriptorDomain::Combined;
    TimbreDescriptorStatus status = TimbreDescriptorStatus::EmptyInput;
    double confidence = 0.0;
    TimbreDescriptorCoverage coverage{};
};

// Single resonance/formant peak descriptor record. Position is on the
// configured perceptual axis; bandwidth uses the same axis coordinate.
struct TimbreResonancePeak
{
    double frequencyHz = 0.0;
    double axisPosition = 0.0;
    double prominenceDb = 0.0;
    double bandwidthAxis = 0.0;
};

// Per-frame descriptor bundle. Fields are populated in dependency order in
// later commits; a fresh value defaults to EmptyInput so a partially populated
// frame is always serializable.
struct TimbreDescriptorFrame
{
    static constexpr std::uint32_t kVersion = 1;

    std::string sourceModelId;
    std::uint64_t frameIndex = 0;

    // Energy descriptors
    TimbreDescriptorValue partialEnergy{};
    TimbreDescriptorValue residualEnergy{};
    TimbreDescriptorValue combinedEnergy{};
    TimbreDescriptorValue noisiness{};
    TimbreDescriptorValue orphanResidualRatio{};
    TimbreDescriptorVector bandwiseNoisiness{};

    // Centroid / moment descriptors
    TimbreDescriptorValue partialCentroidHz{};
    TimbreDescriptorValue residualCentroidHz{};
    TimbreDescriptorValue combinedCentroidHz{};
    TimbreDescriptorValue combinedCentroidAxis{};
    TimbreDescriptorValue combinedSpreadHz{};
    TimbreDescriptorValue combinedSkewness{};
    TimbreDescriptorValue combinedKurtosis{};
    TimbreDescriptorValue rolloff85Hz{};
    TimbreDescriptorValue rolloff95Hz{};

    // Grid-shape descriptors
    TimbreDescriptorValue flatness{};
    TimbreDescriptorValue crest{};
    TimbreDescriptorValue entropyNormalized{};
    TimbreDescriptorValue spectralDecrease{};
    TimbreDescriptorValue strongPeakRatio{};
    TimbreDescriptorValue spectralComplexity{};
    TimbreDescriptorVector spectrumFlatnessVector{};

    // Envelope-relative descriptors
    TimbreDescriptorValue tiltDbPerOctave{};
    TimbreDescriptorValue envelopeDeviationRmsDb{};
    TimbreDescriptorValue adjacentPartialIrregularity{};

    // Contrast and resonance peaks
    TimbreDescriptorValue spectralContrastDb{};
    std::vector<TimbreResonancePeak> resonancePeaks{};
    TimbreDescriptorStatus resonancePeaksStatus = TimbreDescriptorStatus::EmptyInput;
    double resonancePeaksConfidence = 0.0;
    // Subband centroid vector (one value per spectral_contrast_db sub-band).
    // Same status / confidence semantics as TimbreDescriptorVector.
    TimbreDescriptorVector subbandCentroidsHz{};

    // Relative diagnostics
    TimbreDescriptorValue partialVsResidualCentroidOffsetHz{};
    TimbreDescriptorValue dominantPartialFraction{};

    // Harmonic descriptors (conditional on reliable F0)
    TimbreDescriptorValue harmonicity{};
    TimbreDescriptorValue inharmonicity{};
    TimbreDescriptorValue oddEvenRatio{};
    std::array<TimbreDescriptorValue, 3> tristimulus{};

    // Waveform-spectrum-domain descriptors (conditional on waveformAvailable)
    TimbreDescriptorValue cepstralPeakProminenceDb{};
    TimbreDescriptorValue highFrequencyContent{};
};

// Series-level descriptor bundle. Per-frame trajectories live in vectors
// indexed by frame position.
struct TimbreDescriptorSeries
{
    static constexpr std::uint32_t kVersion = 1;

    std::string sourceModelId;
    double frameHopSeconds = 0.0;

    std::vector<TimbreDescriptorFrame> frames;

    std::vector<TimbreDescriptorValue> positiveFlux;
    std::vector<TimbreDescriptorValue> spectralVariation;
    std::vector<TimbreDescriptorValue> centroidHzTrajectory;
    std::vector<TimbreDescriptorValue> noisinessTrajectory;

    TimbreDescriptorValue temporalCentroidSeconds{};
    TimbreDescriptorValue logAttackTime{};
    TimbreDescriptorValue attackSlopeDbPerSecond{};
    TimbreDescriptorValue decaySlopeDbPerSecond{};
    TimbreDescriptorValue effectiveDurationSeconds{};
    TimbreDescriptorValue centroidVelocityHzPerSecond{};
    TimbreDescriptorValue centroidAccelerationHzPerSecondSq{};
    TimbreDescriptorValue frameEnergyCoefficientOfVariation{};

    TimbreDescriptorValue partialOnsetAsynchronySeconds{};
    TimbreDescriptorValue partialReleaseAsynchronySeconds{};
    TimbreDescriptorValue partialAmRateHz{};
    TimbreDescriptorValue partialFmRateHz{};

    TimbreDescriptorValue residualSpectralVariation{};
    TimbreDescriptorValue residualStationarity{};

    TimbreDescriptorValue harmonicSpectralVariation{};
    TimbreDescriptorValue f0Jitter{};
    TimbreDescriptorValue amplitudeShimmer{};
};

// Options consumed by the per-frame analyzer. comparisonAxis is reused from
// the spectral envelope subsystem.
struct TimbreDescriptorOptions
{
    EnvelopeAxisSpec comparisonAxis{};
    int comparisonGridSize = 64;
    double amplitudeFloor = 1.0e-12;
    double minimumPeakProminenceDb = 3.0;
    double rolloffLowFraction = 0.85;
    double rolloffHighFraction = 0.95;
    bool includeResidualInCombined = true;
    bool enableHarmonicDescriptors = false;
    bool enableWaveformDescriptors = true;
    double minimumF0ConfidenceForHarmonic = 0.5;
};

// Options consumed by the series analyzer.
struct TimbreDescriptorSeriesOptions
{
    double frameHopSeconds = 0.0;
    double attackThresholdDb = -20.0;
    double effectiveDurationThresholdDb = -40.0;
    bool resetFluxAtTransientBoundaries = true;
    std::vector<bool> transientBoundaries;
};

// Stable descriptor-id metadata. Mirrors tests/fixtures/timbre_descriptor_manifest.json.
struct TimbreDescriptorMetadata
{
    const char* id = "";
    std::uint32_t formulaVersion = 0;
    TimbreDescriptorDomain domain = TimbreDescriptorDomain::Combined;
    const char* units = "";
    const char* weighting = "";
};

// Enum-string conversion. The string forms match the manifest and report schema
// vocabulary used in the Python checkers and exported JSON.
const char* toString(TimbreDescriptorDomain domain);
const char* toString(TimbreDescriptorStatus status);

bool parseTimbreDescriptorDomain(const std::string& text,
                                 TimbreDescriptorDomain& out);
bool parseTimbreDescriptorStatus(const std::string& text,
                                 TimbreDescriptorStatus& out);

// Lookup the stable metadata for a descriptor id. Returns nullptr if the id is
// unknown. Used by the serialization layer in Commit 8 to ensure the exported
// formula_version matches the C++ implementation.
const TimbreDescriptorMetadata* findTimbreDescriptorMetadata(const std::string& id);

// Returns all known descriptor metadata records in stable order.
const std::vector<TimbreDescriptorMetadata>& allTimbreDescriptorMetadata();

} // namespace ats

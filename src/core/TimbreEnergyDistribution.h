#pragma once

#include "SpectralEnvelope.h"
#include "TimbreDescriptors.h"
#include "TimbreFrameAdapter.h"

#include <cstddef>
#include <vector>

namespace ats
{

// One sample of an energy distribution. frequencyHz is the source frequency;
// axisPosition is the normalized position on the configured comparison axis
// (0..1). energy is the linear-energy weight, never negative.
struct EnergySample
{
    double frequencyHz = 0.0;
    double axisPosition = 0.0;
    double energy = 0.0;
};

// Output of buildPartialEnergyDistribution / buildResidualEnergyDistribution /
// buildCombinedEnergyDistribution. samples are sorted by frequency ascending.
// totalEnergy equals the sum of sample energies.
// activePointCount is the number of non-zero-energy samples. The frequency
// span and the represented energy ratio (relative to the source) are reported
// for descriptor coverage records.
struct EnergyDistribution
{
    std::vector<EnergySample> samples;
    double totalEnergy = 0.0;
    int activePointCount = 0;
    double minFrequencyHz = 0.0;
    double maxFrequencyHz = 0.0;
    double representedEnergyRatio = 1.0;
    TimbreDescriptorStatus status = TimbreDescriptorStatus::EmptyInput;
};

// Build the partial-only energy distribution from validated partial samples.
// The axis is used only to compute axisPosition for each sample; clamping is
// honored when isValidFrequencyAxis(axis) is true.
EnergyDistribution buildPartialEnergyDistribution(
    const std::vector<PartialEnergySample>& partials,
    const EnvelopeAxisSpec& axis);

// Build the residual-only energy distribution. Sub-floor bands with zero
// energy are included as zero-energy samples so coverage records remain
// accurate.
EnergyDistribution buildResidualEnergyDistribution(
    const std::vector<ResidualEnergySample>& bands,
    const EnvelopeAxisSpec& axis);

// Build the combined (partials + residual bands) distribution. The
// totalEnergy is the canonical sum used by combined-domain descriptors.
EnergyDistribution buildCombinedEnergyDistribution(
    const std::vector<PartialEnergySample>& partials,
    const std::vector<ResidualEnergySample>& bands,
    const EnvelopeAxisSpec& axis);

// Result of projecting an EnergyDistribution onto a uniform comparison grid.
// bins[i] holds the projected energy in cell i; binCenterHz[i] holds the
// cell's center frequency in Hz. The projection conserves total energy within
// tolerance; representedEnergyRatio reports the fraction of the source energy
// that fell inside the axis range (excluded energy is reported separately).
struct ProjectedGrid
{
    std::vector<double> bins;
    std::vector<double> binCenterHz;
    double totalEnergyBefore = 0.0;
    double totalEnergyAfter = 0.0;
    double excludedEnergy = 0.0;
    double representedEnergyRatio = 1.0;
    int activeBinCount = 0;
    TimbreDescriptorStatus status = TimbreDescriptorStatus::EmptyInput;
};

// Project an EnergyDistribution onto a uniform grid of binCount bins covering
// the configured axis range. Each sample's energy is split between its
// neighboring bins using linear interpolation in the axis coordinate so that
// the projection is invariant to sample ordering and conserves total energy
// within tolerance for in-range samples.
ProjectedGrid projectEnergyOntoUniformGrid(
    const EnergyDistribution& distribution,
    const EnvelopeAxisSpec& axis,
    int binCount);

// Sample a FrameEnvelope onto a uniform grid in axis coordinate. value units
// are linear amplitudes (output of estimator->evaluate). Returns empty values
// when the envelope storage is unavailable. amplitudeFloor protects against
// non-positive envelope values returned by estimators; values below the floor
// are clamped to the floor.
struct EnvelopeGridSamples
{
    std::vector<double> values;        // linear amplitude
    std::vector<double> energy;        // value^2
    std::vector<double> binCenterHz;
    double minBinHz = 0.0;
    double maxBinHz = 0.0;
    int activeBinCount = 0;
    TimbreDescriptorStatus status = TimbreDescriptorStatus::EmptyInput;
};

// Forward-declared estimator type from SpectralEnvelopeEstimator.h is used at
// call sites; this helper takes a callable so the descriptor core does not
// depend on the estimator registry.
using EnvelopeSamplerFn = double (*)(const FrameEnvelope&, double, bool*);

// Sample the envelope onto a uniform grid. The sampler may be any callable
// matching evaluate(envelope, frequencyHz, &ok) and is expected to return a
// linear amplitude. The default of nullptr falls back to a no-op grid (only
// useful when the caller plans to populate amplitudes itself).
EnvelopeGridSamples sampleFittedEnvelopeOntoGrid(
    const FrameEnvelope& envelope,
    const EnvelopeAxisSpec& axis,
    int binCount,
    double amplitudeFloor,
    EnvelopeSamplerFn sampler);

// Convert a linear amplitude value to its energy weight using the canonical
// energy model (energy = amplitude^2). The helper is exposed so caller code
// reads as canonical rather than open-coded multiplication.
constexpr double amplitudeToEnergy(double amplitude)
{
    return amplitude * amplitude;
}

} // namespace ats

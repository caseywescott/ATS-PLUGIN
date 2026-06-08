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

} // namespace ats

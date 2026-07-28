#include "../../src/dsp/FrequencyShapeProcessor.h"
#include "../../src/dsp/MathExpressionService.h"
#include "../../src/dsp/MorphCorrespondenceBuilder.h"
#include "../../src/dsp/MorphEvaluator.h"
#include "../../src/dsp/MorphGridBuilder.h"
#include "../../src/dsp/MorphPhaseService.h"
#include "../../src/dsp/MorphRecipeService.h"
#include "../../src/dsp/MorphResidualService.h"
#include "../../src/dsp/MorphTransientService.h"
#include "../../src/dsp/Resampler.h"
#include "../../src/dsp/SpectralEnvelopeService.h"
#include "../../src/dsp/TimbreMotionService.h"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

namespace
{
bool isFiniteValue(double value) { return std::isfinite(value); }

ats::MorphBandAssignment assignment(int partial, int band, double frequency, double amplitude)
{
    ats::MorphBandAssignment value;
    value.partialIndex = partial;
    value.frameIndex = 0;
    value.frequencyHz = frequency;
    value.amplitude = amplitude;
    value.bandIndex = band;
    value.kind = ats::MorphBandAssignmentKind::Assigned;
    return value;
}
} // namespace

int main()
{
    // Resampling preserves the endpoint and produces deterministic finite output.
    const float ramp[] { 0.0f, 1.0f, 2.0f, 3.0f };
    std::vector<float> resampled;
    ats::dsp::Resampler::renderMono(ramp, 4, 4.0, resampled, 8.0);
    assert(resampled.size() == 8);
    assert(resampled.front() == 0.0f && resampled.back() == 3.0f);
    for (float value : resampled) assert(std::isfinite(value));

    // Frequency shaping changes only the partial grid and remains Nyquist-safe.
    double frequencies[] { 200.0, 400.0 };
    double amplitudes[] { 1.0, 0.5 };
    double* frequencyRows[] { frequencies };
    double* amplitudeRows[] { amplitudes };
    ATS_SOUND sound {};
    sound.srate = 48000;
    sound.partials = 1;
    sound.frames = 2;
    sound.frq = frequencyRows;
    sound.amp = amplitudeRows;
    ats::FrequencyShapeSettings shape;
    shape.mode = ats::FrequencyShapeMode::Stretch;
    shape.amount = 2.0;
    shape.pivotHz = 200.0;
    ats::dsp::FrequencyShapeProcessor::applyToSound(&sound, shape);
    assert(frequencies[0] == 200.0 && frequencies[1] == 800.0);
    assert(ats::dsp::FrequencyShapeProcessor::pivot(&sound, shape) == 200.0);

    // Timbre plans round-trip through the neutral service and evaluate finite values.
    auto timbrePlan = ats::dsp::TimbreMotionService::deserialize("");
    ats::TimbreTargetCurve brightness;
    brightness.descriptorId = "combined.centroid_axis";
    brightness.mode = ats::TimbreTargetMode::InterpolateAB;
    brightness.smoothingMs = 0.0;
    brightness.points = { { 0.0, 0.0, 0 }, { 1.0, 1.0, 0 } };
    timbrePlan.targetCurves.push_back(brightness);
    const auto timbreText = ats::dsp::TimbreMotionService::serialize(timbrePlan);
    const auto timbreRoundTrip = ats::dsp::TimbreMotionService::deserialize(timbreText);
    assert(timbreRoundTrip.targetCurves.size() == 1);
    auto compiledTimbre = ats::dsp::TimbreMotionService::compile(timbreRoundTrip, 1000.0);
    const auto timbre = compiledTimbre.evaluate(0.5, 0.0, 0.0, 0.5);
    assert(isFiniteValue(timbre.brightnessDb) && isFiniteValue(timbre.tiltDbPerOct)
           && isFiniteValue(timbre.noiseBalance));

    // Morph grids include their endpoints and reject out-of-range bands safely.
    const auto bark = ats::dsp::MorphGridBuilder::bark(4, 100.0, 8000.0);
    const auto mel = ats::dsp::MorphGridBuilder::mel(4, 100.0, 8000.0);
    assert(bark.edgeFrequenciesHz.size() == 5 && mel.edgeFrequenciesHz.size() == 5);
    assert(bark.edgeFrequenciesHz.front() == 100.0 && bark.edgeFrequenciesHz.back() == 8000.0);
    assert(ats::dsp::MorphGridBuilder::band(bark, 440.0) >= 0);
    assert(ats::dsp::MorphGridBuilder::band(bark, 10.0) == -1);

    // Recipe serialization, phase, residual, transient, envelope, and evaluator contracts.
    auto recipe = ats::dsp::MorphRecipeService::defaults();
    recipe.frequencyGrid = bark;
    recipe.defaultMorphAmount = 0.5;
    const auto recipeText = ats::dsp::MorphRecipeService::serialize(recipe);
    ats::MorphRecipe decoded;
    std::string error;
    assert(ats::dsp::MorphRecipeService::deserialize(recipeText, decoded, &error));
    assert(decoded.frequencyGrid == recipe.frequencyGrid);
    const auto snapshotA = ats::dsp::MorphEvaluator::evaluate(decoded, { 0.5 });
    const auto snapshotB = ats::dsp::MorphEvaluator::evaluate(decoded, { 0.5 });
    assert(snapshotA == snapshotB && snapshotA.safeForAudioThreadPlayback);

    ats::MorphPhaseInput phase;
    phase.hasSourcePhase = phase.hasTargetPhase = true;
    phase.sourcePhaseRadians = 0.25;
    phase.targetPhaseRadians = 1.25;
    ats::MorphPhaseSettings phaseSettings;
    phaseSettings.strategy = ats::MorphPhaseStrategy::DirectBlend;
    const auto phaseResult = ats::dsp::MorphPhaseService::resolve(phase, 0.5, phaseSettings);
    assert(phaseResult.valid && phaseResult.interpolatedPhase);

    const auto residual = ats::dsp::MorphResidualService::evaluate(
        { { 0, 2.0, 1.0 } }, { { 0, 4.0, 3.0 } }, { 0.5 });
    assert(residual.bands.size() == 1 && residual.bands[0].outputTotalNoisePower > 0.0);

    ats::MorphTransientSettings transientSettings;
    transientSettings.maxOnsetAlignmentOffsetSeconds = 0.01;
    const auto transient = ats::dsp::MorphTransientService::analyze(
        { { 0.10, 1.0, 1 } }, { { 0.14, 1.0, 1 } }, transientSettings);
    assert(transient.comparedMarkerCount == 1 && transient.warningCount == 1);

    const auto envelope = ats::dsp::SpectralEnvelopeService::transfer(0.5, 1.0, 2.0, 0.5);
    assert(envelope.ok && isFiniteValue(envelope.outputAmplitude) && envelope.outputAmplitude > 0.5);

    const auto source = std::vector<ats::MorphBandAssignment> {
        assignment(0, 0, 440.0, 1.0), assignment(1, 0, 880.0, 0.5) };
    const auto target = std::vector<ats::MorphBandAssignment> {
        assignment(4, 0, 445.0, 0.9), assignment(5, 0, 900.0, 0.4) };
    assert(ats::dsp::MorphCorrespondenceBuilder::rank(source, target).size() == 2);
    assert(ats::dsp::MorphCorrespondenceBuilder::greedy(source, target).size() == 2);
    ats::MorphAssignmentMatrixOptions matrixOptions;
    matrixOptions.advancedStrategyEnabled = true;
    const auto matrix = ats::dsp::MorphCorrespondenceBuilder::assignment(source, target, matrixOptions);
    assert(matrix.records.size() == 2 && matrix.evaluatedAssignmentCount > 0);
    const auto transport = ats::dsp::MorphCorrespondenceBuilder::transport(source, target);
    assert(transport.sourceMass > 0.0 && transport.targetMass > 0.0);

    error.clear();
    assert(!ats::dsp::MathExpressionService::apply(nullptr, "a=1", error));
    assert(!error.empty());
    return 0;
}

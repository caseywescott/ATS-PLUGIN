#include "../src/dsp/MorphTrajectoryRenderer.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
struct SoundFixture
{
    std::vector<double> frequencies;
    std::vector<double> amplitudes;
    std::vector<double> phases;
    double* frequencyRows[1] {};
    double* amplitudeRows[1] {};
    double* phaseRows[1] {};
    ATS_SOUND sound {};

    SoundFixture(double amplitude, double phase)
        : frequencies(8, 20.0), amplitudes(8, amplitude), phases(8, phase)
    {
        frequencyRows[0] = frequencies.data();
        amplitudeRows[0] = amplitudes.data();
        phaseRows[0] = phases.data();
        sound.srate = 1000;
        sound.frame_size = 10;
        sound.frames = 8;
        sound.partials = 1;
        sound.frq = frequencyRows;
        sound.amp = amplitudeRows;
        sound.pha = phaseRows;
    }
};

ats::MorphRecipe recipeFor(const ats::MorphPhaseStrategy phase,
                           const ats::MorphTransientPolicy transient)
{
    ats::MorphRecipe recipe;
    recipe.sourceA.frameHopSeconds = 0.01;
    recipe.sourceB.frameHopSeconds = 0.01;
    recipe.phase.strategy = phase;
    recipe.transient.policy = transient;
    recipe.transient.protectionWindowSeconds = 0.04;
    recipe.sourceATransients.push_back({ 0.015, 1.0, 1 });
    recipe.sourceBTransients.push_back({ 0.015, 1.0, 1 });
    ats::MorphCorrespondenceRecord record;
    record.kind = ats::MorphCorrespondenceKind::Matched;
    record.sourcePartialIndex = 0;
    record.targetPartialIndex = 0;
    record.bandIndex = 1;
    recipe.correspondenceRecords.push_back(record);
    return recipe;
}

ats::dsp::MorphTrajectorySettings renderSettings()
{
    ats::dsp::MorphTrajectorySettings settings;
    settings.sampleRate = 1000.0;
    settings.durationSeconds = 0.07;
    settings.fixedMorphAmount = 0.5;
    settings.performanceMacro = 0.5;
    return settings;
}

double energy(const std::vector<float>& samples, int begin, int end)
{
    double result = 0.0;
    for (int i = begin; i < end; ++i)
        result += (double) samples[(size_t) i] * (double) samples[(size_t) i];
    return result;
}
} // namespace

int main()
{
    SoundFixture phaseA(1.0, 0.0);
    SoundFixture phaseB(1.0, 0.5 * 3.14159265358979323846);
    auto sourceRecipe = recipeFor(ats::MorphPhaseStrategy::SourceADonor,
                                  ats::MorphTransientPolicy::Crossfade);
    auto targetRecipe = sourceRecipe;
    targetRecipe.phase.strategy = ats::MorphPhaseStrategy::SourceBDonor;
    const auto sourcePhase = ats::dsp::renderMorphTrajectory(
        &phaseA.sound, &phaseB.sound, sourceRecipe, renderSettings());
    const auto targetPhase = ats::dsp::renderMorphTrajectory(
        &phaseA.sound, &phaseB.sound, targetRecipe, renderSettings());
    double phaseDifference = 0.0;
    for (size_t i = 0; i < sourcePhase.partialSamples.size(); ++i)
        phaseDifference += std::abs((double) sourcePhase.partialSamples[i]
                                    - (double) targetPhase.partialSamples[i]);
    if (!(phaseDifference > 1.0))
    {
        std::cerr << "phase strategy did not affect rendered oscillator phase\n";
        return 1;
    }

    SoundFixture loudA(1.0, 0.0);
    SoundFixture silentB(0.0, 0.0);
    auto recipeA = recipeFor(ats::MorphPhaseStrategy::OscillatorContinuation,
                             ats::MorphTransientPolicy::SourceA);
    auto recipeB = recipeA;
    recipeB.transient.policy = ats::MorphTransientPolicy::SourceB;
    auto recipeCrossfade = recipeA;
    recipeCrossfade.transient.policy = ats::MorphTransientPolicy::Crossfade;
    const auto protectedA = ats::dsp::renderMorphTrajectory(
        &loudA.sound, &silentB.sound, recipeA, renderSettings());
    const auto protectedB = ats::dsp::renderMorphTrajectory(
        &loudA.sound, &silentB.sound, recipeB, renderSettings());
    const auto crossfade = ats::dsp::renderMorphTrajectory(
        &loudA.sound, &silentB.sound, recipeCrossfade, renderSettings());
    const double energyA = energy(protectedA.partialSamples, 20, 50);
    const double energyB = energy(protectedB.partialSamples, 20, 50);
    const double energyCrossfade = energy(crossfade.partialSamples, 20, 50);
    if (!(energyA > energyCrossfade && energyCrossfade > energyB))
    {
        std::cerr << "transient policy did not order protected attack energy: "
                  << energyA << ", " << energyCrossfade << ", " << energyB << '\n';
        return 1;
    }

    std::cout << "ok: trajectory renderer applies phase strategy and transient policy\n";
    return 0;
}

#pragma once

#include "LegacyStateCodec.h"
#include "../core/ParameterRegistry.h"

#include <array>
#include <string_view>

namespace ats
{

// Projects the parameter-bearing fields of a decoded legacy state onto a
// parameter store. The VST3 controller and the processor-side host adapter both
// restore from the same ATSP chunk but own separate parameter tables, so this
// projection has to be shared rather than reimplemented on each side.
inline void applyLegacyStateToParameterStore(const LegacyState& ls, ParameterStore& parameters)
{
    const auto set = [&parameters](std::string_view id, float value) {
        (void) parameters.write(id, value);
    };

    // SynthParams
    set("synStart",         static_cast<float>(ls.synth.startTime));
    set("synDur",           static_cast<float>(ls.synth.duration));
    set("useAllPartials",   ls.synth.useAllPartials   ? 1.0f : 0.0f);
    set("usePhase",         ls.synth.usePartialsPhase ? 1.0f : 0.0f);
    set("partGain",         static_cast<float>(ls.synth.partialsGain));
    set("resGain",          static_cast<float>(ls.synth.residualGain));
    set("freq",             static_cast<float>(ls.synth.frequencyFactor));
    set("timeStretch",      static_cast<float>(ls.synth.timeFactor));
    set("useIfftSynth",     ls.synth.useIfftSynth ? 1.0f : 0.0f);
    // The chunk carries the shape once, as LegacyState::frequencyShape (v13+);
    // synth.frequencyShape is never serialized, so reading it reset the shape
    // on every project reload.
    set("freqShapeMode",    static_cast<float>(static_cast<int>(ls.frequencyShape.mode)));
    set("freqShapeAmount",  static_cast<float>(ls.frequencyShape.amount));
    set("freqShapePivotHz", static_cast<float>(ls.frequencyShape.pivotHz));
    // v19; older chunks keep the registry defaults held by LegacyState.
    set("timbreBrightnessDb", static_cast<float>(ls.timbreBrightnessDb));
    set("timbreTiltDbPerOct", static_cast<float>(ls.timbreTiltDbPerOct));
    set("timbreNoiseBalance", static_cast<float>(ls.timbreNoiseBalance));
    set("outputTrim",       static_cast<float>(ls.outputTrim));
    set("residualOnly",     ls.residualOnly ? 1.0f : 0.0f);
    set("classicPreviewMode", static_cast<float>(ls.classicPreviewMode));

    // AnalysisParams: ATSA type 1-4 back to choice ordinal 0-3.
    set("analysisType",                 static_cast<float>(std::max(0, ls.analysis.type - 1)));
    set("analysisStart",                static_cast<float>(ls.analysis.start));
    set("analysisDuration",             static_cast<float>(ls.analysis.duration));
    set("analysisLowestFreq",           static_cast<float>(ls.analysis.lowest_freq));
    set("analysisHighestFreq",          static_cast<float>(ls.analysis.highest_freq));
    set("analysisFreqDev",              static_cast<float>(ls.analysis.freq_dev));
    set("analysisWinCycles",            static_cast<float>(ls.analysis.win_cycles));
    set("analysisWinType",              static_cast<float>(ls.analysis.win_type));
    set("analysisHopSize",              static_cast<float>(ls.analysis.hop_size));
    set("analysisLowestMag",            static_cast<float>(ls.analysis.lowest_mag));
    set("analysisTrackLen",             static_cast<float>(ls.analysis.track_len));
    set("analysisMinSegLen",            static_cast<float>(ls.analysis.min_seg_len));
    set("analysisMinGapLen",            static_cast<float>(ls.analysis.min_gap_len));
    set("analysisSmrThreshold",         static_cast<float>(ls.analysis.SMR_thres));
    set("analysisMinSegSmr",            static_cast<float>(ls.analysis.min_seg_SMR));
    set("analysisLastPeakContribution", static_cast<float>(ls.analysis.last_peak_cont));
    set("analysisSmrContribution",      static_cast<float>(ls.analysis.SMR_cont));
    set("analysisMultiRes",             ls.analysis.multiRes ? 1.0f : 0.0f);
    // v22; older chunks keep the registry defaults held by LegacyState.
    set("analysisReverseTracking",      ls.analysis.reverseTracking ? 1.0f : 0.0f);
    set("analysisClampResidual",        ls.analysis.clampResidual ? 1.0f : 0.0f);
    set("residualDensity",              static_cast<float>(ls.synth.residualDensity));
    set("animationShimmerDb",           static_cast<float>(ls.synth.animationShimmerDb));
    set("animationJitterCents",         static_cast<float>(ls.synth.animationJitterCents));
    set("animationRateHz",              static_cast<float>(ls.synth.animationRateHz));
    set("vibratoDepth",                 static_cast<float>(ls.synth.vibratoDepth));
    set("vibratoRate",                  static_cast<float>(ls.synth.vibratoRateScale));
    set("transposeCharacter",           ls.synth.transposeCharacter ? 1.0f : 0.0f);
    set("transposeTiltDbPerOct",        static_cast<float>(ls.synth.transposeTiltDbPerOct));
    set("morphOnsetAlignedWarp",        ls.morphOnsetAlignedWarp ? 1.0f : 0.0f);

    // Morph
    set("morphAmount",                        static_cast<float>(ls.morphAmount));
    set("morphPerformanceMacro",              static_cast<float>(ls.morphPerformanceMacro));
    set("morphSmoothingMs",                   static_cast<float>(ls.morphSmoothingMs));
    set("morphGridType",                      static_cast<float>(ls.morphGridType));
    set("morphCorrespondenceStrategy",        static_cast<float>(ls.morphCorrespondenceStrategy));
    set("morphQuality",                       static_cast<float>(ls.morphQuality));
    set("morphUnmatchedSourcePolicy",         static_cast<float>(ls.morphUnmatchedSourcePolicy));
    set("morphUnmatchedTargetPolicy",         static_cast<float>(ls.morphUnmatchedTargetPolicy));
    set("morphDiscardOptIn",                  ls.morphDiscardOptIn ? 1.0f : 0.0f);
    set("morphFrequencyInterpolationDomain",  static_cast<float>(ls.morphFrequencyDomain));
    set("morphAmplitudeInterpolationDomain",  static_cast<float>(ls.morphAmplitudeDomain));
    set("morphPhaseStrategy",                 static_cast<float>(ls.morphPhaseStrategy));
    set("morphTransientPolicy",               static_cast<float>(ls.morphTransientPolicy));

    constexpr std::array<std::string_view, 6> kLaneAmounts {
        "morphLaneFrequencyAmount", "morphLaneAmplitudeAmount", "morphLaneResidualAmount",
        "morphLaneTimeAmount",      "morphLaneTransientAmount", "morphLanePhaseAmount"
    };
    constexpr std::array<std::string_view, 6> kLaneLinks {
        "morphLaneFrequencyLinked", "morphLaneAmplitudeLinked", "morphLaneResidualLinked",
        "morphLaneTimeLinked",      "morphLaneTransientLinked", "morphLanePhaseLinked"
    };
    for (std::size_t n = 0; n < ls.morphLaneAmounts.size(); ++n)
    {
        set(kLaneAmounts[n], static_cast<float>(ls.morphLaneAmounts[n]));
        set(kLaneLinks[n],   ls.morphLaneLinked[n] ? 1.0f : 0.0f);
    }

    // Smearnov
    set("smearnovBlurMs",             static_cast<float>(ls.smearnov.blurMs));
    set("smearnovSharpenAmount",      static_cast<float>(ls.smearnov.sharpen));
    set("smearnovExaggerateDb",       static_cast<float>(ls.smearnov.exaggerateDb));
    set("smearnovMix",                static_cast<float>(ls.smearnov.mix));
    set("smearnovMaxDb",              static_cast<float>(ls.smearnov.maxDb));
    set("smearnovTargets",            static_cast<float>(ls.smearnov.targets));
    set("smearnovScope",              static_cast<float>(ls.smearnov.scope));
    set("smearnovWeightDetFlux",      static_cast<float>(ls.smearnov.weightDetFlux));
    set("smearnovWeightResFlux",      static_cast<float>(ls.smearnov.weightResFlux));
    set("smearnovWeightHfcFlux",      static_cast<float>(ls.smearnov.weightHfcFlux));
    set("smearnovWeightBirth",        static_cast<float>(ls.smearnov.weightBirth));
    set("smearnovWeightPhaseDev",     static_cast<float>(ls.smearnov.weightPhaseDev));
    set("smearnovThresholdLambda",    static_cast<float>(ls.smearnov.thresholdLambda));
    set("smearnovThresholdAutoScale", static_cast<float>(ls.smearnov.thresholdAutoScale));
    set("smearnovPeakWindowFrames",   static_cast<float>(ls.smearnov.peakWindowFrames));
    set("smearnovMinGapFrames",       static_cast<float>(ls.smearnov.minGapFrames));
    set("smearnovPreMarginMs",        static_cast<float>(ls.smearnov.preMarginMs));
    set("smearnovMaxAttackFrames",    static_cast<float>(ls.smearnov.maxAttackFrames));
    set("smearnovUsePhaseDeviation",  ls.smearnov.usePhaseDeviation ? 1.0f : 0.0f);
    set("smearnovUseRpcaFallback",    ls.smearnov.useRpcaFallback   ? 1.0f : 0.0f);
    set("smearnovWeightTransientness", static_cast<float>(ls.smearnov.weightTransientness)); // v22
}

} // namespace ats

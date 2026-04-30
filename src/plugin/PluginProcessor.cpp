#include "PluginProcessor.h"

#include "PresetLibrary.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <utility>

#include "AtsAnalysisPipeline.h"
#include "AtsMathExpr.h"
#include "AtsModernMorph.h"
#include "AtsModernSynth.h"
#include "PluginEditor.h"

#include "ResampleLinear.h"

#include <JuceHeader.h>

extern "C" {
#include "atsa.h"
}

namespace
{
constexpr int kStateVersion = 6;
constexpr int kMinReadableStateVersion = 2;

constexpr int kAnalysisModeClassic = 0;
constexpr int kAnalysisModeModern = 1;
constexpr int kAnalysisModeExperimental = 2;
constexpr int kTrackerModeClassic = 0;
constexpr int kTrackerModeAssignment = 1;
constexpr int kPeakEstimatorClassic = 0;
constexpr int kPeakEstimatorExperimental = 1;
constexpr int kMorphSourceCurrent = 0;
constexpr int kMorphSourcePrevious = 1;
constexpr int kMorphSourceDisabled = 2;

juce::AudioBuffer<float> runDoSynthesis(const ATS_SOUND* sound, const ats::SynthParams& sp)
{
    juce::AudioBuffer<float> out;

    if (sound == nullptr)
        return out;

    SPARAMS s {};
    sp.toSparams(s, sound->dur);

    CURVE timenv {};
    curve_init(&timenv);

    double* rawOut = nullptr;
    int nOut = 0;
    do_synthesis(const_cast<ATS_SOUND*>(sound), &s, &timenv, nullptr, &rawOut, &nOut);
    curve_free(&timenv);

    if (rawOut == nullptr || nOut <= 0)
        return out;

    out.setSize(1, nOut);
    for (int i = 0; i < nOut; ++i)
        out.setSample(0, i, static_cast<float>(rawOut[i]));

    std::free(rawOut);
    return out;
}

void writeAnalysis(juce::MemoryOutputStream& mos, const ats::AnalysisParams& a)
{
    mos.writeDouble(a.start);
    mos.writeDouble(a.duration);
    mos.writeDouble(a.lowest_freq);
    mos.writeDouble(a.highest_freq);
    mos.writeDouble(a.freq_dev);
    mos.writeInt(a.win_cycles);
    mos.writeInt(a.win_type);
    mos.writeInt(a.win_size);
    mos.writeDouble(a.hop_size);
    mos.writeDouble(a.lowest_mag);
    mos.writeInt(a.track_len);
    mos.writeInt(a.min_seg_len);
    mos.writeInt(a.min_gap_len);
    mos.writeDouble(a.SMR_thres);
    mos.writeDouble(a.min_seg_SMR);
    mos.writeDouble(a.last_peak_cont);
    mos.writeDouble(a.SMR_cont);
    mos.writeInt(a.type);
}

bool readAnalysis(juce::MemoryInputStream& mis, ats::AnalysisParams& a)
{
    if (mis.getNumBytesRemaining() < 80)
        return false;
    a.start = mis.readDouble();
    a.duration = mis.readDouble();
    a.lowest_freq = mis.readDouble();
    a.highest_freq = mis.readDouble();
    a.freq_dev = mis.readDouble();
    a.win_cycles = mis.readInt();
    a.win_type = mis.readInt();
    a.win_size = mis.readInt();
    a.hop_size = mis.readDouble();
    a.lowest_mag = mis.readDouble();
    a.track_len = mis.readInt();
    a.min_seg_len = mis.readInt();
    a.min_gap_len = mis.readInt();
    a.SMR_thres = mis.readDouble();
    a.min_seg_SMR = mis.readDouble();
    a.last_peak_cont = mis.readDouble();
    a.SMR_cont = mis.readDouble();
    a.type = mis.readInt();
    return true;
}

void writeSynth(juce::MemoryOutputStream& mos, const ats::SynthParams& s)
{
    mos.writeDouble(s.startTime);
    mos.writeDouble(s.duration);
    mos.writeBool(s.useAllPartials);
    mos.writeBool(s.usePartialsPhase);
    mos.writeDouble(s.partialsGain);
    mos.writeDouble(s.residualGain);
    mos.writeDouble(s.frequencyFactor);
    mos.writeDouble(s.timeFactor);
    mos.writeDouble(s.sampleRate);
}

bool readSynth(juce::MemoryInputStream& mis, ats::SynthParams& s)
{
    if (mis.getNumBytesRemaining() < 48)
        return false;
    s.startTime = mis.readDouble();
    s.duration = mis.readDouble();
    s.useAllPartials = mis.readBool();
    s.usePartialsPhase = mis.readBool();
    s.partialsGain = mis.readDouble();
    s.residualGain = mis.readDouble();
    s.frequencyFactor = mis.readDouble();
    s.timeFactor = mis.readDouble();
    s.sampleRate = mis.readDouble();
    return true;
}

/** True when cached partial/residual layers (host rate) can be reused for this core. */
bool samePreviewLayerCore(const ats::SynthParams& a, const ats::SynthParams& b)
{
    return a.usePartialsPhase == b.usePartialsPhase
           && a.useAllPartials == b.useAllPartials
           && (std::abs(a.sampleRate - b.sampleRate) < 1e-3)
           && (std::abs(a.frequencyFactor - b.frequencyFactor) < 1e-8)
           && (std::abs(a.timeFactor - b.timeFactor) < 1e-8)
           && (std::abs(a.startTime - b.startTime) < 1e-8)
           && (std::abs(a.duration - b.duration) < 1e-8);
}

const char* analysisModeIdForIndex(int idx)
{
    switch (idx)
    {
    case kAnalysisModeModern: return "modern_ats";
    case kAnalysisModeExperimental: return "experimental";
    default: return "classic_ats";
    }
}

int peakEstimatorIndexForAnalysisMode(int analysisModeIndex)
{
    return analysisModeIndex == kAnalysisModeExperimental ? kPeakEstimatorExperimental
                                                          : kPeakEstimatorClassic;
}

const char* analysisModeDisplayNameForIndex(int idx)
{
    switch (idx)
    {
    case kAnalysisModeModern: return "Modern ATS";
    case kAnalysisModeExperimental: return "Experimental reassigned/DDM";
    default: return "Classic ATS";
    }
}

const char* trackerModeIdForIndex(int idx)
{
    return idx == kTrackerModeAssignment ? "modern_assignment" : "classic_recursive_frequency_smr";
}

const char* peakEstimatorIdForIndex(int idx)
{
    return idx == kPeakEstimatorExperimental ? "experimental_reassigned_peak_estimator"
                                             : "classic_stft_peak_estimator";
}

double atsCell(double** table, int row, int column)
{
    if (table == nullptr || row < 0 || column < 0 || table[row] == nullptr)
        return 0.0;
    const double value = table[row][column];
    return std::isfinite(value) ? value : 0.0;
}

bool atsPartialActive(const ATS_SOUND& sound, int partialIndex, int frameIndex)
{
    return atsCell(sound.amp, partialIndex, frameIndex) > 0.0
           && atsCell(sound.frq, partialIndex, frameIndex) > 0.0;
}

double atsFrameTime(const ATS_SOUND& sound, int frameIndex)
{
    const double stored = atsCell(sound.time, 0, frameIndex);
    if (stored > 0.0 || frameIndex == 0)
        return stored;
    if (sound.srate > 0 && sound.frame_size > 0)
        return (double) frameIndex * (double) sound.frame_size / (double) sound.srate;
    return (double) frameIndex;
}

std::vector<double> residualBandsForFrame(const ATS_SOUND& sound, int frameIndex)
{
    std::vector<double> bands;
    if (sound.band_energy == nullptr)
        return bands;

    bands.assign(ats::kModernAtsDefaultResidualBands, 0.0);
    for (int band = 0; band < ats::kModernAtsDefaultResidualBands; ++band)
        bands[(std::size_t) band] = std::max(0.0, atsCell(sound.band_energy, band, frameIndex));
    return bands;
}

double frameEnergy(const ATS_SOUND& sound, int frameIndex)
{
    double energy = 0.0;
    for (int partial = 0; partial < sound.partials; ++partial)
    {
        const double amp = atsCell(sound.amp, partial, frameIndex);
        energy += std::max(0.0, amp * amp);
    }
    if (sound.band_energy != nullptr)
    {
        for (int band = 0; band < ats::kModernAtsDefaultResidualBands; ++band)
            energy += std::max(0.0, atsCell(sound.band_energy, band, frameIndex));
    }
    return energy;
}

int activePartialCount(const ATS_SOUND& sound, int frameIndex)
{
    int active = 0;
    for (int partial = 0; partial < sound.partials; ++partial)
    {
        if (atsPartialActive(sound, partial, frameIndex))
            ++active;
    }
    return active;
}

std::vector<ats::TransientFeatureFrame> transientFeaturesFromAtsSound(const ATS_SOUND& sound)
{
    std::vector<ats::TransientFeatureFrame> features;
    features.reserve((std::size_t) std::max(0, sound.frames));

    double previousEnergy = 0.0;
    int previousActive = 0;
    for (int frame = 0; frame < sound.frames; ++frame)
    {
        const double energy = frameEnergy(sound, frame);
        const int active = activePartialCount(sound, frame);

        ats::TransientFeatureFrame feature;
        feature.timeSeconds = atsFrameTime(sound, frame);
        feature.energy = energy;
        feature.spectralFlux = frame == 0 ? 0.0 : std::max(0.0, energy - previousEnergy);
        feature.modelDiscontinuity =
            frame == 0 || sound.partials <= 0 ? 0.0
                                              : std::abs((double) active - (double) previousActive)
                                                    / (double) std::max(1, sound.partials);
        features.push_back(feature);

        previousEnergy = energy;
        previousActive = active;
    }

    return features;
}

std::vector<ats::TrackingPeakFrame> trackingPeakFramesFromAtsSound(const ATS_SOUND& sound)
{
    std::vector<ats::TrackingPeakFrame> frames((std::size_t) std::max(0, sound.frames));
    for (int frame = 0; frame < sound.frames; ++frame)
    {
        auto& peaks = frames[(std::size_t) frame];
        for (int partial = 0; partial < sound.partials; ++partial)
        {
            if (!atsPartialActive(sound, partial, frame))
                continue;

            ats::TrackingPeak peak;
            peak.sourceIndex = partial;
            peak.frequencyHz = atsCell(sound.frq, partial, frame);
            peak.amplitude = atsCell(sound.amp, partial, frame);
            peak.smrDb = atsCell(sound.smr, partial, frame);
            peak.phaseRadians = atsCell(sound.pha, partial, frame);
            peaks.push_back(peak);
        }
    }
    return frames;
}

ats::ModernAtsPartialFrame modernFrameFromAtsCell(const ATS_SOUND& sound,
                                                  int partialIndex,
                                                  int frameIndex,
                                                  bool previousActive)
{
    const bool active = atsPartialActive(sound, partialIndex, frameIndex);
    ats::ModernAtsPartialFrame frame;
    frame.amplitude = active ? std::max(0.0, atsCell(sound.amp, partialIndex, frameIndex)) : 0.0;
    frame.frequencyHz = active ? std::max(0.0, atsCell(sound.frq, partialIndex, frameIndex)) : 0.0;
    frame.phaseRadians = active ? atsCell(sound.pha, partialIndex, frameIndex) : 0.0;
    frame.smrDb = active ? atsCell(sound.smr, partialIndex, frameIndex) : 0.0;
    frame.noiseEnergy = active ? std::max(0.0, atsCell(sound.res, partialIndex, frameIndex)) : 0.0;
    frame.confidence = active ? 1.0 : 0.0;
    frame.reason = !active ? ats::ModernAtsFrameReason::Slept
                           : (previousActive ? ats::ModernAtsFrameReason::Matched
                                             : ats::ModernAtsFrameReason::Born);
    return frame;
}

std::vector<ats::ModernAtsPartial> classicPartialsAsModern(const ATS_SOUND& sound)
{
    std::vector<ats::ModernAtsPartial> partials;
    partials.reserve((std::size_t) std::max(0, sound.partials));
    for (int partialIndex = 0; partialIndex < sound.partials; ++partialIndex)
    {
        ats::ModernAtsPartial partial;
        partial.id = partialIndex;
        partial.label = "classic_" + std::to_string(partialIndex);
        partial.frames.reserve((std::size_t) std::max(0, sound.frames));

        bool previousActive = false;
        for (int frameIndex = 0; frameIndex < sound.frames; ++frameIndex)
        {
            partial.frames.push_back(modernFrameFromAtsCell(sound, partialIndex, frameIndex, previousActive));
            previousActive = atsPartialActive(sound, partialIndex, frameIndex);
        }

        partials.push_back(std::move(partial));
    }
    return partials;
}

void populateModernFramesFromPartials(ats::ModernAtsModel& model, const ATS_SOUND& sound)
{
    model.frames.clear();
    model.frames.reserve((std::size_t) std::max(0, sound.frames));
    for (int frameIndex = 0; frameIndex < sound.frames; ++frameIndex)
    {
        ats::ModernAtsFrame frame;
        frame.timeSeconds = atsFrameTime(sound, frameIndex);
        frame.orphanResidualBandEnergy = residualBandsForFrame(sound, frameIndex);
        for (const auto& partial : model.partials)
        {
            if (frameIndex >= (int) partial.frames.size())
                continue;
            const auto& pf = partial.frames[(std::size_t) frameIndex];
            if (pf.amplitude > 0.0 && pf.frequencyHz > 0.0
                && pf.reason != ats::ModernAtsFrameReason::Slept
                && pf.reason != ats::ModernAtsFrameReason::Pruned
                && pf.reason != ats::ModernAtsFrameReason::Rejected)
            {
                frame.activePartialIds.push_back(partial.id);
            }
        }
        model.frames.push_back(std::move(frame));
    }
}

ats::ModernAtsModel buildModernModelFromClassicAtsSound(
    const ATS_SOUND& sound,
    const ats::AnalysisParams& analysisParams,
    int analysisModeIndex,
    int trackerModeIndex,
    int peakEstimatorIndex,
    int inputChannelCount,
    const std::vector<ats::ModernAtsTrackLifecycleEvent>& classicLifecycle)
{
    ats::ModernAtsModel model;
    model.header.analysisMode = analysisModeIndex == kAnalysisModeExperimental
                                    ? ats::ModernAtsAnalysisMode::Experimental
                                    : (analysisModeIndex == kAnalysisModeModern
                                           ? ats::ModernAtsAnalysisMode::ModernAts
                                           : ats::ModernAtsAnalysisMode::ClassicAts);
    model.header.trackerMode = trackerModeIndex == kTrackerModeAssignment
                                   ? ats::ModernAtsTrackerMode::ModernAssignment
                                   : ats::ModernAtsTrackerMode::ClassicRecursiveFrequencySmr;
    model.header.sampleRate = sound.srate;
    model.header.hopSizeSamples = std::max(1, sound.frame_size);
    model.header.windowSizeSamples = std::max(1, sound.window_size);
    model.header.inputChannelCount = std::max(1, inputChannelCount);
    model.header.channelPolicy =
        ats::defaultModernAtsChannelPolicyForInput(model.header.inputChannelCount);
    model.header.parameterHash = "plugin:type=" + std::to_string(analysisParams.type)
                                 + ";tracker=" + trackerModeIdForIndex(trackerModeIndex)
                                 + ";peak=" + peakEstimatorIdForIndex(peakEstimatorIndex);
    model.header.sourceHash = "classic:" + std::to_string(sound.srate) + ":" + std::to_string(sound.frames)
                              + ":" + std::to_string(sound.partials);

    auto transientFeatures = transientFeaturesFromAtsSound(sound);
    ats::TransientDetectionDiagnostics transientDiagnostics;
    model.transients = ats::detectTransientMarkers(transientFeatures, ats::TransientDetectionConfig {}, transientDiagnostics);

    if (trackerModeIndex == kTrackerModeAssignment)
    {
        ats::AssignmentTrackerConfig config;
        config.analysisMode = model.header.analysisMode;
        for (const auto& marker : model.transients)
            config.transientBreakFrameIndices.push_back(marker.frameIndex);

        auto assignment = ats::runAssignmentTrackerPrototype(trackingPeakFramesFromAtsSound(sound), config);
        model.partials = std::move(assignment.partials);
        model.trackLifecycle = std::move(assignment.trackLifecycle);
        model.assignmentCosts = std::move(assignment.assignmentCosts);
    }
    else
    {
        model.partials = classicPartialsAsModern(sound);
        model.trackLifecycle = classicLifecycle;
    }

    populateModernFramesFromPartials(model, sound);

    if (analysisModeIndex == kAnalysisModeModern)
    {
        model.warnings.push_back(trackerModeIndex == kTrackerModeAssignment
                                     ? "Modern assignment tracker applied to derived ATS peak frames."
                                     : "Modern ATS mode using classic recursive frequency/SMR tracking.");
    }
    if (analysisModeIndex == kAnalysisModeExperimental)
    {
        model.warnings.push_back(
            "Experimental reassigned/DDM estimator selected; plugin snapshot is tagged experimental.");
    }

    return model;
}

ats::MorphRecipe morphRecipeFromApvts(const juce::AudioProcessorValueTreeState& apvts)
{
    const int recipeIndex =
        juce::jlimit(0, 3, juce::roundToInt(apvts.getRawParameterValue("morphRecipe")->load()));

    ats::MorphRecipe recipe;
    recipe.recipeId = "plugin_morph_" + std::to_string(recipeIndex);
    recipe.sourceModelId = "source";
    recipe.targetModelId = "target";
    recipe.envelopes = ats::makeDefaultMorphEnvelopeSet();
    recipe.envelopes.frequency = ats::makeConstantMorphEnvelope(
        "frequency", apvts.getRawParameterValue("morphFreqEnv")->load());
    recipe.envelopes.amplitude = ats::makeConstantMorphEnvelope(
        "amplitude", apvts.getRawParameterValue("morphAmpEnv")->load());
    recipe.envelopes.partialNoise = ats::makeConstantMorphEnvelope(
        "partial_noise", apvts.getRawParameterValue("morphNoiseEnv")->load());
    recipe.envelopes.orphanResidual = ats::makeConstantMorphEnvelope(
        "orphan_residual", apvts.getRawParameterValue("morphOrphanEnv")->load());
    recipe.envelopes.time = ats::makeConstantMorphEnvelope(
        "time", apvts.getRawParameterValue("morphTimeEnv")->load());

    switch (recipeIndex)
    {
    case 1:
        recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::HarmonicNumber;
        recipe.sourceFundamentalHz = 110.0;
        recipe.targetFundamentalHz = 110.0;
        recipe.sourceHarmonicity = 0.9;
        recipe.targetHarmonicity = 0.9;
        break;
    case 2:
        recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::Automatic;
        recipe.transientMode = ats::MorphTransientMode::BlendBoth;
        recipe.protectTransientWindows = true;
        break;
    case 3:
        recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::NearestFrequency;
        recipe.sourceUnmatchedPolicy = ats::MorphUnmatchedPolicy::Hold;
        recipe.targetUnmatchedPolicy = ats::MorphUnmatchedPolicy::FadeIn;
        break;
    default:
        recipe.correspondenceStrategy = ats::MorphCorrespondenceStrategy::Automatic;
        break;
    }

    return recipe;
}

const ats::ModernAtsModel* selectMorphSource(int sourceIndex,
                                             const ats::ModernAtsModel& latest,
                                             bool latestValid,
                                             const ats::ModernAtsModel& previous,
                                             bool previousValid)
{
    if (sourceIndex == kMorphSourceCurrent)
        return latestValid ? &latest : nullptr;
    if (sourceIndex == kMorphSourcePrevious)
        return previousValid ? &previous : nullptr;
    return nullptr;
}

} // namespace

AtsAudioProcessor::AtsAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    apvts.addParameterListener("freq", this);
    apvts.addParameterListener("timeStretch", this);
    apvts.addParameterListener("synStart", this);
    apvts.addParameterListener("synDur", this);
    apvts.addParameterListener("usePhase", this);
    apvts.addParameterListener("useAllPartials", this);
}

AtsAudioProcessor::~AtsAudioProcessor()
{
    cancelJobs();
    joinWorkers();
    apvts.removeParameterListener("freq", this);
    apvts.removeParameterListener("timeStretch", this);
    apvts.removeParameterListener("synStart", this);
    apvts.removeParameterListener("synDur", this);
    apvts.removeParameterListener("usePhase", this);
    apvts.removeParameterListener("useAllPartials", this);
}

int AtsAudioProcessor::getNumPrograms()
{
    return ats_preset::factoryPresetNames().size();
}

int AtsAudioProcessor::getCurrentProgram()
{
    return currentFactoryProgramIndex;
}

void AtsAudioProcessor::setCurrentProgram(int index)
{
    const int n = ats_preset::factoryPresetNames().size();
    if (n <= 0)
        return;

    const int i = juce::jlimit(0, n - 1, index);
    currentFactoryProgramIndex = i;
    ats_preset::applyFactoryPreset(*this, i);
}

const juce::String AtsAudioProcessor::getProgramName(int index)
{
    const auto names = ats_preset::factoryPresetNames();
    if (index >= 0 && index < names.size())
        return names[index];
    return {};
}

void AtsAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void AtsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    outputTrimSmoothed.reset(sampleRate, 0.02);
    outputTrimSmoothed.setCurrentAndTargetValue(
        apvts.getRawParameterValue("outputTrim")->load());
    partGainSmoothed.reset(sampleRate, 0.02);
    partGainSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue("partGain")->load());
    resGainSmoothed.reset(sampleRate, 0.02);
    resGainSmoothed.setCurrentAndTargetValue(apvts.getRawParameterValue("resGain")->load());

    juce::ignoreUnused(samplesPerBlock);
}

void AtsAudioProcessor::releaseResources() {}

bool AtsAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();
    return mainOut == juce::AudioChannelSet::stereo()
           && (mainIn == juce::AudioChannelSet::stereo() || mainIn.isDisabled());
}

juce::AudioProcessorValueTreeState::ParameterLayout AtsAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "partGain", "Partials gain", juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f, 0.35f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "resGain", "Residual gain", juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f, 0.35f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "freq", "Frequency factor", juce::NormalisableRange<float>(0.25f, 4.0f, 0.0001f, 0.35f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "timeStretch", "Time factor", juce::NormalisableRange<float>(0.25f, 4.0f, 0.0001f, 0.35f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "synStart", "Synth start (s)", juce::NormalisableRange<float>(0.0f, 600.0f, 0.001f, 0.4f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "synDur", "Synth duration (s, 0=full)", juce::NormalisableRange<float>(0.0f, 600.0f, 0.001f, 0.4f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>("usePhase", "Use partial phases", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("useAllPartials", "Use all partials", true));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "residualOnly",
        "Residual only (mute partials)",
        false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "outputTrim", "Output trim", juce::NormalisableRange<float>(0.0f, 2.0f, 0.0001f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "analysisMode",
        "Analysis mode",
        juce::StringArray { "Classic ATS", "Modern ATS", "Experimental reassigned/DDM" },
        0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "trackerMode",
        "Tracker mode",
        juce::StringArray { "Classic recursive freq/SMR", "Modern assignment" },
        0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "analysisType",
        "Analysis type",
        juce::StringArray { "1 amp+freq", "2 +phase", "3 +noise", "4 full" },
        3));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "peakEstimator",
        "Peak estimator",
        juce::StringArray { "Classic STFT", "Experimental reassigned/DDM" },
        0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "morphSourceA",
        "Morph source A",
        juce::StringArray { "Current modern model", "Previous modern model", "Disabled" },
        1));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "morphSourceB",
        "Morph source B",
        juce::StringArray { "Current modern model", "Previous modern model", "Disabled" },
        0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "morphRecipe",
        "Morph recipe",
        juce::StringArray { "Linear blend", "Harmonic lock", "Percussive protect", "Texture drift" },
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "morphFreqEnv", "Morph frequency envelope", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "morphAmpEnv", "Morph amplitude envelope", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "morphNoiseEnv", "Morph partial noise envelope", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "morphOrphanEnv", "Morph orphan residual envelope", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.5f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "morphTimeEnv", "Morph time envelope", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.5f));

    return layout;
}

void AtsAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);
    synthParamsDirty.store(true);
    triggerAsyncUpdate();
}

void AtsAudioProcessor::handleAsyncUpdate()
{
    if (synthParamsDirty.exchange(false))
        startResynthWorker();
}

void AtsAudioProcessor::setUiState(AtsUiState s) { uiState.store(s); }

void AtsAudioProcessor::setError(const juce::String& e)
{
    const juce::SpinLock::ScopedLockType sl(errorLock);
    lastError = e;
    if (e.isNotEmpty())
        setUiState(AtsUiState::Error);
}

ats::SynthParams AtsAudioProcessor::readCoreSynthParamsFromApvts() const
{
    ats::SynthParams sp;
    sp.frequencyFactor = (double) apvts.getRawParameterValue("freq")->load();
    sp.frequencyFactor *= std::pow(2.0, (double) midiSemitoneOffset.load() / 12.0);
    sp.timeFactor = apvts.getRawParameterValue("timeStretch")->load();
    sp.startTime = apvts.getRawParameterValue("synStart")->load();
    sp.duration = apvts.getRawParameterValue("synDur")->load();
    sp.usePartialsPhase = apvts.getRawParameterValue("usePhase")->load() > 0.5f;
    sp.useAllPartials = apvts.getRawParameterValue("useAllPartials")->load() > 0.5f;
    sp.sampleRate = hostSampleRate;
    sp.partialsGain = 1.0;
    sp.residualGain = 1.0;
    return sp;
}

ats::SynthParams AtsAudioProcessor::readSynthParamsFromApvts() const
{
    auto sp = readCoreSynthParamsFromApvts();
    const bool residualOnly = apvts.getRawParameterValue("residualOnly")->load() > 0.5f;
    if (residualOnly)
    {
        sp.partialsGain = 0.0;
        sp.residualGain = (double) apvts.getRawParameterValue("resGain")->load();
    }
    else
    {
        sp.partialsGain = (double) apvts.getRawParameterValue("partGain")->load();
        sp.residualGain = (double) apvts.getRawParameterValue("resGain")->load();
    }
    return sp;
}

ats::AnalysisParams AtsAudioProcessor::readAnalysisParamsFromApvts() const
{
    ats::AnalysisParams a;
    const float v = apvts.getRawParameterValue("analysisType")->load();
    const int idx = juce::jlimit(0, 3, juce::roundToInt(v));
    a.type = idx + 1;
    return a;
}

juce::String AtsAudioProcessor::currentAnalysisModeId() const
{
    const int idx = juce::jlimit(0, 2, juce::roundToInt(apvts.getRawParameterValue("analysisMode")->load()));
    return analysisModeIdForIndex(idx);
}

juce::String AtsAudioProcessor::currentTrackerModeId() const
{
    const int idx = juce::jlimit(0, 1, juce::roundToInt(apvts.getRawParameterValue("trackerMode")->load()));
    return trackerModeIdForIndex(idx);
}

juce::String AtsAudioProcessor::currentPeakEstimatorId() const
{
    const int idx = peakEstimatorIndexForAnalysisMode(
        juce::jlimit(0, 2, juce::roundToInt(apvts.getRawParameterValue("analysisMode")->load())));
    return peakEstimatorIdForIndex(idx);
}

void AtsAudioProcessor::loadSourceFile(const juce::File& file)
{
    cancelJobs();
    joinWorkers();

    auto* reader = juce::WavAudioFormat().createReaderFor(
        new juce::FileInputStream(file), true);
    if (reader == nullptr)
    {
        setError("Could not read WAV file.");
        return;
    }

    const int nCh = juce::jmax(1, (int) reader->numChannels);
    const int64_t n = reader->lengthInSamples;
    const double fileSr = reader->sampleRate;
    if (n <= 0)
    {
        setError("Empty audio file.");
        delete reader;
        return;
    }

    juce::AudioBuffer<float> tmp((int) reader->numChannels, (int) n);
    reader->read(&tmp, 0, (int) n, 0, true, true);
    delete reader;

    std::vector<double> mono(static_cast<size_t>(n));
    for (int i = 0; i < (int) n; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < nCh; ++c)
            acc += static_cast<double>(tmp.getSample(c, i));
        mono[static_cast<size_t>(i)] = acc / static_cast<double>(nCh);
    }

    {
        const juce::ScopedLock sl(sourceLock);
        sourceMono = std::move(mono);
        sourceSampleRate = (int) fileSr;
        sourceChannelCount = nCh;
    }

    {
        const juce::ScopedLock ml(modelLock);
        analyzedModel.reset(nullptr);
        latestTrackLifecycle.clear();
        latestModernModel = {};
        previousModernModel = {};
        latestModernModelValid = false;
        previousModernModelValid = false;
        analysisWarnings.clear();
    }
    activePlaybackBufferIndex.store(-1, std::memory_order_release);
    previewPlaying.store(false, std::memory_order_release);
    modelRevision.fetch_add(1, std::memory_order_relaxed);

    {
        const juce::SpinLock::ScopedLockType sl(errorLock);
        lastError.clear();
    }
    setUiState(AtsUiState::ReadyToAnalyze);
}

void AtsAudioProcessor::joinWorkers()
{
    resynthJobSerial.fetch_add(1, std::memory_order_acq_rel);
    while (resynthDetachedWorkers.load(std::memory_order_acquire) > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    if (analysisThread.joinable())
        analysisThread.join();
    if (transformThread.joinable())
        transformThread.join();
    analysisThreadRunning.store(false);
    resynthThreadRunning.store(false);
    transformThreadRunning.store(false);

    clearPreviewLayerCache();
}

void AtsAudioProcessor::clearPreviewLayerCache()
{
    previewLayerCacheValid = false;
    previewLayerCacheRevision = -1;
    previewLayerCacheHostSr = 0.0;
    previewLayerCacheCore = {};
    previewLayerCachePartials.clear();
    previewLayerCacheResidual.clear();
}

void AtsAudioProcessor::cancelJobs()
{
    jobGeneration.fetch_add(1, std::memory_order_acq_rel);
    resynthJobSerial.fetch_add(1, std::memory_order_acq_rel);
}

void AtsAudioProcessor::startAnalysis()
{
    cancelJobs();
    joinWorkers();

    std::vector<double> localMono;
    int fileSr = 0;
    int fileChannelCount = 0;
    {
        const juce::ScopedLock sl(sourceLock);
        if (sourceMono.empty())
        {
            setError("No audio loaded.");
            return;
        }
        localMono = sourceMono;
        fileSr = sourceSampleRate;
        fileChannelCount = sourceChannelCount;
    }

    if (fileSr <= 0)
    {
        setError("Invalid sample rate.");
        return;
    }

    const uint64_t gen = jobGeneration.load();
    const int selectedAnalysisMode =
        juce::jlimit(0, 2, juce::roundToInt(apvts.getRawParameterValue("analysisMode")->load()));
    const int selectedTrackerMode =
        juce::jlimit(0, 1, juce::roundToInt(apvts.getRawParameterValue("trackerMode")->load()));
    const int selectedPeakEstimator = peakEstimatorIndexForAnalysisMode(selectedAnalysisMode);
    const ats::AnalysisParams selectedAnalysisParams = readAnalysisParamsFromApvts();

    setUiState(AtsUiState::Analyzing);
    analysisProgress.store(0.05f);
    analysisThreadRunning.store(true);

    analysisThread = std::thread([this,
                                  localMono,
                                  fileSr,
                                  gen,
                                  selectedAnalysisMode,
                                  selectedTrackerMode,
                                  selectedPeakEstimator,
                                  fileChannelCount,
                                  selectedAnalysisParams]() {
        ats::AnalysisParams ap = selectedAnalysisParams;
        analysisProgress.store(0.15f);
        auto result = ats::runClassicAtsAnalysis(ap, localMono, fileSr);

        if (jobGeneration.load() != gen)
        {
            analysisThreadRunning.store(false);
            return;
        }

        if (!result.sound)
        {
            analysisThreadRunning.store(false);
            setError(result.errorMessage.empty() ? juce::String("Classic ATS analysis failed.")
                                                 : juce::String(result.errorMessage));
            return;
        }

        analysisProgress.store(0.85f);

        ats::ModernAtsModel modernModel;
        bool modernModelValid = false;
        std::string modernValidationError;
        if (selectedAnalysisMode != kAnalysisModeClassic)
        {
            modernModel = buildModernModelFromClassicAtsSound(*result.sound.get(),
                                                              ap,
                                                              selectedAnalysisMode,
                                                              selectedTrackerMode,
                                                              selectedPeakEstimator,
                                                              fileChannelCount,
                                                              result.trackLifecycle);
            modernModelValid = ats::validateModernAtsModel(modernModel, modernValidationError);
        }

        {
            const juce::ScopedLock ml(modelLock);
            analyzedModel = std::move(result.sound);
            latestTrackLifecycle = std::move(result.trackLifecycle);
            analysisWarnings.clear();

            if (modernModelValid)
            {
                if (latestModernModelValid)
                {
                    previousModernModel = latestModernModel;
                    previousModernModelValid = true;
                }
                latestModernModel = std::move(modernModel);
                latestModernModelValid = true;
            }
            else
            {
                latestModernModel = {};
                previousModernModel = {};
                latestModernModelValid = false;
                previousModernModelValid = false;
            }

            if (selectedAnalysisMode == kAnalysisModeModern)
            {
                analysisWarnings.push_back(selectedTrackerMode == kTrackerModeAssignment
                                               ? "Modern assignment tracker routed into the modern diagnostics model."
                                               : "Modern mode using classic tracker for compatibility.");
            }
            else if (selectedAnalysisMode == kAnalysisModeExperimental)
            {
                analysisWarnings.push_back(selectedTrackerMode == kTrackerModeAssignment
                                               ? "Experimental reassigned/DDM mode using the modern assignment tracker."
                                               : "Experimental reassigned/DDM mode using the classic tracker.");
            }
            if (!modernModelValid && selectedAnalysisMode != kAnalysisModeClassic)
                analysisWarnings.push_back("Analysis diagnostics unavailable: "
                                           + juce::String(modernValidationError));
        }

        modelRevision.fetch_add(1, std::memory_order_relaxed);

        analysisProgress.store(1.0f);
        analysisThreadRunning.store(false);

        if (jobGeneration.load() != gen)
            return;

        setUiState(AtsUiState::Analyzed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
    });
}

void AtsAudioProcessor::startResynthWorker()
{
    {
        const juce::ScopedLock ml(modelLock);
        if (analyzedModel.get() == nullptr)
            return;
    }

    const uint64_t mySerial = resynthJobSerial.fetch_add(1, std::memory_order_acq_rel) + 1;

    setUiState(AtsUiState::Resynthesizing);
    resynthThreadRunning.store(true);

    std::thread([this, mySerial]() {
        struct DetachedWorkerCount
        {
            std::atomic<int>& n;
            explicit DetachedWorkerCount(std::atomic<int>& x) : n(x)
            {
                n.fetch_add(1, std::memory_order_acq_rel);
            }
            ~DetachedWorkerCount() { n.fetch_sub(1, std::memory_order_acq_rel); }
        } counter(resynthDetachedWorkers);

        std::lock_guard<std::mutex> resynthLock(resynthMutex);
        if (mySerial != resynthJobSerial.load(std::memory_order_acquire))
        {
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        const uint64_t gen = jobGeneration.load(std::memory_order_relaxed);
        const ats::SynthParams core = readCoreSynthParamsFromApvts();

        juce::AudioBuffer<float> monoP;
        juce::AudioBuffer<float> monoR;
        int modelSr = 44100;
        int modelRevSnapshot = -1;
        bool useLayerCache = false;
        bool useModernSynth = false;
        ats::ModernAtsModel modernModelSnapshot;

        {
            const juce::ScopedLock ml(modelLock);
            if (jobGeneration.load() != gen || analyzedModel.get() == nullptr)
            {
                resynthThreadRunning.store(false);
                setUiState(AtsUiState::Analyzed);
                return;
            }

            modelRevSnapshot = modelRevision.load(std::memory_order_relaxed);
            useLayerCache = previewLayerCacheValid
                            && previewLayerCacheRevision == modelRevSnapshot
                            && (std::abs(previewLayerCacheHostSr - hostSampleRate) < 1e-6)
                            && samePreviewLayerCore(previewLayerCacheCore, core)
                            && (!previewLayerCachePartials.empty() || !previewLayerCacheResidual.empty());

            useModernSynth = latestModernModelValid;
            if (useModernSynth)
                modernModelSnapshot = latestModernModel;

            if (!useLayerCache)
            {
                if (useModernSynth)
                {
                    modelSr = modernModelSnapshot.header.sampleRate > 0
                                  ? modernModelSnapshot.header.sampleRate
                                  : analyzedModel.get()->srate;
                }
                else
                {
                    modelSr = analyzedModel.get()->srate;

                    ats::SynthParams spPartials = core;
                    spPartials.partialsGain = 1.0;
                    spPartials.residualGain = 0.0;
                    ats::SynthParams spResidual = core;
                    spResidual.partialsGain = 0.0;
                    spResidual.residualGain = 1.0;

                    monoP = runDoSynthesis(analyzedModel.get(), spPartials);
                    monoR = runDoSynthesis(analyzedModel.get(), spResidual);
                }
            }
        }

        if (!useLayerCache && useModernSynth)
        {
            ats::ModernAtsSynthConfig cfgP;
            cfgP.outputSampleRate = modelSr;
            cfgP.partialGain = 1.0;
            cfgP.orphanResidualGain = 0.0;
            cfgP.transientGain = 0.0;
            cfgP.useStoredPhase = core.usePartialsPhase;
            cfgP.timeFactor = core.timeFactor;
            cfgP.frequencyFactor = core.frequencyFactor;

            auto partialsVec = ats::renderModernAtsModel(modernModelSnapshot, cfgP);
            if (!partialsVec.empty())
            {
                monoP.setSize(1, static_cast<int>(partialsVec.size()));
                for (int i = 0; i < static_cast<int>(partialsVec.size()); ++i)
                    monoP.setSample(0, i, partialsVec[i]);
            }

            ats::ModernAtsSynthConfig cfgR;
            cfgR.outputSampleRate = modelSr;
            cfgR.partialGain = 0.0;
            cfgR.orphanResidualGain = 1.0;
            cfgR.transientGain = 1.0;
            cfgR.timeFactor = core.timeFactor;
            // frequencyFactor is not applied to noise/residual (no pitched content)

            auto residualVec = ats::renderModernAtsModel(modernModelSnapshot, cfgR);
            if (!residualVec.empty())
            {
                monoR.setSize(1, static_cast<int>(residualVec.size()));
                for (int i = 0; i < static_cast<int>(residualVec.size()); ++i)
                    monoR.setSample(0, i, residualVec[i]);
            }
        }

        if (jobGeneration.load() != gen || mySerial != resynthJobSerial.load(std::memory_order_acquire))
        {
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        if (modelRevision.load(std::memory_order_relaxed) != modelRevSnapshot)
        {
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        if (!useLayerCache)
        {
            const int nP = monoP.getNumSamples();
            const int nR = monoR.getNumSamples();
            if (nP <= 0 && nR <= 0)
            {
                setError("Synthesis produced silence.");
                resynthThreadRunning.store(false);
                setUiState(AtsUiState::Analyzed);
                return;
            }

            std::vector<float> resampledP;
            std::vector<float> resampledR;
            if (nP > 0)
                ats::resampleLinearMono(
                    monoP.getReadPointer(0),
                    nP,
                    (double) modelSr,
                    resampledP,
                    hostSampleRate);
            if (nR > 0)
                ats::resampleLinearMono(
                    monoR.getReadPointer(0),
                    nR,
                    (double) modelSr,
                    resampledR,
                    hostSampleRate);

            if (resampledP.empty() && resampledR.empty())
            {
                setError("Synthesis produced silence.");
                resynthThreadRunning.store(false);
                setUiState(AtsUiState::Analyzed);
                return;
            }

            if (jobGeneration.load() != gen || mySerial != resynthJobSerial.load(std::memory_order_acquire)
                || modelRevision.load(std::memory_order_relaxed) != modelRevSnapshot)
            {
                resynthThreadRunning.store(false);
                setUiState(AtsUiState::Analyzed);
                return;
            }

            previewLayerCachePartials = std::move(resampledP);
            previewLayerCacheResidual = std::move(resampledR);
            previewLayerCacheCore = core;
            previewLayerCacheRevision = modelRevSnapshot;
            previewLayerCacheHostSr = hostSampleRate;
            previewLayerCacheValid = true;
        }

        const size_t outN = juce::jmax(previewLayerCachePartials.size(), previewLayerCacheResidual.size());
        if (outN == 0)
        {
            setError("Synthesis produced silence.");
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        if (jobGeneration.load() != gen || mySerial != resynthJobSerial.load(std::memory_order_acquire))
        {
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        const int activeIndex = activePlaybackBufferIndex.load(std::memory_order_acquire);
        const int readingIndex = readingPlaybackBufferIndex.load(std::memory_order_acquire);
        int writeIndex = nextPlaybackBufferIndex;
        for (int attempt = 0; attempt < (int) playbackBuffers.size(); ++attempt)
        {
            const int candidate = (nextPlaybackBufferIndex + attempt) % (int) playbackBuffers.size();
            if (candidate != activeIndex && candidate != readingIndex)
            {
                writeIndex = candidate;
                break;
            }
        }
        nextPlaybackBufferIndex = (writeIndex + 1) % (int) playbackBuffers.size();

        auto& stereo = playbackBuffers[(size_t) writeIndex];
        stereo.setSize(2, (int) outN);
        for (int i = 0; i < (int) outN; ++i)
        {
            const float p = (i < (int) previewLayerCachePartials.size())
                                ? previewLayerCachePartials[(size_t) i]
                                : 0.0f;
            const float r = (i < (int) previewLayerCacheResidual.size())
                                ? previewLayerCacheResidual[(size_t) i]
                                : 0.0f;
            stereo.setSample(0, i, p);
            stereo.setSample(1, i, r);
        }

        if (mySerial != resynthJobSerial.load(std::memory_order_acquire))
        {
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        activePlaybackBufferIndex.store(writeIndex, std::memory_order_release);
        previewPlaying.store(false, std::memory_order_release);

        resynthThreadRunning.store(false);
        setUiState(AtsUiState::Analyzed);
    }).detach();
}

void AtsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const float prev = midiSemitoneOffset.load();
        float next = prev;
        if (m.isNoteOn() && m.getVelocity() > 0)
            next = (float) (m.getNoteNumber() - 60);
        else if (m.isNoteOff())
            next = 0.0f;

        if (next != prev)
        {
            midiSemitoneOffset.store(next);
            synthParamsDirty.store(true);
            triggerAsyncUpdate();
        }
    }

    outputTrimSmoothed.setTargetValue(apvts.getRawParameterValue("outputTrim")->load());
    partGainSmoothed.setTargetValue(apvts.getRawParameterValue("partGain")->load());
    resGainSmoothed.setTargetValue(apvts.getRawParameterValue("resGain")->load());
    const bool residualOnlyMix = apvts.getRawParameterValue("residualOnly")->load() > 0.5f;

    const int n = buffer.getNumSamples();
    const int activeIndex = activePlaybackBufferIndex.load(std::memory_order_acquire);
    const bool hasPlayback = activeIndex >= 0 && activeIndex < (int) playbackBuffers.size();
    readingPlaybackBufferIndex.store(hasPlayback ? activeIndex : -1, std::memory_order_release);

    if (activeIndex != currentPlaybackBufferIndex)
    {
        currentPlaybackBufferIndex = activeIndex;
        playbackPosition = 0;
    }

    const auto* playback = hasPlayback ? &playbackBuffers[(size_t) activeIndex] : nullptr;
    const bool shouldPreview = previewPlaying.load(std::memory_order_acquire);

    for (int s = 0; s < n; ++s)
    {
        const float trim = outputTrimSmoothed.getNextValue();
        const float pg = partGainSmoothed.getNextValue();
        const float rg = resGainSmoothed.getNextValue();
        float v = 0.0f;
        if (shouldPreview && playback != nullptr && playbackPosition < playback->getNumSamples())
        {
            const int nCh = playback->getNumChannels();
            const float part = playback->getSample(0, playbackPosition);
            const float res = nCh > 1 ? playback->getSample(1, playbackPosition) : 0.0f;
            const float mixed = residualOnlyMix ? (res * rg) : (part * pg + res * rg);
            v = mixed * trim;
            ++playbackPosition;
        }

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            buffer.setSample(c, s, v);
    }

    readingPlaybackBufferIndex.store(-1, std::memory_order_release);
}

void AtsAudioProcessor::startPreviewPlayback()
{
    if (!hasPreviewAudio())
        return;
    playbackPosition = 0;
    previewPlaying.store(true, std::memory_order_release);
}

void AtsAudioProcessor::stopPreviewPlayback()
{
    previewPlaying.store(false, std::memory_order_release);
    playbackPosition = 0;
}

bool AtsAudioProcessor::hasPreviewAudio() const
{
    const int idx = activePlaybackBufferIndex.load(std::memory_order_acquire);
    if (idx < 0 || idx >= (int) playbackBuffers.size())
        return false;
    return playbackBuffers[(size_t) idx].getNumSamples() > 0;
}

juce::AudioProcessorEditor* AtsAudioProcessor::createEditor()
{
    return new AtsAudioProcessorEditor(*this);
}

void AtsAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos;

    mos.writeInt((int) stateMagic);
    mos.writeInt(kStateVersion);

    ats::AnalysisParams ap = readAnalysisParamsFromApvts();
    writeAnalysis(mos, ap);

    ats::SynthParams sp = readSynthParamsFromApvts();
    sp.partialsGain = (double) apvts.getRawParameterValue("partGain")->load();
    sp.residualGain = (double) apvts.getRawParameterValue("resGain")->load();
    writeSynth(mos, sp);
    mos.writeDouble(apvts.getRawParameterValue("outputTrim")->load());

    juce::MemoryBlock atsData;
    {
        const juce::ScopedLock ml(modelLock);
        if (analyzedModel.get() != nullptr)
        {
            juce::File tmp = juce::File::createTempFile("atsstate");
            tmp.deleteFile();
            const juce::String path = tmp.getFullPathName();
            if (ats_save(analyzedModel.get(), path.toUTF8(), ap.SMR_thres, ap.type) >= 0
                && tmp.existsAsFile() && tmp.getSize() > 0)
            {
                juce::FileInputStream fis(path);
                juce::MemoryOutputStream chunk;
                chunk.writeFromInputStream(fis, fis.getTotalLength());
                atsData = chunk.getMemoryBlock();
            }
            tmp.deleteFile();
        }
    }

    mos.writeInt64((juce::int64) atsData.getSize());
    mos.write(atsData.getData(), atsData.getSize());

    if (kStateVersion >= 4)
        mos.writeByte(apvts.getRawParameterValue("residualOnly")->load() > 0.5f ? (juce::uint8) 1 : (juce::uint8) 0);
    if (kStateVersion >= 5)
    {
        mos.writeInt(juce::roundToInt(apvts.getRawParameterValue("analysisMode")->load()));
        mos.writeInt(juce::roundToInt(apvts.getRawParameterValue("trackerMode")->load()));
        mos.writeInt(juce::roundToInt(apvts.getRawParameterValue("morphSourceA")->load()));
        mos.writeInt(juce::roundToInt(apvts.getRawParameterValue("morphSourceB")->load()));
        mos.writeInt(juce::roundToInt(apvts.getRawParameterValue("morphRecipe")->load()));
        mos.writeInt(peakEstimatorIndexForAnalysisMode(
            juce::jlimit(0, 2, juce::roundToInt(apvts.getRawParameterValue("analysisMode")->load()))));
        mos.writeFloat(apvts.getRawParameterValue("morphFreqEnv")->load());
        mos.writeFloat(apvts.getRawParameterValue("morphAmpEnv")->load());
        mos.writeFloat(apvts.getRawParameterValue("morphNoiseEnv")->load());
        mos.writeFloat(apvts.getRawParameterValue("morphOrphanEnv")->load());
        mos.writeFloat(apvts.getRawParameterValue("morphTimeEnv")->load());
    }

    destData = mos.getMemoryBlock();
}

void AtsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    cancelJobs();
    joinWorkers();

    juce::MemoryInputStream mis(data, (size_t) sizeInBytes, false);
    if (mis.readInt() != (int) stateMagic)
        return;

    const int ver = mis.readInt();
    if (ver < kMinReadableStateVersion || ver > kStateVersion)
        return;

    ats::AnalysisParams ap;
    ats::SynthParams sp;
    if (!readAnalysis(mis, ap) || !readSynth(mis, sp))
        return;

    float outputTrim = 1.0f;
    if (ver >= 3)
    {
        if (mis.getNumBytesRemaining() < (juce::int64) sizeof(double))
            return;
        outputTrim = (float) mis.readDouble();
    }

    const juce::int64 atsSize = mis.readInt64();
    juce::MemoryBlock atsData;
    if (atsSize > 0 && mis.getNumBytesRemaining() >= atsSize)
    {
        atsData.setSize((size_t) atsSize);
        mis.read(atsData.getData(), (size_t) atsSize);
    }

    bool residualOnly = false;
    if (ver >= 4 && mis.getNumBytesRemaining() > 0)
        residualOnly = mis.readByte() != 0;
    int analysisMode = 0;
    int trackerMode = 0;
    int morphSourceA = 0;
    int morphSourceB = 0;
    int morphRecipe = 0;
    int peakEstimator = 0;
    float morphFreqEnv = 0.5f;
    float morphAmpEnv = 0.5f;
    float morphNoiseEnv = 0.5f;
    float morphOrphanEnv = 0.5f;
    float morphTimeEnv = 0.5f;
    if (ver >= 6 && mis.getNumBytesRemaining() >= (juce::int64) (sizeof(int) * 6 + sizeof(float) * 5))
    {
        analysisMode = mis.readInt();
        trackerMode = mis.readInt();
        morphSourceA = mis.readInt();
        morphSourceB = mis.readInt();
        morphRecipe = mis.readInt();
        peakEstimator = mis.readInt();
        morphFreqEnv = mis.readFloat();
        morphAmpEnv = mis.readFloat();
        morphNoiseEnv = mis.readFloat();
        morphOrphanEnv = mis.readFloat();
        morphTimeEnv = mis.readFloat();
    }
    else if (ver == 5 && mis.getNumBytesRemaining() >= (juce::int64) (sizeof(int) * 5 + sizeof(float) * 5))
    {
        analysisMode = mis.readInt();
        trackerMode = mis.readInt();
        morphSourceA = mis.readInt();
        morphSourceB = mis.readInt();
        morphRecipe = mis.readInt();
        morphFreqEnv = mis.readFloat();
        morphAmpEnv = mis.readFloat();
        morphNoiseEnv = mis.readFloat();
        morphOrphanEnv = mis.readFloat();
        morphTimeEnv = mis.readFloat();
    }

    if (auto* p = apvts.getParameter("analysisType"))
        p->setValueNotifyingHost((float) (juce::jlimit(1, 4, ap.type) - 1) / 3.0f);

    if (auto* p = apvts.getParameter("partGain"))
        p->setValueNotifyingHost(apvts.getParameterRange("partGain").convertTo0to1((float) sp.partialsGain));
    if (auto* p = apvts.getParameter("resGain"))
        p->setValueNotifyingHost(apvts.getParameterRange("resGain").convertTo0to1((float) sp.residualGain));
    if (auto* p = apvts.getParameter("freq"))
        p->setValueNotifyingHost(apvts.getParameterRange("freq").convertTo0to1((float) sp.frequencyFactor));
    if (auto* p = apvts.getParameter("timeStretch"))
        p->setValueNotifyingHost(apvts.getParameterRange("timeStretch").convertTo0to1((float) sp.timeFactor));
    if (auto* p = apvts.getParameter("synStart"))
        p->setValueNotifyingHost(apvts.getParameterRange("synStart").convertTo0to1((float) sp.startTime));
    if (auto* p = apvts.getParameter("synDur"))
        p->setValueNotifyingHost(apvts.getParameterRange("synDur").convertTo0to1((float) sp.duration));
    if (auto* p = apvts.getParameter("usePhase"))
        p->setValueNotifyingHost(sp.usePartialsPhase ? 1.0f : 0.0f);
    if (auto* p = apvts.getParameter("useAllPartials"))
        p->setValueNotifyingHost(sp.useAllPartials ? 1.0f : 0.0f);
    if (auto* p = apvts.getParameter("residualOnly"))
        p->setValueNotifyingHost(residualOnly ? 1.0f : 0.0f);
    if (auto* p = apvts.getParameter("outputTrim"))
        p->setValueNotifyingHost(apvts.getParameterRange("outputTrim").convertTo0to1(outputTrim));
    analysisMode = juce::jlimit(0, 2, analysisMode);
    if (peakEstimator == kPeakEstimatorExperimental && analysisMode != kAnalysisModeExperimental)
        analysisMode = kAnalysisModeExperimental;
    peakEstimator = peakEstimatorIndexForAnalysisMode(analysisMode);

    if (auto* p = apvts.getParameter("analysisMode"))
        p->setValueNotifyingHost((float) analysisMode / 2.0f);
    if (auto* p = apvts.getParameter("trackerMode"))
        p->setValueNotifyingHost((float) juce::jlimit(0, 1, trackerMode));
    if (auto* p = apvts.getParameter("morphSourceA"))
        p->setValueNotifyingHost((float) juce::jlimit(0, 2, morphSourceA) / 2.0f);
    if (auto* p = apvts.getParameter("morphSourceB"))
        p->setValueNotifyingHost((float) juce::jlimit(0, 2, morphSourceB) / 2.0f);
    if (auto* p = apvts.getParameter("morphRecipe"))
        p->setValueNotifyingHost((float) juce::jlimit(0, 3, morphRecipe) / 3.0f);
    if (auto* p = apvts.getParameter("peakEstimator"))
        p->setValueNotifyingHost((float) juce::jlimit(0, 1, peakEstimator));
    if (auto* p = apvts.getParameter("morphFreqEnv"))
        p->setValueNotifyingHost(apvts.getParameterRange("morphFreqEnv").convertTo0to1(morphFreqEnv));
    if (auto* p = apvts.getParameter("morphAmpEnv"))
        p->setValueNotifyingHost(apvts.getParameterRange("morphAmpEnv").convertTo0to1(morphAmpEnv));
    if (auto* p = apvts.getParameter("morphNoiseEnv"))
        p->setValueNotifyingHost(apvts.getParameterRange("morphNoiseEnv").convertTo0to1(morphNoiseEnv));
    if (auto* p = apvts.getParameter("morphOrphanEnv"))
        p->setValueNotifyingHost(apvts.getParameterRange("morphOrphanEnv").convertTo0to1(morphOrphanEnv));
    if (auto* p = apvts.getParameter("morphTimeEnv"))
        p->setValueNotifyingHost(apvts.getParameterRange("morphTimeEnv").convertTo0to1(morphTimeEnv));

    if (atsData.getSize() > 0)
    {
        juce::File tmp = juce::File::createTempFile("atsload");
        tmp.replaceWithData(atsData.getData(), atsData.getSize());
        auto* raw = (ATS_SOUND*) std::calloc(1, sizeof(ATS_SOUND));
        int st = 0;
        if (ats_load(raw, tmp.getFullPathName().toUTF8(), &st) >= 0)
        {
            const juce::ScopedLock ml(modelLock);
            analyzedModel.reset(raw);
            latestTrackLifecycle.clear();
            latestModernModel = {};
            previousModernModel = {};
            latestModernModelValid = false;
            previousModernModelValid = false;
            analysisWarnings.clear();
            activePlaybackBufferIndex.store(-1, std::memory_order_release);
            previewPlaying.store(false, std::memory_order_release);
            modelRevision.fetch_add(1, std::memory_order_relaxed);
            setUiState(AtsUiState::Analyzed);
            synthParamsDirty.store(true);
            triggerAsyncUpdate();
        }
        else
        {
            std::free(raw);
            const juce::ScopedLock ml(modelLock);
            analyzedModel.reset(nullptr);
            latestTrackLifecycle.clear();
            latestModernModel = {};
            previousModernModel = {};
            latestModernModelValid = false;
            previousModernModelValid = false;
            analysisWarnings.clear();
            activePlaybackBufferIndex.store(-1, std::memory_order_release);
            previewPlaying.store(false, std::memory_order_release);
            modelRevision.fetch_add(1, std::memory_order_relaxed);
            bool hasSource = false;
            {
                const juce::ScopedLock sl(sourceLock);
                hasSource = !sourceMono.empty();
            }
            setUiState(hasSource ? AtsUiState::ReadyToAnalyze : AtsUiState::NoSource);
            synthParamsDirty.store(true);
            triggerAsyncUpdate();
        }
        tmp.deleteFile();
    }
    else
    {
        const juce::ScopedLock ml(modelLock);
        analyzedModel.reset(nullptr);
        latestTrackLifecycle.clear();
        latestModernModel = {};
        previousModernModel = {};
        latestModernModelValid = false;
        previousModernModelValid = false;
        analysisWarnings.clear();
        activePlaybackBufferIndex.store(-1, std::memory_order_release);
        previewPlaying.store(false, std::memory_order_release);
        modelRevision.fetch_add(1, std::memory_order_relaxed);
        bool hasSource = false;
        {
            const juce::ScopedLock sl(sourceLock);
            hasSource = !sourceMono.empty();
        }
        setUiState(hasSource ? AtsUiState::ReadyToAnalyze : AtsUiState::NoSource);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
    }
}

bool AtsAudioProcessor::isWorkerBusy() const
{
    return analysisThreadRunning.load() || resynthThreadRunning.load() || transformThreadRunning.load();
}

void AtsAudioProcessor::setMathStatus(const juce::String& s)
{
    const juce::SpinLock::ScopedLockType sl(mathStatusLock);
    mathStatus = s;
}

juce::String AtsAudioProcessor::getMathStatus() const
{
    const juce::SpinLock::ScopedLockType sl(mathStatusLock);
    return mathStatus;
}

bool AtsAudioProcessor::readPartialCell(int partialIdx,
                                       int frameIdx,
                                       double& amp,
                                       double& freq,
                                       double& phase,
                                       bool& hasPhase)
{
    const juce::ScopedLock ml(modelLock);
    auto* s = analyzedModel.get();
    if (s == nullptr || s->amp == nullptr || s->frq == nullptr)
        return false;
    if (partialIdx < 0 || frameIdx < 0 || partialIdx >= s->partials || frameIdx >= s->frames)
        return false;

    amp = s->amp[partialIdx][frameIdx];
    freq = s->frq[partialIdx][frameIdx];
    hasPhase = (s->pha != nullptr);
    phase = hasPhase ? s->pha[partialIdx][frameIdx] : 0.0;
    return true;
}

bool AtsAudioProcessor::writePartialCell(int partialIdx, int frameIdx, double amp, double freq, juce::String& error)
{
    error.clear();
    if (!std::isfinite(amp) || !std::isfinite(freq))
    {
        error = "Values must be finite numbers.";
        return false;
    }
    if (amp < 0.0)
    {
        error = "Amplitude must be non-negative.";
        return false;
    }
    if (freq <= 1e-6)
    {
        error = "Frequency must be greater than zero.";
        return false;
    }

    const juce::ScopedLock ml(modelLock);
    auto* s = analyzedModel.get();
    if (s == nullptr || s->amp == nullptr || s->frq == nullptr)
    {
        error = "No model.";
        return false;
    }
    if (partialIdx < 0 || frameIdx < 0 || partialIdx >= s->partials || frameIdx >= s->frames)
    {
        error = "Partial or frame index out of range.";
        return false;
    }

    s->amp[partialIdx][frameIdx] = amp;
    s->frq[partialIdx][frameIdx] = freq;
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    synthParamsDirty.store(true);
    triggerAsyncUpdate();
    return true;
}

void AtsAudioProcessor::startMathExpr(const juce::String& script)
{
    cancelJobs();
    joinWorkers();

    const std::string srcCopy = script.toStdString();
    if (srcCopy.empty())
    {
        setMathStatus("Empty script.");
        return;
    }

    const uint64_t gen = jobGeneration.load();
    transformThreadRunning.store(true);
    setUiState(AtsUiState::ApplyingMath);
    setMathStatus("Running…");

    transformThread = std::thread([this, srcCopy, gen]() {
        bool applied = false;
        juce::String msg;

        {
            const juce::ScopedLock ml(modelLock);
            if (jobGeneration.load() != gen)
            {
                transformThreadRunning.store(false);
                setMathStatus("Cancelled.");
                setUiState(AtsUiState::Analyzed);
                return;
            }

            auto* s = analyzedModel.get();
            if (s == nullptr)
                msg = "No model.";
            else
            {
                std::string err;
                const auto shouldCancel = [this, gen]() -> bool {
                    return jobGeneration.load() != gen;
                };

                if (ats::applyMathExprScript(s, srcCopy, err, shouldCancel))
                {
                    applied = true;
                    msg = "Script applied OK.";
                }
                else
                {
                    msg = juce::String(err.c_str());
                }
            }
        }

        transformThreadRunning.store(false);

        if (jobGeneration.load() != gen)
        {
            setMathStatus("Cancelled.");
            setUiState(AtsUiState::Analyzed);
            return;
        }

        setMathStatus(msg);
        if (applied)
        {
            modelRevision.fetch_add(1, std::memory_order_relaxed);
            synthParamsDirty.store(true);
            triggerAsyncUpdate();
        }
        setUiState(AtsUiState::Analyzed);
    });
}

bool AtsAudioProcessor::getModelFootprint(int& partials, int& frames, bool& hasResidualBands) const
{
    const juce::ScopedLock ml(modelLock);
    auto* s = analyzedModel.get();
    if (s == nullptr)
        return false;
    partials = s->partials;
    frames = s->frames;
    hasResidualBands = (s->band_energy != nullptr);
    return true;
}

bool AtsAudioProcessor::getModernDiagnosticsSummary(juce::String& outSummary, juce::String& outWarnings) const
{
    const juce::ScopedLock ml(modelLock);
    if (!latestModernModelValid)
        return false;

    double orphanResidualEnergy = 0.0;
    for (const auto& frame : latestModernModel.frames)
    {
        for (const double energy : frame.orphanResidualBandEnergy)
            orphanResidualEnergy += std::max(0.0, energy);
    }

    int births = 0;
    int deaths = 0;
    int gapFills = 0;
    int pruned = 0;
    for (const auto& event : latestModernModel.trackLifecycle)
    {
        switch (event.kind)
        {
        case ats::ModernAtsLifecycleKind::Birth: ++births; break;
        case ats::ModernAtsLifecycleKind::Death: ++deaths; break;
        case ats::ModernAtsLifecycleKind::GapFill: ++gapFills; break;
        case ats::ModernAtsLifecycleKind::Pruned: ++pruned; break;
        default: break;
        }
    }

    outSummary = "Analysis diagnostics - mode: "
                 + juce::String(analysisModeDisplayNameForIndex(
                     latestModernModel.header.analysisMode == ats::ModernAtsAnalysisMode::Experimental
                         ? kAnalysisModeExperimental
                         : (latestModernModel.header.analysisMode == ats::ModernAtsAnalysisMode::ModernAts
                                ? kAnalysisModeModern
                                : kAnalysisModeClassic)))
                 + ", tracker: "
                 + juce::String(latestModernModel.header.trackerMode == ats::ModernAtsTrackerMode::ModernAssignment
                                    ? "Modern assignment"
                                    : "Classic recursive freq/SMR")
                 + ", partials: " + juce::String((int) latestModernModel.partials.size())
                 + ", frames: " + juce::String((int) latestModernModel.frames.size())
                 + ", transient markers: " + juce::String((int) latestModernModel.transients.size())
                 + ", orphan residual energy: " + juce::String(orphanResidualEnergy, 3)
                 + ", lifecycle [birth " + juce::String(births) + ", death " + juce::String(deaths)
                 + ", gap-fill " + juce::String(gapFills) + ", pruned " + juce::String(pruned) + "]";

    const int sourceA =
        juce::jlimit(0, 2, juce::roundToInt(apvts.getRawParameterValue("morphSourceA")->load()));
    const int sourceB =
        juce::jlimit(0, 2, juce::roundToInt(apvts.getRawParameterValue("morphSourceB")->load()));
    const auto* morphSource = selectMorphSource(sourceA,
                                                latestModernModel,
                                                latestModernModelValid,
                                                previousModernModel,
                                                previousModernModelValid);
    const auto* morphTarget = selectMorphSource(sourceB,
                                                latestModernModel,
                                                latestModernModelValid,
                                                previousModernModel,
                                                previousModernModelValid);
    if (sourceA == kMorphSourceDisabled || sourceB == kMorphSourceDisabled)
    {
        outSummary << "\nMorph - disabled";
    }
    else if (morphSource == nullptr || morphTarget == nullptr)
    {
        outSummary << "\nMorph - waiting for current and previous modern analyses";
    }
    else
    {
        auto recipe = morphRecipeFromApvts(apvts);
        auto correspondences = ats::buildAutomaticCorrespondence(*morphSource, *morphTarget, recipe);
        int matched = 0;
        int unmatchedSource = 0;
        int unmatchedTarget = 0;
        for (const auto& c : correspondences)
        {
            if (c.sourcePartialId >= 0 && c.targetPartialId >= 0)
                ++matched;
            else if (c.sourcePartialId >= 0)
                ++unmatchedSource;
            else if (c.targetPartialId >= 0)
                ++unmatchedTarget;
        }

        outSummary << "\nMorph - matched: " << matched
                   << ", unmatched A: " << unmatchedSource
                   << ", unmatched B: " << unmatchedTarget
                   << ", candidate partials: " << (matched + unmatchedSource + unmatchedTarget);
    }

    outWarnings.clear();
    for (int i = 0; i < (int) analysisWarnings.size(); ++i)
    {
        if (i > 0)
            outWarnings << "\n";
        outWarnings << "- " << analysisWarnings[(size_t) i];
    }
    return true;
}

bool AtsAudioProcessor::getModernLifecycleMarkers(std::vector<ModernLifecycleMarker>& out) const
{
    const juce::ScopedLock ml(modelLock);
    if (!latestModernModelValid || latestModernModel.frames.empty())
        return false;
    out.clear();
    const int frameCount = static_cast<int>(latestModernModel.frames.size());
    for (const auto& event : latestModernModel.trackLifecycle)
    {
        if (event.kind != ats::ModernAtsLifecycleKind::Birth
            && event.kind != ats::ModernAtsLifecycleKind::Death
            && event.kind != ats::ModernAtsLifecycleKind::GapFill)
            continue;
        if (event.frameIndex < 0 || event.frameIndex >= frameCount)
            continue;
        ModernLifecycleMarker marker;
        marker.normalizedTime =
            frameCount > 1 ? static_cast<float>(event.frameIndex) / static_cast<float>(frameCount - 1) : 0.0f;
        marker.kind = event.kind;
        out.push_back(marker);
    }
    return !out.empty();
}

bool AtsAudioProcessor::buildSpectrogramTexture(bool partialsMode, int width, int height, juce::Image& out)
{
    if (width <= 0 || height <= 0)
        return false;

    out = juce::Image(juce::Image::ARGB, width, height, true);
    out.clear(juce::Rectangle<int>(width, height), juce::Colours::black);

    const juce::ScopedLock ml(modelLock);

    const float dbMin = -90.0f;
    const float dbMax = -10.0f;

    // Use modern model when available (Modern ATS or Experimental analysis modes).
    if (latestModernModelValid && !latestModernModel.partials.empty())
    {
        juce::Image::BitmapData bd(out, juce::Image::BitmapData::writeOnly);
        const int numPartials = (int) latestModernModel.partials.size();
        const int numFrames = (int) latestModernModel.frames.size();

        if (partialsMode)
        {
            if (numPartials <= 0 || numFrames <= 0)
                return true;

            for (int y = 0; y < height; ++y)
            {
                const int par = juce::jmin(numPartials - 1,
                    (int) ((int64_t) y * (int64_t) numPartials / (int64_t) height));
                const auto& partial = latestModernModel.partials[(std::size_t) par];
                const int partFrames = (int) partial.frames.size();
                if (partFrames <= 0)
                    continue;
                for (int x = 0; x < width; ++x)
                {
                    const int fr = juce::jmin(partFrames - 1,
                        (int) ((int64_t) x * (int64_t) partFrames / (int64_t) width));
                    const double a = partial.frames[(std::size_t) fr].amplitude;
                    const float db = (float) (20.0 * std::log10(juce::jmax(1e-15, a)));
                    const float t = juce::jlimit(0.0f, 1.0f, (db - dbMin) / (dbMax - dbMin));
                    const auto col = juce::Colour::fromHSV(0.62f - 0.35f * t, 0.55f + 0.35f * t, 0.15f + 0.75f * t, 1.0f);
                    bd.setPixelColour(x, y, col);
                }
            }
        }
        else
        {
            // Orphan residual band energy stored per frame in the modern model.
            int numBands = 0;
            for (const auto& frame : latestModernModel.frames)
                numBands = juce::jmax(numBands, (int) frame.orphanResidualBandEnergy.size());

            if (numBands <= 0 || numFrames <= 0)
                return true;

            for (int y = 0; y < height; ++y)
            {
                const int band = juce::jmin(numBands - 1,
                    (int) ((int64_t) y * (int64_t) numBands / (int64_t) height));
                for (int x = 0; x < width; ++x)
                {
                    const int fr = juce::jmin(numFrames - 1,
                        (int) ((int64_t) x * (int64_t) numFrames / (int64_t) width));
                    double e = 0.0;
                    const auto& frame = latestModernModel.frames[(std::size_t) fr];
                    if (band < (int) frame.orphanResidualBandEnergy.size())
                        e = frame.orphanResidualBandEnergy[(std::size_t) band];
                    const float db = (float) (10.0 * std::log10(juce::jmax(1e-20, e)));
                    const float t = juce::jlimit(0.0f, 1.0f, (db - (dbMin - 20.0f)) / (dbMax - (dbMin - 20.0f)));
                    const auto col = juce::Colour::fromHSV(0.25f + 0.45f * t, 0.7f, 0.2f + 0.7f * t, 1.0f);
                    bd.setPixelColour(x, y, col);
                }
            }
        }

        return true;
    }

    // Fall back to classic ATS_SOUND (Classic ATS mode or when modern model is unavailable).
    auto* s = analyzedModel.get();
    if (s == nullptr || s->frames <= 0)
        return false;

    juce::Image::BitmapData bd(out, juce::Image::BitmapData::writeOnly);

    if (partialsMode)
    {
        if (s->partials <= 0 || s->amp == nullptr)
            return true;

        for (int y = 0; y < height; ++y)
        {
            const int par = juce::jmin(s->partials - 1, (int) ((int64_t) y * (int64_t) s->partials / (int64_t) height));
            for (int x = 0; x < width; ++x)
            {
                const int fr = juce::jmin(s->frames - 1, (int) ((int64_t) x * (int64_t) s->frames / (int64_t) width));
                const double a = s->amp[par][fr];
                const float db = (float) (20.0 * std::log10(juce::jmax(1e-15, a)));
                const float t = juce::jlimit(0.0f, 1.0f, (db - dbMin) / (dbMax - dbMin));
                const auto col = juce::Colour::fromHSV(0.62f - 0.35f * t, 0.55f + 0.35f * t, 0.15f + 0.75f * t, 1.0f);
                bd.setPixelColour(x, y, col);
            }
        }
    }
    else
    {
        if (s->band_energy == nullptr || s->frames <= 0)
            return true;

        for (int y = 0; y < height; ++y)
        {
            const int band = juce::jmin(ATSA_CRITICAL_BANDS - 1, (int) ((int64_t) y * (int64_t) ATSA_CRITICAL_BANDS / (int64_t) height));
            for (int x = 0; x < width; ++x)
            {
                const int fr = juce::jmin(s->frames - 1, (int) ((int64_t) x * (int64_t) s->frames / (int64_t) width));
                const double e = s->band_energy[band][fr];
                const float db = (float) (10.0 * std::log10(juce::jmax(1e-20, e)));
                const float t = juce::jlimit(0.0f, 1.0f, (db - (dbMin - 20.0f)) / (dbMax - (dbMin - 20.0f)));
                const auto col = juce::Colour::fromHSV(0.25f + 0.45f * t, 0.7f, 0.2f + 0.7f * t, 1.0f);
                bd.setPixelColour(x, y, col);
            }
        }
    }

    return true;
}

bool AtsAudioProcessor::saveFullStatePreset(const juce::File& file)
{
    juce::MemoryBlock block;
    getStateInformation(block);
    return file.replaceWithData(block.getData(), block.getSize());
}

bool AtsAudioProcessor::loadFullStatePreset(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    juce::MemoryBlock block;
    {
        juce::FileInputStream fis(file);
        if (!fis.openedOk())
            return false;
        juce::MemoryOutputStream mos;
        if (!mos.writeFromInputStream(fis, fis.getTotalLength()))
            return false;
        block = mos.getMemoryBlock();
    }

    setStateInformation(block.getData(), (int) block.getSize());
    return true;
}

bool AtsAudioProcessor::exportAtsFile(const juce::File& file, juce::String& error)
{
    error.clear();
    juce::File out = file;
    if (!out.getFileName().endsWithIgnoreCase(".ats"))
        out = out.withFileExtension("ats");

    const auto parent = out.getParentDirectory();
    if (parent.exists() && !parent.isDirectory())
    {
        error = "Export path is not a directory.";
        return false;
    }
    if (!parent.exists())
    {
        if (!parent.createDirectory())
        {
            error = "Could not create export directory.";
            return false;
        }
    }

    const ats::AnalysisParams ap = readAnalysisParamsFromApvts();
    const juce::ScopedLock ml(modelLock);
    if (analyzedModel.get() == nullptr)
    {
        error = "No analyzed model to export.";
        return false;
    }

    if (ats_save(analyzedModel.get(), out.getFullPathName().toUTF8(), ap.SMR_thres, ap.type) < 0)
    {
        error = "ats_save failed.";
        return false;
    }

    return true;
}

bool AtsAudioProcessor::importAtsFile(const juce::File& file, juce::String& error)
{
    error.clear();
    if (!file.existsAsFile())
    {
        error = "File does not exist.";
        return false;
    }

    cancelJobs();
    joinWorkers();

    auto* raw = (ATS_SOUND*) std::calloc(1, sizeof(ATS_SOUND));
    int loadedType = 1;
    if (ats_load(raw, file.getFullPathName().toUTF8(), &loadedType) < 0)
    {
        std::free(raw);
        error = "ats_load failed (not a valid ATS file for this build).";
        return false;
    }

    {
        const juce::ScopedLock ml(modelLock);
        analyzedModel.reset(raw);
        latestTrackLifecycle.clear();
        latestModernModel = {};
        previousModernModel = {};
        latestModernModelValid = false;
        previousModernModelValid = false;
        analysisWarnings.clear();
        activePlaybackBufferIndex.store(-1, std::memory_order_release);
        previewPlaying.store(false, std::memory_order_release);
    }

    modelRevision.fetch_add(1, std::memory_order_relaxed);

    if (auto* p = apvts.getParameter("analysisType"))
        p->setValueNotifyingHost((float) (juce::jlimit(1, 4, loadedType) - 1) / 3.0f);

    {
        const juce::SpinLock::ScopedLockType sl(errorLock);
        lastError.clear();
    }

    setUiState(AtsUiState::Analyzed);
    synthParamsDirty.store(true);
    triggerAsyncUpdate();
    return true;
}

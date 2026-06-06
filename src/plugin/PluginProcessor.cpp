#include "PluginProcessor.h"

#include "PresetLibrary.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <tuple>

#include "AtsFrequencyShape.h"
#include "AtsMathExpr.h"
#include "AtsMorphRecipe.h"
#include "AtsMorphTransportCorrespondence.h"
#include "AtsSpectralEnvelope.h"
#include "AtsTransientSmear.h"
#include "PluginEditor.h"

#include "ResampleLinear.h"

#include <JuceHeader.h>

extern "C" {
#include "atsa.h"
}

// Bit-identical cross-render cache for the per-frame Bark (True-Envelope) builds that feed the
// Hybrid envelope-transfer and EnvelopeMorph lanes. The envelope at (sound, frame0, frame1, frac)
// is a pure function of those inputs, so when a replay reruns the morph after a change that does
// not touch the source partials (morph amount, timeline edits, env-transfer amount), the same
// envelopes are requested again and served from here instead of rebuilt with the iterative DCT.
// Keyed by source pointer (a rebuilt source gets a fresh pointer, evicting the stale slot) plus a
// coarse source revision as insurance against any in-place mutation; frac is compared bit-for-bit,
// so a hit is identical to a fresh build.
struct MorphEnvelopeCache
{
    juce::CriticalSection lock;
    uint64_t revision = ~0ull;
    const ATS_SOUND* keyA = nullptr;
    const ATS_SOUND* keyB = nullptr;
    std::map<std::tuple<int, int, uint64_t>, ats::BarkSpectralEnvelope> mapA;
    std::map<std::tuple<int, int, uint64_t>, ats::BarkSpectralEnvelope> mapB;
};

namespace
{
// TEMP diagnostic: appends to ~/ats_play_debug.log so we can capture the
// playback/render path in the Release standalone (DBG is compiled out there).
// Remove once the silence root cause is fixed.
static void atsPlayLog(const juce::String& msg)
{
    auto f = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                 .getChildFile("ats_play_debug.log");
    f.appendText(juce::Time::getCurrentTime().toString(true, true, true, true) + "  " + msg + "\n");
}

constexpr int kStateVersion = 16;
constexpr int kMinReadableStateVersion = 2;
constexpr float kDefaultMorphPerformanceMacro = 0.5f;
constexpr float kDefaultMorphSmoothingMs = 40.0f;
constexpr int kDefaultMorphGridType = 0;
constexpr int kDefaultMorphCorrespondenceStrategy = 3; // optimal transport
constexpr int kDefaultMorphQuality = 1; // 0=Max, 1=Default, 2=Economy
constexpr int kDefaultMorphUnmatchedSourcePolicy = 0;
constexpr int kDefaultMorphUnmatchedTargetPolicy = 1;
constexpr int kDefaultMorphFrequencyDomain = 0;
constexpr int kDefaultMorphAmplitudeDomain = 0;
constexpr int kDefaultMorphPhaseStrategy = 0;
constexpr int kDefaultMorphTransientPolicy = 0;
constexpr int kDefaultClassicPreviewMode = 0;
constexpr int kSourceAPlaybackBufferIndex = 0;
constexpr int kSourceBPlaybackBufferIndex = 1;
constexpr int kMorphPlaybackBufferIndex = 2;
constexpr const char* kMorphTimelineUnknownField = "ui.morph_timeline_points";

// Smearnov parameters that change *what onsets get detected*. Editing any of
// these drops a pinned auto-detection so the next Apply re-detects (manual
// overlay edits are kept — see invalidateAutoDetectedOnsetOverrides).
constexpr const char* kSmearnovDetectionParamIds[] = {
    ats_param::kSmearnovWeightDetFlux,
    ats_param::kSmearnovWeightResFlux,
    ats_param::kSmearnovWeightHfcFlux,
    ats_param::kSmearnovWeightBirth,
    ats_param::kSmearnovWeightPhaseDev,
    ats_param::kSmearnovUsePhaseDeviation,
    ats_param::kSmearnovThresholdLambda,
    ats_param::kSmearnovThresholdAutoScale,
    ats_param::kSmearnovPeakWindowFrames,
    ats_param::kSmearnovMinGapFrames,
    ats_param::kSmearnovPreMarginMs,
    ats_param::kSmearnovMaxAttackFrames,
    ats_param::kSmearnovUseRpcaFallback,
};

struct ClassicLayerRender
{
    juce::AudioBuffer<float> partials;
    juce::AudioBuffer<float> residual;
    juce::AudioBuffer<float> combined;
    int modelSampleRate = 44100;
    bool hasResidual = false;
};

// Sample-wise (wet - dry) into a fresh mono buffer, zero-padding the shorter one.
// Used to isolate the smear contribution (smeared render minus the dry render).
juce::AudioBuffer<float> subtractMonoBuffers(const juce::AudioBuffer<float>& wet,
                                             const juce::AudioBuffer<float>& dry)
{
    const int wn = wet.getNumSamples();
    const int dn = dry.getNumSamples();
    const int n = juce::jmax(wn, dn);
    juce::AudioBuffer<float> out;
    if (n <= 0)
        return out;
    out.setSize(1, n);
    float* const d = out.getWritePointer(0);
    const float* const wp = wn > 0 ? wet.getReadPointer(0) : nullptr;
    const float* const dp = dn > 0 ? dry.getReadPointer(0) : nullptr;
    for (int i = 0; i < n; ++i)
        d[i] = (wp != nullptr && i < wn ? wp[i] : 0.0f) - (dp != nullptr && i < dn ? dp[i] : 0.0f);
    return out;
}

ClassicLayerRender subtractClassicLayers(const ClassicLayerRender& wet, const ClassicLayerRender& dry)
{
    ClassicLayerRender out;
    out.modelSampleRate = wet.modelSampleRate > 0 ? wet.modelSampleRate : dry.modelSampleRate;
    out.hasResidual = wet.hasResidual || dry.hasResidual;
    out.partials = subtractMonoBuffers(wet.partials, dry.partials);
    out.residual = subtractMonoBuffers(wet.residual, dry.residual);
    out.combined = subtractMonoBuffers(wet.combined, dry.combined);
    return out;
}

void freeResidual(double*** res)
{
    if (res == nullptr || *res == nullptr)
        return;
    if ((*res)[0] != nullptr)
        std::free((*res)[0]);
    if ((*res)[1] != nullptr)
        std::free((*res)[1]);
    std::free(*res);
    *res = nullptr;
}

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
    float* const dst = out.getWritePointer(0);
    for (int i = 0; i < nOut; ++i)
        dst[i] = static_cast<float>(rawOut[i]);

    std::free(rawOut);
    return out;
}

bool hasResidualBandEnergy(const ATS_SOUND* sound)
{
    return sound != nullptr && sound->band_energy != nullptr;
}

float softLimitPreviewSample(float sample)
{
    if (!std::isfinite(sample))
        return 0.0f;

    constexpr float threshold = 0.98f;
    const float magnitude = std::abs(sample);
    if (magnitude <= threshold)
        return sample;

    const float limited = threshold + (1.0f - threshold)
                                      * std::tanh((magnitude - threshold) / (1.0f - threshold));
    return std::copysign(limited, sample);
}

AtsResidualDiagnostics makeResidualDiagnostics(const ATS_SOUND* sound)
{
    AtsResidualDiagnostics diagnostics;
    if (!hasResidualBandEnergy(sound) || sound->frames <= 0)
        return diagnostics;

    diagnostics.hasResidual = true;
    for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
    {
        bool bandActive = false;
        if (sound->band_energy[band] == nullptr)
            continue;

        for (int frame = 0; frame < sound->frames; ++frame)
        {
            const double energy = sound->band_energy[band][frame];
            if (!std::isfinite(energy) || energy <= 0.0)
                continue;

            diagnostics.totalEnergy += energy;
            ++diagnostics.activeBandFrameCount;
            bandActive = true;
            if (energy > diagnostics.peakBandEnergy)
            {
                diagnostics.peakBandEnergy = energy;
                diagnostics.peakBandIndex = band;
            }
        }

        if (bandActive)
            ++diagnostics.activeBandCount;
    }

    return diagnostics;
}

// Correspondence-strategy dispatch shared by rebuildMorphCorrespondenceIfReady (which writes
// member state) and the live-smear timeline path (which builds into a local recipe copy).
// Maps the morph-quality choice (0=Max, 1=Default, 2=Economy) to the
// optimal-transport minMassFraction: higher fraction drops more negligible
// split edges, trading a little morph accuracy for fewer synth partials. Values
// from the /tmp minMassFraction sweep on real Splice material: the knee is ~1e-5,
// 1e-3 is inaudible (<-50 dB dropped), 1e-2 halves edge count and stays <-40 dB.
double morphQualityToMinMassFraction(int quality)
{
    switch (quality)
    {
        case 0: return 1e-5; // Max quality: keep all but the denormal tail.
        case 2: return 1e-2; // Economy: fewest partials, still inaudible loss.
        case 1:
        default: return 1e-3; // Default: safe, slightly fewer partials.
    }
}

// strategy: 1 = greedy nearest, 2 = assignment matrix, 3 = optimal transport,
// otherwise frequency-rank. minMassFraction tunes the OT path only.
std::vector<ats::MorphCorrespondenceRecord> buildMorphCorrespondence(
    const std::vector<ats::MorphBandAssignment>& asgA,
    const std::vector<ats::MorphBandAssignment>& asgB,
    int strategy,
    double minMassFraction = 1e-3)
{
    if (asgA.empty() || asgB.empty())
        return {};

    if (strategy == 1)
        return ats::buildGreedyNearestCorrespondence(asgA, asgB);

    if (strategy == 3)
        return ats::buildOptimalTransportRecords(asgA, asgB, { minMassFraction });

    if (strategy == 2)
    {
        ats::MorphAssignmentMatrixOptions options;
        options.advancedStrategyEnabled = true;
        options.amplitudeRankWeight = 0.25;
        return ats::buildAssignmentMatrixCorrespondence(asgA, asgB, options).records;
    }

    // Bias the default frequency-rank pairing toward amplitude so the loud, salient partials of
    // A and B stay matched (one gliding partial) rather than split across mismatched ranks.
    return ats::buildFrequencyRankCorrespondence(asgA, asgB, 0.25);
}

std::vector<ats::MorphResidualNoiseBand> makeMorphResidualNoiseBands(const ATS_SOUND* sound)
{
    std::vector<ats::MorphResidualNoiseBand> bands;
    if (!hasResidualBandEnergy(sound) || sound->frames <= 0)
        return bands;

    bands.reserve(ATSA_CRITICAL_BANDS);
    for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
    {
        if (sound->band_energy[band] == nullptr)
            continue;

        double totalEnergy = 0.0;
        int validFrameCount = 0;
        for (int frame = 0; frame < sound->frames; ++frame)
        {
            const double energy = sound->band_energy[band][frame];
            if (!std::isfinite(energy) || energy <= 0.0)
                continue;

            totalEnergy += energy;
            ++validFrameCount;
        }

        if (validFrameCount <= 0 || totalEnergy <= 0.0)
            continue;

        // Normalize by window_size * ATSA_NOISE_VARIANCE so orphanResidualPower is in
        // ENG_RMS^2 units, consistent with how do_synthesis converts band_energy to amplitude.
        // renderMorphSnapshotPlaybackBuffer uses powerToAmplitude(orphanResidualPower) which
        // then gives the correct RMS amplitude for the carrier synthesis.
        const double norm = 1.0 / (juce::jmax(1, sound->window_size) * (double) ATSA_NOISE_VARIANCE);
        ats::MorphResidualNoiseBand residualBand;
        residualBand.bandIndex = band;
        residualBand.orphanResidualPower = (totalEnergy / (double) validFrameCount) * norm;
        bands.push_back(residualBand);
    }

    return bands;
}

// Detect onset markers from an analyzed ATS model by tracking the rising edges of its total
// deterministic energy (an energy-flux onset detector: a sharp positive jump in summed partial
// amplitude marks an attack). Used to time-align A and B's principal attacks before morphing so
// the midpoint hears a single onset rather than two at distinct instants. Markers carry their
// normalized flux as strength so the morph can pick the earliest salient attack.
std::vector<ats::MorphTransientMarker> detectMorphTransients(const ATS_SOUND* sound,
                                                             double hopSeconds)
{
    std::vector<ats::MorphTransientMarker> markers;
    if (sound == nullptr || sound->frames <= 1 || sound->amp == nullptr || sound->partials <= 0)
        return markers;

    const int frames = sound->frames;
    const int partials = sound->partials;

    std::vector<double> energy((size_t) frames, 0.0);
    for (int f = 0; f < frames; ++f)
    {
        double e = 0.0;
        for (int p = 0; p < partials; ++p)
        {
            if (sound->amp[p] == nullptr)
                continue;
            const double a = sound->amp[p][f];
            if (std::isfinite(a) && a > 0.0)
                e += a;
        }
        energy[(size_t) f] = e;
    }

    std::vector<double> flux((size_t) frames, 0.0);
    double maxFlux = 0.0;
    for (int f = 1; f < frames; ++f)
    {
        const double d = energy[(size_t) f] - energy[(size_t) (f - 1)];
        flux[(size_t) f] = d > 0.0 ? d : 0.0;
        maxFlux = std::max(maxFlux, flux[(size_t) f]);
    }
    if (maxFlux <= 0.0)
        return markers;

    const double hop = hopSeconds > 0.0 ? hopSeconds : 0.0;
    const double threshold = 0.25 * maxFlux; // only salient rising edges qualify as onsets
    for (int f = 1; f < frames; ++f)
    {
        const bool isLocalPeak = flux[(size_t) f] >= threshold
                                 && flux[(size_t) f] >= flux[(size_t) (f - 1)]
                                 && (f + 1 >= frames || flux[(size_t) f] >= flux[(size_t) (f + 1)]);
        if (!isLocalPeak)
            continue;
        ats::MorphTransientMarker marker;
        marker.timeSeconds = hop * (double) f;
        marker.strength = flux[(size_t) f] / maxFlux;
        marker.frameIndex = f;
        markers.push_back(marker);
    }
    return markers;
}

ClassicLayerRender renderClassicLayers(const ATS_SOUND* sound, const ats::SynthParams& core)
{
    ClassicLayerRender render;
    if (sound == nullptr)
        return render;

    ats::AtsSoundPtr shapedSound;
    const ATS_SOUND* renderSound = sound;
    if (ats::isFrequencyShapeActive(ats::resolveFrequencyShapeSettingsForSound(sound, core.frequencyShape)))
    {
        if (auto* copy = copy_sound(const_cast<ATS_SOUND*>(sound)))
        {
            shapedSound.reset(copy);
            ats::applyFrequencyShapeToSound(shapedSound.get(), core.frequencyShape);
            renderSound = shapedSound.get();
        }
    }

    // do_synthesis generates output at sparams->sr (= core.sampleRate, the host rate),
    // independent of the model's analysis srate — partials carry absolute frequencies.
    // Report that synthesis rate here so the downstream resample-to-host is a no-op;
    // using renderSound->srate instead double-converts and stretches by hostSr/modelSr
    // (audibly lengthened + pitched down when host != analysis rate).
    render.modelSampleRate = core.sampleRate > 0.0 ? juce::roundToInt(core.sampleRate)
                                                   : renderSound->srate;
    render.hasResidual = hasResidualBandEnergy(renderSound);

    ats::SynthParams spPartials = core;
    spPartials.partialsGain = 1.0;
    spPartials.residualGain = 0.0;

    if (render.hasResidual)
    {
        // All three passes read the same const model and write independent
        // output buffers — safe to run concurrently.
        ats::SynthParams spResidual = core;
        spResidual.partialsGain = 0.0;
        spResidual.residualGain = 1.0;

        ats::SynthParams spCombined = core;
        spCombined.partialsGain = 1.0;
        spCombined.residualGain = 1.0;

        auto futurePartials = std::async(std::launch::async, [&] { return runDoSynthesis(renderSound, spPartials); });
        auto futureResidual = std::async(std::launch::async, [&] { return runDoSynthesis(renderSound, spResidual); });
        render.combined  = runDoSynthesis(renderSound, spCombined);
        render.partials  = futurePartials.get();
        render.residual  = futureResidual.get();
    }
    else
    {
        render.partials = runDoSynthesis(renderSound, spPartials);
        render.combined.makeCopyOf(render.partials);
    }

    return render;
}

juce::AudioBuffer<float> makePlaybackBufferFromClassicLayers(const ClassicLayerRender& render,
                                                             double renderSampleRate)
{
    juce::AudioBuffer<float> playback;
    const int nP = render.partials.getNumSamples();
    const int nR = render.residual.getNumSamples();
    const int nC = render.combined.getNumSamples();
    if (nP <= 0 && nR <= 0 && nC <= 0)
        return playback;

    const double sourceSampleRate = render.modelSampleRate > 0 ? (double) render.modelSampleRate : 44100.0;
    const double targetSampleRate = renderSampleRate > 0.0 ? renderSampleRate : sourceSampleRate;

    std::vector<float> resampledP;
    std::vector<float> resampledR;
    std::vector<float> resampledC;
    if (nP > 0)
        ats::resampleLinearMono(render.partials.getReadPointer(0), nP, sourceSampleRate, resampledP, targetSampleRate);
    if (nR > 0)
        ats::resampleLinearMono(render.residual.getReadPointer(0), nR, sourceSampleRate, resampledR, targetSampleRate);
    if (nC > 0)
        ats::resampleLinearMono(render.combined.getReadPointer(0), nC, sourceSampleRate, resampledC, targetSampleRate);

    const size_t outN = std::max(resampledC.size(), std::max(resampledP.size(), resampledR.size()));
    if (outN == 0)
        return playback;

    playback.setSize(resampledC.empty() ? 2 : 3, (int) outN);
    const bool hasCombined = playback.getNumChannels() > 2;
    float* const dstP = playback.getWritePointer(0);
    float* const dstR = playback.getWritePointer(1);
    float* const dstC = hasCombined ? playback.getWritePointer(2) : nullptr;
    const int nResP = (int) resampledP.size();
    const int nResR = (int) resampledR.size();
    const int nResC = (int) resampledC.size();
    for (int i = 0; i < (int) outN; ++i)
    {
        const float p = i < nResP ? resampledP[(size_t) i] : 0.0f;
        const float r = i < nResR ? resampledR[(size_t) i] : 0.0f;
        dstP[i] = p;
        dstR[i] = r;
        if (dstC != nullptr)
            dstC[i] = i < nResC ? resampledC[(size_t) i] : p + r;
    }

    return playback;
}

juce::AudioBuffer<float> renderClassicSourcePlaybackBuffer(const ATS_SOUND* sound,
                                                           const ats::SynthParams& core,
                                                           double renderSampleRate)
{
    return makePlaybackBufferFromClassicLayers(renderClassicLayers(sound, core), renderSampleRate);
}

const ATS_SOUND* makeFrequencyShapedRenderSound(const ATS_SOUND* sound,
                                                const ats::FrequencyShapeSettings& frequencyShape,
                                                ats::AtsSoundPtr& shapedSound)
{
    shapedSound.reset();
    if (sound == nullptr)
        return nullptr;

    if (!ats::isFrequencyShapeActive(ats::resolveFrequencyShapeSettingsForSound(sound, frequencyShape)))
        return sound;

    if (auto* copy = copy_sound(const_cast<ATS_SOUND*>(sound)))
    {
        shapedSound.reset(copy);
        ats::applyFrequencyShapeToSound(shapedSound.get(), frequencyShape);
        return shapedSound.get();
    }

    return sound;
}

ats::MorphSourceMetadata makeMorphSourceMetadata(const ATS_SOUND* sound,
                                                 int slotIndex,
                                                 const juce::String& modelName,
                                                 int revision)
{
    ats::MorphSourceMetadata metadata;
    if (sound == nullptr)
        return metadata;

    metadata.id = slotIndex == 0 ? "source_a" : "source_b";
    metadata.name = (modelName.isNotEmpty() ? modelName
                                            : juce::String(slotIndex == 0 ? "Source A" : "Source B"))
                        .toStdString();
    metadata.sampleRate = sound->srate;
    metadata.durationSeconds = sound->dur;
    metadata.frameCount = sound->frames;
    metadata.partialCount = sound->partials;
    metadata.frameHopSeconds = sound->frames > 1 && sound->dur > 0.0
                                   ? sound->dur / (double) (sound->frames - 1)
                                   : 0.0;
    metadata.channelCount = 1;
    metadata.hasPhase = sound->pha != nullptr;
    metadata.hasResidual = sound->band_energy != nullptr;

    double lowest = std::numeric_limits<double>::max();
    double highest = 0.0;
    bool foundFrequency = false;
    if (sound->frq != nullptr)
    {
        for (int p = 0; p < sound->partials; ++p)
        {
            if (sound->frq[p] == nullptr)
                continue;

            for (int f = 0; f < sound->frames; ++f)
            {
                const double hz = sound->frq[p][f];
                const bool active = sound->amp == nullptr || sound->amp[p] == nullptr || sound->amp[p][f] > 0.0;
                if (active && std::isfinite(hz) && hz > 0.0)
                {
                    lowest = std::min(lowest, hz);
                    highest = std::max(highest, hz);
                    foundFrequency = true;
                }
            }
        }
    }

    metadata.lowestFrequencyHz = foundFrequency ? lowest : 0.0;
    metadata.highestFrequencyHz = foundFrequency ? highest : juce::jmax(0.0, sound->srate * 0.5);
    metadata.hash = ("rev:" + juce::String(revision)
                     + ";sr:" + juce::String(metadata.sampleRate, 0)
                     + ";dur:" + juce::String(metadata.durationSeconds, 3)
                     + ";frames:" + juce::String(metadata.frameCount)
                     + ";partials:" + juce::String(metadata.partialCount))
                        .toStdString();
    return metadata;
}

std::vector<ats::MorphBandAssignment> makeMorphAssignmentsFromSound(const ATS_SOUND* sound,
                                                                    const ats::MorphFrequencyGrid& grid)
{
    std::vector<ats::MorphPartialFrame> frames;
    if (sound == nullptr || sound->partials <= 0 || sound->frames <= 0 || sound->frq == nullptr)
        return {};

    frames.reserve((size_t) sound->partials);
    for (int p = 0; p < sound->partials; ++p)
    {
        if (sound->frq[p] == nullptr)
            continue;

        double weightedFrequency = 0.0;
        double totalAmplitude = 0.0;
        double peakAmplitude = 0.0;
        int peakFrame = -1;
        int activeFrameCount = 0;
        for (int f = 0; f < sound->frames; ++f)
        {
            const double hz = sound->frq[p][f];
            const double amp = sound->amp != nullptr && sound->amp[p] != nullptr ? sound->amp[p][f] : 1.0;
            if (!std::isfinite(hz) || hz <= 0.0 || !std::isfinite(amp) || amp <= 0.0)
                continue;

            weightedFrequency += hz * amp;
            totalAmplitude += amp;
            if (amp > peakAmplitude)
            {
                peakAmplitude = amp;
                peakFrame = f;
            }
            ++activeFrameCount;
        }

        if (activeFrameCount == 0 || totalAmplitude <= 0.0)
            continue;

        ats::MorphPartialFrame frame;
        frame.partialIndex = p;
        frame.frameIndex = peakFrame;
        frame.frequencyHz = weightedFrequency / totalAmplitude;
        frame.amplitude = totalAmplitude / (double) activeFrameCount;
        frame.active = true;
        frames.push_back(frame);
    }

    return ats::assignPartialFramesToFrequencyBands(grid, frames);
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

void writeFrequencyShape(juce::MemoryOutputStream& mos, const ats::FrequencyShapeSettings& shape)
{
    mos.writeInt((int) shape.mode);
    mos.writeDouble(shape.amount);
    mos.writeDouble(shape.pivotHz);
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

bool readFrequencyShape(juce::MemoryInputStream& mis, ats::FrequencyShapeSettings& shape)
{
    constexpr juce::int64 kShapeBytes = sizeof(int) + sizeof(double) * 2;
    if (mis.getNumBytesRemaining() < kShapeBytes)
        return false;

    shape.mode = (ats::FrequencyShapeMode) juce::jlimit(0, 3, mis.readInt());
    shape.amount = mis.readDouble();
    shape.pivotHz = mis.readDouble();
    shape.resolvedPivotHz = 0.0;

    if (!std::isfinite(shape.amount) || shape.amount <= 0.0)
        shape.amount = 1.0;
    if (!std::isfinite(shape.pivotHz) || shape.pivotHz < 0.0)
        shape.pivotHz = 0.0;

    return true;
}

void writeStringChunk(juce::MemoryOutputStream& mos, const std::string& text)
{
    mos.writeInt64((juce::int64) text.size());
    if (!text.empty())
        mos.write(text.data(), text.size());
}

bool readStringChunk(juce::MemoryInputStream& mis, std::string& text)
{
    if (mis.getNumBytesRemaining() < (juce::int64) sizeof(juce::int64))
        return false;

    const juce::int64 textSize = mis.readInt64();
    if (textSize < 0 || mis.getNumBytesRemaining() < textSize)
        return false;

    text.clear();
    if (textSize > 0)
    {
        text.resize((size_t) textSize);
        mis.read(text.data(), (size_t) textSize);
    }
    return true;
}

void writeResidualState(juce::MemoryOutputStream& mos,
                        const AtsResidualDiagnostics& diagnostics,
                        const std::vector<ats::MorphResidualNoiseBand>& bands)
{
    mos.writeByte(diagnostics.hasResidual ? (juce::uint8) 1 : (juce::uint8) 0);
    mos.writeInt(diagnostics.activeBandCount);
    mos.writeInt(diagnostics.activeBandFrameCount);
    mos.writeDouble(diagnostics.totalEnergy);
    mos.writeInt(diagnostics.peakBandIndex);
    mos.writeDouble(diagnostics.peakBandEnergy);

    mos.writeInt((int) bands.size());
    for (const auto& band : bands)
    {
        mos.writeInt(band.bandIndex);
        mos.writeDouble(band.partialNoisePower);
        mos.writeDouble(band.orphanResidualPower);
    }
}

bool readResidualState(juce::MemoryInputStream& mis,
                       AtsResidualDiagnostics& diagnostics,
                       std::vector<ats::MorphResidualNoiseBand>& bands)
{
    constexpr int kMaxPersistedResidualBands = 256;
    constexpr juce::int64 kDiagnosticBytes = 1 + sizeof(int) * 3 + sizeof(double) * 2;

    if (mis.getNumBytesRemaining() < kDiagnosticBytes)
        return false;

    diagnostics = {};
    diagnostics.hasResidual = mis.readByte() != 0;
    diagnostics.activeBandCount = mis.readInt();
    diagnostics.activeBandFrameCount = mis.readInt();
    diagnostics.totalEnergy = mis.readDouble();
    diagnostics.peakBandIndex = mis.readInt();
    diagnostics.peakBandEnergy = mis.readDouble();

    if (mis.getNumBytesRemaining() < (juce::int64) sizeof(int))
        return false;

    const int bandCount = mis.readInt();
    if (bandCount < 0 || bandCount > kMaxPersistedResidualBands)
        return false;
    if (mis.getNumBytesRemaining() < (juce::int64) bandCount * (juce::int64) (sizeof(int) + sizeof(double) * 2))
        return false;

    bands.clear();
    bands.reserve((size_t) bandCount);
    for (int i = 0; i < bandCount; ++i)
    {
        ats::MorphResidualNoiseBand band;
        band.bandIndex = mis.readInt();
        band.partialNoisePower = mis.readDouble();
        band.orphanResidualPower = mis.readDouble();
        if (band.bandIndex >= 0 && band.bandIndex < ATSA_CRITICAL_BANDS
            && std::isfinite(band.partialNoisePower)
            && std::isfinite(band.orphanResidualPower))
        {
            bands.push_back(band);
        }
    }

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
           && (std::abs(a.duration - b.duration) < 1e-8)
           && ats::sameFrequencyShapeSettings(a.frequencyShape, b.frequencyShape);
}

bool isMorphSnapshotParameter(const juce::String& parameterID)
{
    if (parameterID == ats_param::kMorphAmount
        || parameterID == ats_param::kMorphPerformanceMacro
        || parameterID == ats_param::kMorphSmoothingMs
        || parameterID == ats_param::kMorphGridType
        || parameterID == ats_param::kMorphCorrespondenceStrategy
        || parameterID == ats_param::kMorphQuality
        || parameterID == ats_param::kMorphUnmatchedSourcePolicy
        || parameterID == ats_param::kMorphUnmatchedTargetPolicy
        || parameterID == ats_param::kMorphDiscardOptIn
        || parameterID == ats_param::kMorphFrequencyInterpolationDomain
        || parameterID == ats_param::kMorphAmplitudeInterpolationDomain
        || parameterID == ats_param::kMorphPhaseStrategy
        || parameterID == ats_param::kMorphTransientPolicy)
    {
        return true;
    }

    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        if (parameterID == ats_param::kMorphLaneAmountIds[i]
            || parameterID == ats_param::kMorphLaneLinkedIds[i])
        {
            return true;
        }
    }
    return false;
}

bool isFrequencyShapeParameter(const juce::String& parameterID)
{
    return parameterID == ats_param::kFreqShapeMode
           || parameterID == ats_param::kFreqShapeAmount
           || parameterID == ats_param::kFreqShapePivotHz;
}

float getRawParamValue(const juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& parameterID,
                       float fallback)
{
    if (auto* value = apvts.getRawParameterValue(parameterID))
        return value->load();
    return fallback;
}

void setParameterValue(juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& parameterID,
                       float plainValue)
{
    if (auto* p = apvts.getParameter(parameterID))
        p->setValueNotifyingHost(apvts.getParameterRange(parameterID).convertTo0to1(plainValue));
}

void setParameterBool(juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& parameterID,
                      bool value)
{
    if (auto* p = apvts.getParameter(parameterID))
        p->setValueNotifyingHost(value ? 1.0f : 0.0f);
}

AtsMorphSnapshotSlotInfo captureMorphSnapshotSlot(
    const juce::AudioProcessorValueTreeState& apvts,
    const juce::String& name)
{
    AtsMorphSnapshotSlotInfo slot;
    slot.name = name;
    slot.hasSnapshot = true;
    slot.morphAmount = getRawParamValue(apvts, ats_param::kMorphAmount, 0.0f);
    slot.performanceMacro = getRawParamValue(apvts,
                                             ats_param::kMorphPerformanceMacro,
                                             kDefaultMorphPerformanceMacro);
    slot.smoothingMs = getRawParamValue(apvts, ats_param::kMorphSmoothingMs, kDefaultMorphSmoothingMs);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        slot.laneAmounts[i] = getRawParamValue(apvts, ats_param::kMorphLaneAmountIds[i], 0.0f);
        slot.laneLinked[i] = getRawParamValue(apvts, ats_param::kMorphLaneLinkedIds[i], 1.0f) > 0.5f;
    }
    return slot;
}

int getRawChoiceValue(const juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& parameterID,
                      int fallback)
{
    return juce::jmax(0, juce::roundToInt(getRawParamValue(apvts, parameterID, (float) fallback)));
}

void setChoiceParameter(juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& parameterID,
                        int index)
{
    setParameterValue(apvts, parameterID, (float) juce::jmax(0, index));
}

ats::MorphUnmatchedPartialPolicy unmatchedPolicyFromChoice(int index)
{
    switch (index)
    {
        case 1: return ats::MorphUnmatchedPartialPolicy::FadeIn;
        case 2: return ats::MorphUnmatchedPartialPolicy::Hold;
        case 3: return ats::MorphUnmatchedPartialPolicy::MergeIntoResidual;
        case 4: return ats::MorphUnmatchedPartialPolicy::Discard;
        case 0:
        default: return ats::MorphUnmatchedPartialPolicy::FadeOut;
    }
}

int unmatchedPolicyToChoice(ats::MorphUnmatchedPartialPolicy policy)
{
    switch (policy)
    {
        case ats::MorphUnmatchedPartialPolicy::FadeIn: return 1;
        case ats::MorphUnmatchedPartialPolicy::Hold: return 2;
        case ats::MorphUnmatchedPartialPolicy::MergeIntoResidual: return 3;
        case ats::MorphUnmatchedPartialPolicy::Discard: return 4;
        case ats::MorphUnmatchedPartialPolicy::FadeOut:
        default: return 0;
    }
}

ats::MorphPhaseStrategy phaseStrategyFromChoice(int index)
{
    switch (index)
    {
        case 1: return ats::MorphPhaseStrategy::SourceADonor;
        case 2: return ats::MorphPhaseStrategy::SourceBDonor;
        case 3: return ats::MorphPhaseStrategy::RelativeClusterPreservation;
        case 4: return ats::MorphPhaseStrategy::DirectBlend;
        case 0:
        default: return ats::MorphPhaseStrategy::OscillatorContinuation;
    }
}

int phaseStrategyToChoice(ats::MorphPhaseStrategy strategy)
{
    switch (strategy)
    {
        case ats::MorphPhaseStrategy::SourceADonor: return 1;
        case ats::MorphPhaseStrategy::SourceBDonor: return 2;
        case ats::MorphPhaseStrategy::RelativeClusterPreservation: return 3;
        case ats::MorphPhaseStrategy::DirectBlend: return 4;
        case ats::MorphPhaseStrategy::OscillatorContinuation:
        default: return 0;
    }
}

ats::MorphTransientPolicy transientPolicyFromChoice(int index)
{
    switch (index)
    {
        case 1: return ats::MorphTransientPolicy::SourceB;
        case 2: return ats::MorphTransientPolicy::Crossfade;
        case 3: return ats::MorphTransientPolicy::SustainedOnlyAfterAttack;
        case 0:
        default: return ats::MorphTransientPolicy::SourceA;
    }
}

int transientPolicyToChoice(ats::MorphTransientPolicy policy)
{
    switch (policy)
    {
        case ats::MorphTransientPolicy::SourceB: return 1;
        case ats::MorphTransientPolicy::Crossfade: return 2;
        case ats::MorphTransientPolicy::SustainedOnlyAfterAttack: return 3;
        case ats::MorphTransientPolicy::SourceA:
        default: return 0;
    }
}

void upsertRecipeUnknownField(ats::MorphRecipe& recipe, std::string key, std::string value)
{
    for (auto& field : recipe.unknownFields)
    {
        if (field.key == key)
        {
            field.value = value;
            return;
        }
    }
    recipe.unknownFields.push_back({std::move(key), std::move(value)});
}

std::string getRecipeUnknownField(const ats::MorphRecipe& recipe, const std::string& key)
{
    for (const auto& field : recipe.unknownFields)
    {
        if (field.key == key)
            return field.value;
    }
    return {};
}

std::vector<AtsMorphTimelinePoint> defaultMorphTimelinePoints()
{
    return { { 0.0, 0.0 }, { 1.0, 1.0 } };
}

std::vector<AtsMorphTimelinePoint> sanitizeMorphTimelinePoints(std::vector<AtsMorphTimelinePoint> points)
{
    constexpr size_t kMaxTimelinePoints = 32;
    constexpr double kDuplicateTimeTolerance = 1.0e-4;

    if (points.empty())
        points = defaultMorphTimelinePoints();

    for (auto& point : points)
    {
        point.timeNorm = juce::jlimit(0.0, 1.0, point.timeNorm);
        point.morphAmount = juce::jlimit(0.0, 1.0, point.morphAmount);
    }

    auto pointSortLess = [](const auto& a, const auto& b) {
        if (a.timeNorm != b.timeNorm)
            return a.timeNorm < b.timeNorm;
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    };
    std::sort(points.begin(), points.end(), pointSortLess);

    std::vector<AtsMorphTimelinePoint> unique;
    unique.reserve(std::min(points.size(), kMaxTimelinePoints));
    for (const auto& point : points)
    {
        bool replaced = false;
        for (auto& existing : unique)
        {
            if (existing.kind == point.kind
                && std::abs(point.timeNorm - existing.timeNorm) <= kDuplicateTimeTolerance)
            {
                existing = point;
                replaced = true;
                break;
            }
        }

        if (!replaced)
            unique.push_back(point);

        if (unique.size() >= kMaxTimelinePoints)
            break;
    }

    if (unique.empty())
        unique = defaultMorphTimelinePoints();

    bool hasMorphPoints = false;
    bool hasMorphStart = false;
    bool hasMorphEnd = false;
    for (auto& point : unique)
    {
        if (point.kind != MorphTimelinePointKind::Morph)
            continue;

        hasMorphPoints = true;
        if (point.timeNorm <= kDuplicateTimeTolerance)
        {
            point.timeNorm = 0.0;
            hasMorphStart = true;
        }
        if (point.timeNorm >= 1.0 - kDuplicateTimeTolerance)
        {
            point.timeNorm = 1.0;
            hasMorphEnd = true;
        }
    }

    if (!hasMorphPoints)
    {
        unique.push_back({ 0.0, 0.0, MorphTimelinePointKind::Morph });
        unique.push_back({ 1.0, 0.0, MorphTimelinePointKind::Morph });
    }
    else
    {
        if (!hasMorphStart)
            unique.push_back({ 0.0, 0.0, MorphTimelinePointKind::Morph });
        if (!hasMorphEnd)
            unique.push_back({ 1.0, 1.0, MorphTimelinePointKind::Morph });
    }

    std::sort(unique.begin(), unique.end(), pointSortLess);

    if (unique.size() == 1)
        unique.push_back({ 1.0, unique.front().morphAmount });

    return unique;
}

std::string serializeMorphTimelinePoints(const std::vector<AtsMorphTimelinePoint>& points)
{
    juce::String text;
    const auto safePoints = sanitizeMorphTimelinePoints(points);
    for (size_t i = 0; i < safePoints.size(); ++i)
    {
        if (i > 0)
            text << ";";
        text << juce::String(safePoints[i].timeNorm, 6)
             << "," << juce::String(safePoints[i].morphAmount, 6)
             << "," << static_cast<int>(safePoints[i].kind)
             << "," << static_cast<int>(safePoints[i].curve);
    }
    return text.toStdString();
}

std::vector<AtsMorphTimelinePoint> deserializeMorphTimelinePoints(const std::string& text)
{
    std::vector<AtsMorphTimelinePoint> points;
    auto pairs = juce::StringArray::fromTokens(juce::String(text.c_str()), ";", "");
    for (const auto& pair : pairs)
    {
        auto values = juce::StringArray::fromTokens(pair, ",:", "");
        if (values.size() < 2)
            continue;

        AtsMorphTimelinePoint point;
        point.timeNorm = values[0].getDoubleValue();
        point.morphAmount = values[1].getDoubleValue();
        if (values.size() >= 3)
        {
            const int kv = values[2].getIntValue();
            if (kv == static_cast<int>(MorphTimelinePointKind::Hybrid))
                point.kind = MorphTimelinePointKind::Hybrid;
            else if (kv == static_cast<int>(MorphTimelinePointKind::EnvelopeMorph))
                point.kind = MorphTimelinePointKind::EnvelopeMorph;
        }
        if (values.size() >= 4)
        {
            const int cv = values[3].getIntValue();
            if (cv >= 0 && cv <= static_cast<int>(ats::MorphCurveShape::EaseInOut))
                point.curve = static_cast<ats::MorphCurveShape>(cv);
        }
        if (std::isfinite(point.timeNorm) && std::isfinite(point.morphAmount))
            points.push_back(point);
    }
    return sanitizeMorphTimelinePoints(points);
}

static double applyCurveShape(double t, ats::MorphCurveShape shape) noexcept
{
    switch (shape)
    {
        case ats::MorphCurveShape::EaseIn:    return t * t;
        case ats::MorphCurveShape::EaseOut:   return t * (2.0 - t);
        case ats::MorphCurveShape::EaseInOut: return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
        default:                              return t;
    }
}

double evaluateMorphTimeline(const std::vector<AtsMorphTimelinePoint>& points, double timeNorm)
{
    const auto safePoints = sanitizeMorphTimelinePoints(points);
    const double t = juce::jlimit(0.0, 1.0, timeNorm);
    // Only Morph-kind breakpoints drive the A/B blend; Hybrid ones are excluded.
    const AtsMorphTimelinePoint* prev = nullptr;
    const AtsMorphTimelinePoint* next = nullptr;
    for (const auto& p : safePoints)
    {
        if (p.kind != MorphTimelinePointKind::Morph)
            continue;
        if (p.timeNorm <= t)
            prev = &p;
        else if (next == nullptr)
            next = &p;
    }
    if (prev == nullptr && next == nullptr) return 0.0;
    if (prev == nullptr) return next->morphAmount;
    if (next == nullptr) return prev->morphAmount;
    const double span = next->timeNorm - prev->timeNorm;
    if (span <= 1.0e-9) return next->morphAmount;
    const double shaped = applyCurveShape((t - prev->timeNorm) / span, prev->curve);
    return prev->morphAmount + shaped * (next->morphAmount - prev->morphAmount);
}

// Returns the lane amount [0,1] at timeNorm, interpolating only breakpoints of the
// requested kind. Returns 0.0 when the lane has no breakpoints.
static double evaluateLaneTimeline(const std::vector<AtsMorphTimelinePoint>& safePoints,
                                   double timeNorm,
                                   MorphTimelinePointKind laneKind)
{
    std::vector<const AtsMorphTimelinePoint*> lane;
    for (const auto& p : safePoints)
        if (p.kind == laneKind)
            lane.push_back(&p);

    if (lane.empty())
        return 0.0;

    const double t = juce::jlimit(0.0, 1.0, timeNorm);
    if (t <= lane.front()->timeNorm)
        return lane.front()->morphAmount;
    for (size_t i = 1; i < lane.size(); ++i)
    {
        if (t <= lane[i]->timeNorm)
        {
            const double span = lane[i]->timeNorm - lane[i - 1]->timeNorm;
            if (span <= 1.0e-9)
                return lane[i]->morphAmount;
            const double shaped = applyCurveShape((t - lane[i - 1]->timeNorm) / span,
                                                  lane[i - 1]->curve);
            return lane[i - 1]->morphAmount
                   + shaped * (lane[i]->morphAmount - lane[i - 1]->morphAmount);
        }
    }
    return lane.back()->morphAmount;
}

// Returns the envelope-transfer amount [0,1] at timeNorm, using only Hybrid-kind breakpoints.
static double evaluateEnvTransferTimeline(const std::vector<AtsMorphTimelinePoint>& safePoints, double timeNorm)
{
    return evaluateLaneTimeline(safePoints, timeNorm, MorphTimelinePointKind::Hybrid);
}

bool getConstantMorphEndpointSourceIndex(const std::vector<AtsMorphTimelinePoint>& points, int& sourceIndex)
{
    constexpr double kEndpointTolerance = 1.0e-6;
    const auto safePoints = sanitizeMorphTimelinePoints(points);

    // Only Morph-kind breakpoints drive the A/B blend.
    // Hybrid breakpoints must never trigger the "pure source" shortcut path;
    // they exist to apply Bark envelope transfer, not to suppress rendering.
    bool hasMorphPoints = false;
    bool hasEffectLanePoints = false;
    bool allSourceA = true;
    bool allSourceB = true;
    for (const auto& point : safePoints)
    {
        // Any non-Morph lane (Hybrid envelope transfer, EnvelopeMorph) needs the full
        // trajectory render — it must never trigger the "pure source" shortcut path.
        if (point.kind != MorphTimelinePointKind::Morph)
        {
            hasEffectLanePoints = true;
            continue;
        }

        hasMorphPoints = true;
        allSourceA = allSourceA && point.morphAmount <= kEndpointTolerance;
        allSourceB = allSourceB && point.morphAmount >= 1.0 - kEndpointTolerance;
    }

    if (hasEffectLanePoints)
    {
        sourceIndex = -1;
        return false;
    }

    // Defensive fallback for malformed/future point kinds: render the trajectory
    // path rather than taking a pure-source shortcut.
    if (!hasMorphPoints)
    {
        sourceIndex = -1;
        return false;
    }

    if (allSourceA)
    {
        sourceIndex = 0;
        return true;
    }

    if (allSourceB)
    {
        sourceIndex = 1;
        return true;
    }

    sourceIndex = -1;
    return false;
}

const char* correspondenceStrategyName(int index)
{
    switch (index)
    {
        case 1: return "greedy_nearest";
        case 2: return "assignment_matrix";
        case 3: return "optimal_transport";
        case 0:
        default: return "frequency_rank";
    }
}

juce::String formatPower(double value)
{
    if (value <= 0.0)
        return "0";
    return juce::String(value, value < 0.001 ? 6 : 3);
}

juce::String frequencyDomainName(int index)
{
    return index == 1 ? "Bark coordinate" : "Log frequency";
}

juce::String amplitudeDomainName(int index)
{
    return index == 1 ? "Decibels" : "Power";
}

juce::String correspondenceStrategyDisplayName(int index)
{
    switch (index)
    {
        case 1: return "Greedy nearest";
        case 2: return "Assignment matrix";
        case 3: return "Optimal transport";
        case 0:
        default: return "Frequency rank";
    }
}

void syncMorphControlsToRecipe(const juce::AudioProcessorValueTreeState& apvts,
                               ats::MorphRecipe& recipe)
{
    recipe.defaultMorphAmount = getRawParamValue(apvts, ats_param::kMorphAmount, 0.0f);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        recipe.lanes[i].amount = getRawParamValue(apvts, ats_param::kMorphLaneAmountIds[i], 0.0f);
        recipe.lanes[i].linkedToMainMorph =
            getRawParamValue(apvts, ats_param::kMorphLaneLinkedIds[i], 1.0f) > 0.5f;
    }

    const int gridType = getRawChoiceValue(apvts, ats_param::kMorphGridType, kDefaultMorphGridType);
    const auto currentGrid = recipe.frequencyGrid;
    if (gridType == 1)
        recipe.frequencyGrid = ats::makeMelFrequencyGrid(currentGrid.bandCount,
                                                         currentGrid.lowestFrequencyHz,
                                                         currentGrid.highestFrequencyHz);
    else
        recipe.frequencyGrid = ats::makeBarkFrequencyGrid(currentGrid.bandCount,
                                                          currentGrid.lowestFrequencyHz,
                                                          currentGrid.highestFrequencyHz);

    recipe.unmatchedPartialPolicies.sourcePolicy =
        unmatchedPolicyFromChoice(getRawChoiceValue(apvts,
                                                    ats_param::kMorphUnmatchedSourcePolicy,
                                                    kDefaultMorphUnmatchedSourcePolicy));
    recipe.unmatchedPartialPolicies.targetPolicy =
        unmatchedPolicyFromChoice(getRawChoiceValue(apvts,
                                                    ats_param::kMorphUnmatchedTargetPolicy,
                                                    kDefaultMorphUnmatchedTargetPolicy));
    recipe.unmatchedPartialPolicies.discardOptIn =
        getRawParamValue(apvts, ats_param::kMorphDiscardOptIn, 0.0f) > 0.5f;
    if (!recipe.unmatchedPartialPolicies.discardOptIn)
    {
        if (recipe.unmatchedPartialPolicies.sourcePolicy == ats::MorphUnmatchedPartialPolicy::Discard)
            recipe.unmatchedPartialPolicies.sourcePolicy = ats::MorphUnmatchedPartialPolicy::FadeOut;
        if (recipe.unmatchedPartialPolicies.targetPolicy == ats::MorphUnmatchedPartialPolicy::Discard)
            recipe.unmatchedPartialPolicies.targetPolicy = ats::MorphUnmatchedPartialPolicy::FadeIn;
    }

    recipe.phase.strategy =
        phaseStrategyFromChoice(getRawChoiceValue(apvts,
                                                  ats_param::kMorphPhaseStrategy,
                                                  kDefaultMorphPhaseStrategy));
    recipe.transient.policy =
        transientPolicyFromChoice(getRawChoiceValue(apvts,
                                                    ats_param::kMorphTransientPolicy,
                                                    kDefaultMorphTransientPolicy));

    upsertRecipeUnknownField(recipe,
                             "ui.correspondence_strategy",
                             correspondenceStrategyName(getRawChoiceValue(apvts,
                                                                          ats_param::kMorphCorrespondenceStrategy,
                                                                          kDefaultMorphCorrespondenceStrategy)));
    upsertRecipeUnknownField(recipe,
                             "ui.frequency_interpolation_domain",
                             getRawChoiceValue(apvts,
                                               ats_param::kMorphFrequencyInterpolationDomain,
                                               kDefaultMorphFrequencyDomain)
                                 == 1
                                 ? "bark_coordinate"
                                 : "log_frequency");
    upsertRecipeUnknownField(recipe,
                             "ui.amplitude_interpolation_domain",
                             getRawChoiceValue(apvts,
                                               ats_param::kMorphAmplitudeInterpolationDomain,
                                               kDefaultMorphAmplitudeDomain)
                                 == 1
                                 ? "decibels"
                                 : "power");
    upsertRecipeUnknownField(recipe,
                             "ui.discard_opt_in",
                             recipe.unmatchedPartialPolicies.discardOptIn ? "1" : "0");
}

} // namespace

AtsAudioProcessor::AtsAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "PARAMS", createParameterLayout())
{
    // Bound undo memory: ~256 MB of stored snapshots, but always keep at least
    // the most recent 8 transactions even if that exceeds the byte budget (a
    // single large model can be tens of MB, so keep this floor low).
    undoManager.setMaxNumberOfStoredUnits(256 * 1024, 8);
    morphEnvelopeCache = std::make_unique<MorphEnvelopeCache>();
    apvts.addParameterListener("freq", this);
    apvts.addParameterListener("timeStretch", this);
    apvts.addParameterListener("synStart", this);
    apvts.addParameterListener("synDur", this);
    apvts.addParameterListener("usePhase", this);
    apvts.addParameterListener("useAllPartials", this);
    apvts.addParameterListener(ats_param::kFreqShapeMode, this);
    apvts.addParameterListener(ats_param::kFreqShapeAmount, this);
    apvts.addParameterListener(ats_param::kFreqShapePivotHz, this);
    apvts.addParameterListener(ats_param::kMorphAmount, this);
    apvts.addParameterListener(ats_param::kMorphPerformanceMacro, this);
    apvts.addParameterListener(ats_param::kMorphSmoothingMs, this);
    apvts.addParameterListener(ats_param::kMorphGridType, this);
    apvts.addParameterListener(ats_param::kMorphCorrespondenceStrategy, this);
    apvts.addParameterListener(ats_param::kMorphQuality, this);
    apvts.addParameterListener(ats_param::kMorphUnmatchedSourcePolicy, this);
    apvts.addParameterListener(ats_param::kMorphUnmatchedTargetPolicy, this);
    apvts.addParameterListener(ats_param::kMorphDiscardOptIn, this);
    apvts.addParameterListener(ats_param::kMorphFrequencyInterpolationDomain, this);
    apvts.addParameterListener(ats_param::kMorphAmplitudeInterpolationDomain, this);
    apvts.addParameterListener(ats_param::kMorphPhaseStrategy, this);
    apvts.addParameterListener(ats_param::kMorphTransientPolicy, this);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        apvts.addParameterListener(ats_param::kMorphLaneAmountIds[i], this);
        apvts.addParameterListener(ats_param::kMorphLaneLinkedIds[i], this);
    }
    for (const char* id : kSmearnovDetectionParamIds)
        apvts.addParameterListener(id, this);

    outputTrimRaw = apvts.getRawParameterValue("outputTrim");
    partGainRaw   = apvts.getRawParameterValue("partGain");
    resGainRaw    = apvts.getRawParameterValue("resGain");
    classicPreviewModeRaw = apvts.getRawParameterValue(ats_param::kClassicPreviewMode);

    startTimerHz(30);
}

AtsAudioProcessor::~AtsAudioProcessor()
{
    stopTimer();
    cancelJobs();
    joinWorkers();
    apvts.removeParameterListener("freq", this);
    apvts.removeParameterListener("timeStretch", this);
    apvts.removeParameterListener("synStart", this);
    apvts.removeParameterListener("synDur", this);
    apvts.removeParameterListener("usePhase", this);
    apvts.removeParameterListener("useAllPartials", this);
    apvts.removeParameterListener(ats_param::kFreqShapeMode, this);
    apvts.removeParameterListener(ats_param::kFreqShapeAmount, this);
    apvts.removeParameterListener(ats_param::kFreqShapePivotHz, this);
    apvts.removeParameterListener(ats_param::kMorphAmount, this);
    apvts.removeParameterListener(ats_param::kMorphPerformanceMacro, this);
    apvts.removeParameterListener(ats_param::kMorphSmoothingMs, this);
    apvts.removeParameterListener(ats_param::kMorphGridType, this);
    apvts.removeParameterListener(ats_param::kMorphCorrespondenceStrategy, this);
    apvts.removeParameterListener(ats_param::kMorphQuality, this);
    apvts.removeParameterListener(ats_param::kMorphUnmatchedSourcePolicy, this);
    apvts.removeParameterListener(ats_param::kMorphUnmatchedTargetPolicy, this);
    apvts.removeParameterListener(ats_param::kMorphDiscardOptIn, this);
    apvts.removeParameterListener(ats_param::kMorphFrequencyInterpolationDomain, this);
    apvts.removeParameterListener(ats_param::kMorphAmplitudeInterpolationDomain, this);
    apvts.removeParameterListener(ats_param::kMorphPhaseStrategy, this);
    apvts.removeParameterListener(ats_param::kMorphTransientPolicy, this);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        apvts.removeParameterListener(ats_param::kMorphLaneAmountIds[i], this);
        apvts.removeParameterListener(ats_param::kMorphLaneLinkedIds[i], this);
    }
    for (const char* id : kSmearnovDetectionParamIds)
        apvts.removeParameterListener(id, this);
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
        ats_param::kMorphAmount, "Morph amount", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kMorphPerformanceMacro,
        "Morph performance macro",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        kDefaultMorphPerformanceMacro));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kMorphSmoothingMs,
        "Morph smoothing (ms)",
        juce::NormalisableRange<float>(0.0f, 250.0f, 0.01f, 0.35f),
        kDefaultMorphSmoothingMs));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphGridType,
        "Morph grid type",
        juce::StringArray { "Bark", "Mel" },
        kDefaultMorphGridType));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphCorrespondenceStrategy,
        "Morph correspondence strategy",
        juce::StringArray { "Frequency rank", "Greedy nearest", "Assignment matrix", "Optimal transport" },
        kDefaultMorphCorrespondenceStrategy));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphQuality,
        "Morph quality",
        juce::StringArray { "Max", "Default", "Economy" },
        kDefaultMorphQuality));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphUnmatchedSourcePolicy,
        "Source-only partial policy",
        juce::StringArray { "Fade out", "Fade in", "Hold", "Merge residual", "Discard" },
        kDefaultMorphUnmatchedSourcePolicy));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphUnmatchedTargetPolicy,
        "Target-only partial policy",
        juce::StringArray { "Fade out", "Fade in", "Hold", "Merge residual", "Discard" },
        kDefaultMorphUnmatchedTargetPolicy));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        ats_param::kMorphDiscardOptIn,
        "Enable discard policy",
        false));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphFrequencyInterpolationDomain,
        "Frequency interpolation domain",
        juce::StringArray { "Log frequency", "Bark coordinate" },
        kDefaultMorphFrequencyDomain));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphAmplitudeInterpolationDomain,
        "Amplitude interpolation domain",
        juce::StringArray { "Power", "Decibels" },
        kDefaultMorphAmplitudeDomain));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphPhaseStrategy,
        "Morph phase strategy",
        juce::StringArray {
            "Oscillator continuation",
            "Source A donor",
            "Source B donor",
            "Relative cluster",
            "Direct blend",
        },
        kDefaultMorphPhaseStrategy));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kMorphTransientPolicy,
        "Morph transient policy",
        juce::StringArray { "Source A attack", "Source B attack", "Crossfade attacks", "Sustain after attack" },
        kDefaultMorphTransientPolicy));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovBlurMs,
        "Smearnov blur (ms)",
        juce::NormalisableRange<float>(0.0f, 120.0f, 0.01f, 0.45f),
        0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovSharpenAmount,
        "Smearnov sharpen",
        juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f, 0.5f),
        0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovExaggerateDb,
        "Smearnov exaggerate (dB)",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.01f),
        0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovMix,
        "Smearnov mix (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovMaxDb,
        "Smearnov max gain (dB)",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.01f),
        12.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kSmearnovTargets,
        "Smearnov targets",
        juce::StringArray { "Partials", "Residual", "Both" },
        2));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kSmearnovScope,
        "Smearnov scope",
        juce::StringArray { "All", "Selection", "Sensitivity" },
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovWeightDetFlux,
        "Smearnov deterministic flux weight",
        juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f),
        1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovWeightResFlux,
        "Smearnov residual flux weight",
        juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f),
        0.8f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovWeightHfcFlux,
        "Smearnov HFC flux weight",
        juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f),
        0.6f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovWeightBirth,
        "Smearnov birth weight",
        juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f),
        0.7f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovWeightPhaseDev,
        "Smearnov phase-deviation weight",
        juce::NormalisableRange<float>(0.0f, 4.0f, 0.0001f),
        0.4f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovThresholdLambda,
        "Smearnov threshold lambda",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.001f, 0.45f),
        0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovThresholdAutoScale,
        "Smearnov auto-threshold scale",
        juce::NormalisableRange<float>(0.0f, 8.0f, 0.001f),
        1.5f));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ats_param::kSmearnovPeakWindowFrames,
        "Smearnov peak window (frames)",
        1,
        64,
        6));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ats_param::kSmearnovMinGapFrames,
        "Smearnov min gap override (frames)",
        0,
        512,
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kSmearnovPreMarginMs,
        "Smearnov pre-margin (ms)",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.01f, 0.5f),
        5.0f));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ats_param::kSmearnovMaxAttackFrames,
        "Smearnov max attack (frames)",
        0,
        1024,
        0));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        ats_param::kSmearnovUsePhaseDeviation,
        "Smearnov use phase deviation",
        false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        ats_param::kSmearnovUseRpcaFallback,
        "Smearnov RPCA fallback",
        false));
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        const juce::String laneName(ats_param::kMorphLaneDisplayNames[i]);
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            ats_param::kMorphLaneAmountIds[i],
            "Morph lane " + laneName,
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
            0.0f));
        layout.add(std::make_unique<juce::AudioParameterBool>(
            ats_param::kMorphLaneLinkedIds[i],
            "Link " + laneName + " lane",
            true));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "freq", "Frequency factor", juce::NormalisableRange<float>(0.25f, 4.0f, 0.0001f, 0.35f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kFreqShapeMode,
        "Frequency shaping mode",
        juce::StringArray { "Linear", "Stretch", "Compress", "Harmonic" },
        0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kFreqShapeAmount,
        "Frequency shaping amount",
        juce::NormalisableRange<float>(0.25f, 4.0f, 0.0001f, 0.35f),
        1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kFreqShapePivotHz,
        "Frequency shaping pivot (Hz)",
        juce::NormalisableRange<float>(0.0f, 4000.0f, 0.01f, 0.45f),
        0.0f));
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
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kClassicPreviewMode,
        "Both playback mode",
        juce::StringArray { "Classic combined", "Layer mix" },
        kDefaultClassicPreviewMode));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "outputTrim", "Output trim", juce::NormalisableRange<float>(0.0f, 2.0f, 0.0001f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kAnalysisType,
        "Analysis type",
        juce::StringArray { "1 amp+freq", "2 +phase", "3 +noise", "4 full" },
        3));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisStart,
        "Analysis start (s)",
        juce::NormalisableRange<float>(0.0f, 600.0f, 0.001f, 0.4f),
        (float) ATSA_START));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisDuration,
        "Analysis duration (s, 0=full)",
        juce::NormalisableRange<float>(0.0f, 600.0f, 0.001f, 0.4f),
        (float) ATSA_DUR));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisLowestFreq,
        "Lowest frequency (Hz)",
        juce::NormalisableRange<float>(1.0f, 2000.0f, 0.01f, 0.35f),
        (float) ATSA_LFREQ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisHighestFreq,
        "Highest frequency (Hz)",
        juce::NormalisableRange<float>(20.0f, 24000.0f, 0.01f, 0.35f),
        (float) ATSA_HFREQ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisFreqDev,
        "Frequency deviation",
        juce::NormalisableRange<float>(0.001f, 0.99f, 0.0001f, 0.5f),
        (float) ATSA_FREQDEV));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ats_param::kAnalysisWinCycles,
        "Window cycles",
        1,
        8,
        ATSA_WCYCLES));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ats_param::kAnalysisWinType,
        "Window type",
        juce::StringArray { "Blackman", "Blackman-Harris", "Hanning", "Von Hann" },
        ATSA_WTYPE));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisHopSize,
        "Hop size ratio",
        juce::NormalisableRange<float>(0.01f, 1.0f, 0.0001f, 0.5f),
        (float) ATSA_HSIZE));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisLowestMag,
        "Peak threshold (dB)",
        juce::NormalisableRange<float>(-120.0f, 0.0f, 0.01f),
        (float) ATSA_LMAG));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ats_param::kAnalysisTrackLen,
        "Track memory (frames)",
        1,
        64,
        ATSA_TRKLEN));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ats_param::kAnalysisMinSegLen,
        "Min segment length (frames)",
        1,
        128,
        ATSA_MSEGLEN));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        ats_param::kAnalysisMinGapLen,
        "Min gap length (frames)",
        0,
        128,
        ATSA_MGAPLEN));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisSmrThreshold,
        "SMR threshold (dB)",
        juce::NormalisableRange<float>(0.0f, (float) ATSA_MAX_DB_SPL - 0.001f, 0.01f),
        (float) ATSA_SMRTHRES));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisMinSegSmr,
        "Min segment SMR (dB)",
        juce::NormalisableRange<float>(0.0f, (float) ATSA_MAX_DB_SPL - 0.001f, 0.01f),
        (float) ATSA_MSEGSMR));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisLastPeakContribution,
        "Last peak tracking weight",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        (float) ATSA_LPKCONT));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        ats_param::kAnalysisSmrContribution,
        "SMR tracking weight",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
        (float) ATSA_SMRCONT));

    return layout;
}

void AtsAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);
    if (isFrequencyShapeParameter(parameterID))
    {
        displayShapeRevision.fetch_add(1, std::memory_order_acq_rel);
        // Debounce: record change time and let timerCallback fire the resynth
        // once the slider has been idle for 120 ms. This prevents spawning a
        // new std::thread on every mouse-move event during a drag.
        freqShapePendingResynth.store(true, std::memory_order_release);
        freqShapeLastChangeMs.store(juce::Time::currentTimeMillis(), std::memory_order_release);
        return;
    }

    if (isMorphSnapshotParameter(parameterID))
    {
        markMorphSnapshotStale();
        return;
    }

    if (isSmearnovDetectionParameter(parameterID))
    {
        // Detection tuning changed: drop pinned auto-detections so the next Apply
        // re-detects. Does not affect synthesis, so no resynth is triggered.
        invalidateAutoDetectedOnsetOverrides();
        return;
    }

    synthParamsDirty.store(true);
    triggerAsyncUpdate();
}

void AtsAudioProcessor::handleAsyncUpdate()
{
    if (synthParamsDirty.exchange(false))
        startResynthWorker();
}

void AtsAudioProcessor::timerCallback()
{
    // Record undo entries for completed worker-thread edits (math script, smear
    // apply). The before-snapshot was captured on the message thread at kickoff.
    if (asyncUndoCommit.exchange(false, std::memory_order_acquire))
    {
        recordModelEdit(asyncUndoSource, asyncUndoBefore, asyncUndoLabel);
        asyncUndoBefore.reset();
    }

    // Coalesce rapid parameter changes (knob drags) into ~500 ms undo
    // transactions; beginNewTransaction is a no-op when nothing changed.
    if (++undoTransactionTick >= 15) // timer runs at 30 Hz
    {
        undoTransactionTick = 0;
        undoManager.beginNewTransaction();
    }

    // Pick up synthParamsDirty set by processBlock (MIDI) without calling
    // triggerAsyncUpdate() from the audio thread.
    if (synthParamsDirty.load(std::memory_order_acquire))
        triggerAsyncUpdate();

    // Fire the debounced freq-shape resynth once the slider has been idle ≥120 ms.
    if (freqShapePendingResynth.load(std::memory_order_acquire))
    {
        const int64_t elapsed = juce::Time::currentTimeMillis()
                                - freqShapeLastChangeMs.load(std::memory_order_acquire);
        if (elapsed >= 80)
        {
            freqShapePendingResynth.store(false, std::memory_order_release);
            synthParamsDirty.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }
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
    sp.frequencyShape = readFrequencyShapeFromApvts();
    return sp;
}

ats::FrequencyShapeSettings AtsAudioProcessor::readFrequencyShapeFromApvts() const
{
    ats::FrequencyShapeSettings shape;
    int mode = juce::jlimit(0,
                            3,
                            juce::roundToInt(getRawParamValue(apvts,
                                                              ats_param::kFreqShapeMode,
                                                              0.0f)));
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(ats_param::kFreqShapeMode)))
        mode = juce::jlimit(0, 3, choice->getIndex());
    shape.mode = (ats::FrequencyShapeMode) mode;
    shape.amount = getRawParamValue(apvts, ats_param::kFreqShapeAmount, 1.0f);
    shape.pivotHz = getRawParamValue(apvts, ats_param::kFreqShapePivotHz, 0.0f);
    shape.resolvedPivotHz = 0.0;
    return shape;
}

ats::FrequencyShapeSettings AtsAudioProcessor::getCurrentFrequencyShapeSettings() const
{
    auto shape = readFrequencyShapeFromApvts();
    const juce::ScopedLock ml(modelLock);
    return ats::resolveFrequencyShapeSettingsForSound(analyzedModel.get(), shape);
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
    const float v = apvts.getRawParameterValue(ats_param::kAnalysisType)->load();
    const int idx = juce::jlimit(0, 3, juce::roundToInt(v));
    a.type = idx + 1;
    a.start = getRawParamValue(apvts, ats_param::kAnalysisStart, (float) ATSA_START);
    a.duration = getRawParamValue(apvts, ats_param::kAnalysisDuration, (float) ATSA_DUR);
    a.lowest_freq = getRawParamValue(apvts, ats_param::kAnalysisLowestFreq, (float) ATSA_LFREQ);
    a.highest_freq = getRawParamValue(apvts, ats_param::kAnalysisHighestFreq, (float) ATSA_HFREQ);
    a.freq_dev = getRawParamValue(apvts, ats_param::kAnalysisFreqDev, (float) ATSA_FREQDEV);
    a.win_cycles = getRawChoiceValue(apvts, ats_param::kAnalysisWinCycles, ATSA_WCYCLES);
    a.win_type = getRawChoiceValue(apvts, ats_param::kAnalysisWinType, ATSA_WTYPE);
    a.hop_size = getRawParamValue(apvts, ats_param::kAnalysisHopSize, (float) ATSA_HSIZE);
    a.lowest_mag = getRawParamValue(apvts, ats_param::kAnalysisLowestMag, (float) ATSA_LMAG);
    a.track_len = getRawChoiceValue(apvts, ats_param::kAnalysisTrackLen, ATSA_TRKLEN);
    a.min_seg_len = getRawChoiceValue(apvts, ats_param::kAnalysisMinSegLen, ATSA_MSEGLEN);
    a.min_gap_len = getRawChoiceValue(apvts, ats_param::kAnalysisMinGapLen, ATSA_MGAPLEN);
    a.SMR_thres = getRawParamValue(apvts, ats_param::kAnalysisSmrThreshold, (float) ATSA_SMRTHRES);
    a.min_seg_SMR = getRawParamValue(apvts, ats_param::kAnalysisMinSegSmr, (float) ATSA_MSEGSMR);
    a.last_peak_cont = getRawParamValue(apvts, ats_param::kAnalysisLastPeakContribution, (float) ATSA_LPKCONT);
    a.SMR_cont = getRawParamValue(apvts, ats_param::kAnalysisSmrContribution, (float) ATSA_SMRCONT);
    return a;
}

ats::SmearnovDetectParams AtsAudioProcessor::readSmearnovDetectParamsFromApvts() const
{
    ats::SmearnovDetectParams params;
    params.weightDetFlux = getRawParamValue(apvts, ats_param::kSmearnovWeightDetFlux, 1.0f);
    params.weightResFlux = getRawParamValue(apvts, ats_param::kSmearnovWeightResFlux, 0.8f);
    params.weightHfcFlux = getRawParamValue(apvts, ats_param::kSmearnovWeightHfcFlux, 0.6f);
    params.weightBirth = getRawParamValue(apvts, ats_param::kSmearnovWeightBirth, 0.7f);
    params.weightPhaseDev = getRawParamValue(apvts, ats_param::kSmearnovWeightPhaseDev, 0.4f);
    params.usePhaseDeviation = getRawParamValue(apvts, ats_param::kSmearnovUsePhaseDeviation, 0.0f) > 0.5f;
    params.peakWindowRadiusFrames = getRawChoiceValue(apvts, ats_param::kSmearnovPeakWindowFrames, 6);
    params.thresholdLambda = getRawParamValue(apvts, ats_param::kSmearnovThresholdLambda, 0.0f);
    params.thresholdAutoScale = getRawParamValue(apvts, ats_param::kSmearnovThresholdAutoScale, 1.5f);
    params.minGapFramesOverride = getRawChoiceValue(apvts, ats_param::kSmearnovMinGapFrames, 0);
    params.preMarginSeconds = (double) getRawParamValue(apvts, ats_param::kSmearnovPreMarginMs, 5.0f) * 0.001;
    params.maxAttackFrames = getRawChoiceValue(apvts, ats_param::kSmearnovMaxAttackFrames, 0);
    return params;
}

ats::SmearnovParams AtsAudioProcessor::readSmearnovParamsFromApvts() const
{
    ats::SmearnovParams params;
    params.blurMs = getRawParamValue(apvts, ats_param::kSmearnovBlurMs, 0.0f);
    params.sharpenAmount = getRawParamValue(apvts, ats_param::kSmearnovSharpenAmount, 0.0f);
    params.exaggerateDb = getRawParamValue(apvts, ats_param::kSmearnovExaggerateDb, 0.0f);
    params.mix = getRawParamValue(apvts, ats_param::kSmearnovMix, 100.0f) / 100.0;
    params.maxDb = getRawParamValue(apvts, ats_param::kSmearnovMaxDb, 12.0f);
    switch (getRawChoiceValue(apvts, ats_param::kSmearnovTargets, 2))
    {
        case 0: params.targets = ats::SmearnovTarget::Partials; break;
        case 1: params.targets = ats::SmearnovTarget::Residual; break;
        case 2:
        default: params.targets = ats::SmearnovTarget::Both; break;
    }
    return params;
}

void AtsAudioProcessor::loadSourceFile(const juce::File& file)
{
    // Loading a new source replaces the model; old undo snapshots no longer apply.
    undoManager.clearUndoHistory();
    pendingMorphSourceSlotAfterAnalysis.store(-1, std::memory_order_release);
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
        currentSourceFile = file;
    }

    {
        const juce::ScopedLock ml(modelLock);
        analyzedModel.reset(nullptr);
        currentModelName = file.getFileNameWithoutExtension();
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
    previewLayerCacheCombined.clear();
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
    {
        const juce::ScopedLock sl(sourceLock);
        if (sourceMono.empty())
        {
            setError("No audio loaded.");
            return;
        }
        localMono = sourceMono;
        fileSr = sourceSampleRate;
    }

    if (fileSr <= 0)
    {
        setError("Invalid sample rate.");
        return;
    }

    const uint64_t gen = jobGeneration.load();

    setUiState(AtsUiState::Analyzing);
    analysisProgress.store(0.05f);
    analysisThreadRunning.store(true);

    analysisThread = std::thread([this, localMono, fileSr, gen]() {
        ats::AnalysisParams ap = readAnalysisParamsFromApvts();
        ANARGS an {};
        ap.toAnargs(an);

        // Stream real tracker progress into [0.10, 0.90] so the bar tracks
        // the heavy frame loop instead of jumping after it returns.
        an.progress_user = &analysisProgress;
        an.progress_cb = [](void* user, double fraction) {
            auto* prog = static_cast<std::atomic<float>*>(user);
            prog->store(0.10f + 0.80f * (float) fraction);
        };

        auto* samples = const_cast<double*>(localMono.data());
        double** residual = nullptr;
        int resLen = 0;

        analysisProgress.store(0.10f);
        ATS_SOUND* raw = tracker(&an, samples, fileSr, (int) localMono.size(), &residual, &resLen);
        freeResidual(&residual);

        if (jobGeneration.load() != gen)
        {
            if (raw != nullptr)
            {
                free_sound(raw);
                std::free(raw);
            }
            pendingMorphSourceSlotAfterAnalysis.store(-1, std::memory_order_release);
            analysisThreadRunning.store(false);
            return;
        }

        analysisProgress.store(0.92f);

        {
            const juce::ScopedLock ml(modelLock);
            analyzedModel.reset(raw);
        }

        modelRevision.fetch_add(1, std::memory_order_relaxed);
        clearSmearnovOnsetOverrides(AtsTraceSource::Current);

        const int pendingSlot = pendingMorphSourceSlotAfterAnalysis.exchange(-1, std::memory_order_acq_rel);
        if (pendingSlot == 0 || pendingSlot == 1)
        {
            juce::String slotError;
            if (!setCurrentModelAsMorphSourceSlot(pendingSlot, slotError) && slotError.isNotEmpty())
                setError(slotError);
        }

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
        juce::AudioBuffer<float> monoC;
        int modelSr = 44100;
        int modelRevSnapshot = -1;
        bool useLayerCache = false;

        // Hold modelLock only long enough to validate state and take a snapshot
        // copy. renderClassicLayers (which runs multiple full synthesis passes)
        // must NOT hold modelLock — the message thread acquires it on every
        // 100 ms UI tick and would freeze tabs/interactions for the full
        // synthesis duration otherwise.
        ats::AtsSoundPtr modelSnapshot;
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
                            && (!previewLayerCachePartials.empty() || !previewLayerCacheResidual.empty()
                                || !previewLayerCacheCombined.empty());

            if (!useLayerCache)
            {
                auto* raw = copy_sound(const_cast<ATS_SOUND*>(analyzedModel.get()));
                if (raw == nullptr)
                {
                    resynthThreadRunning.store(false);
                    setUiState(AtsUiState::Analyzed);
                    return;
                }
                modelSnapshot.reset(raw);
            }
        }
        // modelLock is now released — message thread is unblocked.

        if (!useLayerCache)
        {
            if (jobGeneration.load() != gen || mySerial != resynthJobSerial.load(std::memory_order_acquire))
            {
                resynthThreadRunning.store(false);
                setUiState(AtsUiState::Analyzed);
                return;
            }
            auto rendered = renderClassicLayers(modelSnapshot.get(), core);
            modelSr = rendered.modelSampleRate;
            monoP = std::move(rendered.partials);
            monoR = std::move(rendered.residual);
            monoC = std::move(rendered.combined);
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
            const int nC = monoC.getNumSamples();
            if (nP <= 0 && nR <= 0 && nC <= 0)
            {
                setError("Synthesis produced silence.");
                resynthThreadRunning.store(false);
                setUiState(AtsUiState::Analyzed);
                return;
            }

            std::vector<float> resampledP;
            std::vector<float> resampledR;
            std::vector<float> resampledC;
            // Each stream is independent — run in parallel.
            auto doResampleP = [&] {
                if (nP > 0)
                    ats::resampleLinearMono(monoP.getReadPointer(0), nP, (double) modelSr, resampledP, hostSampleRate);
            };
            auto doResampleR = [&] {
                if (nR > 0)
                    ats::resampleLinearMono(monoR.getReadPointer(0), nR, (double) modelSr, resampledR, hostSampleRate);
            };
            auto futureResP = std::async(std::launch::async, doResampleP);
            auto futureResR = std::async(std::launch::async, doResampleR);
            if (nC > 0)
                ats::resampleLinearMono(monoC.getReadPointer(0), nC, (double) modelSr, resampledC, hostSampleRate);
            futureResP.get();
            futureResR.get();

            if (resampledP.empty() && resampledR.empty() && resampledC.empty())
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
            previewLayerCacheCombined = std::move(resampledC);
            previewLayerCacheCore = core;
            previewLayerCacheRevision = modelRevSnapshot;
            previewLayerCacheHostSr = hostSampleRate;
            previewLayerCacheValid = true;
        }

        const size_t outN = juce::jmax(previewLayerCacheCombined.size(),
                                       juce::jmax(previewLayerCachePartials.size(), previewLayerCacheResidual.size()));
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

        const bool replacingActivePreview =
            activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex;
        const bool resumeAfterSwap = replacingActivePreview && previewPlaying.load(std::memory_order_acquire);
        const float resumeNorm = resumeAfterSwap
                                     ? getPreviewPlayheadNormForTraceSource(AtsTraceSource::Current)
                                     : -1.0f;
        if (replacingActivePreview)
            previewPlaying.store(false, std::memory_order_release);

        // Wait for processBlock to finish reading this buffer before reallocating.
        while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
            std::this_thread::yield();

        auto& playback = playbackBuffers[(size_t) kMorphPlaybackBufferIndex];
        playback.setSize(previewLayerCacheCombined.empty() ? 2 : 3, (int) outN);
        {
            const bool hasCombined = playback.getNumChannels() > 2;
            float* const dstP = playback.getWritePointer(0);
            float* const dstR = playback.getWritePointer(1);
            float* const dstC = hasCombined ? playback.getWritePointer(2) : nullptr;
            const int nCacheP = (int) previewLayerCachePartials.size();
            const int nCacheR = (int) previewLayerCacheResidual.size();
            const int nCacheC = (int) previewLayerCacheCombined.size();
            for (int i = 0; i < (int) outN; ++i)
            {
                const float p = i < nCacheP ? previewLayerCachePartials[(size_t) i] : 0.0f;
                const float r = i < nCacheR ? previewLayerCacheResidual[(size_t) i] : 0.0f;
                dstP[i] = p;
                dstR[i] = r;
                if (dstC != nullptr)
                    dstC[i] = i < nCacheC ? previewLayerCacheCombined[(size_t) i] : p + r;
            }
        }

        if (mySerial != resynthJobSerial.load(std::memory_order_acquire))
        {
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        activePlaybackBufferIndex.store(kMorphPlaybackBufferIndex, std::memory_order_release);
        previewPlaybackKind.store((int) AtsPreviewPlaybackKind::MorphPreview, std::memory_order_release);
        if (resumeAfterSwap && playback.getNumSamples() > 1)
        {
            const int resumeSample = juce::jlimit(0,
                                                  playback.getNumSamples() - 1,
                                                  juce::roundToInt((double) juce::jlimit(0.0f, 1.0f, resumeNorm)
                                                                   * (double) (playback.getNumSamples() - 1)));
            playbackPosition = resumeSample;
            previewPlaybackAnchorSamples.store((double) resumeSample, std::memory_order_release);
            previewPlaybackAnchorTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
            previewPlaybackHostSampleRate.store(hostSampleRate, std::memory_order_release);
            previewPlaying.store(true, std::memory_order_release);
        }
        else
        {
            previewPlaying.store(false, std::memory_order_release);
            previewPlaybackAnchorSamples.store(0.0, std::memory_order_release);
        }

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
            synthParamsDirty.store(true, std::memory_order_release);
            // triggerAsyncUpdate() is RT-unsafe on macOS (GCD dispatch).
            // timerCallback() picks up synthParamsDirty on the message thread.
        }
    }

    outputTrimSmoothed.setTargetValue(outputTrimRaw->load(std::memory_order_relaxed));
    partGainSmoothed.setTargetValue(partGainRaw->load(std::memory_order_relaxed));
    resGainSmoothed.setTargetValue(resGainRaw->load(std::memory_order_relaxed));
    const auto auditionMode = (AtsSourceAuditionMode) previewAuditionMode.load(std::memory_order_acquire);
    const bool includePartials = auditionMode != AtsSourceAuditionMode::ResidualOnly;
    const bool includeResidual = auditionMode != AtsSourceAuditionMode::PartialsOnly;
    const bool useClassicBothPlayback =
        juce::jmax(0, juce::roundToInt(classicPreviewModeRaw->load(std::memory_order_relaxed))) == 0;

    const int n = buffer.getNumSamples();
    const int activeIndex = activePlaybackBufferIndex.load(std::memory_order_acquire);
    const bool hasPlayback = activeIndex >= 0 && activeIndex < (int) playbackBuffers.size();
    readingPlaybackBufferIndex.store(hasPlayback ? activeIndex : -1, std::memory_order_release);

    if (activeIndex != currentPlaybackBufferIndex)
    {
        currentPlaybackBufferIndex = activeIndex;
        playbackPosition = 0;
        previewPlaybackAnchorSamples.store(0.0, std::memory_order_release);
        previewPlaybackAnchorTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
    }

    const auto* playback = hasPlayback ? &playbackBuffers[(size_t) activeIndex] : nullptr;
    const bool shouldPreview = previewPlaying.load(std::memory_order_acquire);

    if (shouldPreview && playback != nullptr)
    {
        previewPlaybackAnchorSamples.store((double) playbackPosition, std::memory_order_release);
        previewPlaybackAnchorTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
        previewPlaybackHostSampleRate.store(hostSampleRate, std::memory_order_release);
    }

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
            const bool useClassicCombined = auditionMode == AtsSourceAuditionMode::Both
                                            && nCh > 2
                                            && useClassicBothPlayback;
            const float mixed = useClassicCombined
                                    ? playback->getSample(2, playbackPosition)
                                    : ((includePartials ? (part * pg) : 0.0f)
                                       + (includeResidual ? (res * rg) : 0.0f));
            v = softLimitPreviewSample(mixed * trim);
            ++playbackPosition;
        }

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            buffer.setSample(c, s, v);
    }

    if (shouldPreview && (playback == nullptr || playbackPosition >= playback->getNumSamples()))
    {
        previewPlaying.store(false, std::memory_order_release);
        previewPlaybackKind.store((int) AtsPreviewPlaybackKind::None, std::memory_order_release);
        previewPlaybackAnchorSamples.store(0.0, std::memory_order_release);
    }

    readingPlaybackBufferIndex.store(-1, std::memory_order_release);
}

void AtsAudioProcessor::startPreviewPlayback()
{
    const int idx = activePlaybackBufferIndex.load(std::memory_order_acquire);
    const int n = (idx >= 0 && idx < (int) playbackBuffers.size()) ? playbackBuffers[(size_t) idx].getNumSamples() : -1;
    atsPlayLog("startPreviewPlayback activeIdx=" + juce::String(idx) + " n=" + juce::String(n)
               + " hasPreviewAudio=" + juce::String((int) hasPreviewAudio()));
    if (!hasPreviewAudio())
        return;
    playbackPosition = 0;
    previewPlaybackAnchorSamples.store(0.0, std::memory_order_release);
    previewPlaybackAnchorTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
    previewPlaybackHostSampleRate.store(hostSampleRate, std::memory_order_release);
    previewPlaying.store(true, std::memory_order_release);
}

void AtsAudioProcessor::stopPreviewPlayback()
{
    previewPlaying.store(false, std::memory_order_release);
    playbackPosition = 0;
    previewPlaybackAnchorSamples.store(0.0, std::memory_order_release);
    previewPlaybackKind.store((int) AtsPreviewPlaybackKind::None, std::memory_order_release);
}

float AtsAudioProcessor::getPreviewPlayheadNormForTraceSource(AtsTraceSource source) const
{
    if (!previewPlaying.load(std::memory_order_acquire))
        return -1.0f;

    const auto kind = (AtsPreviewPlaybackKind) previewPlaybackKind.load(std::memory_order_acquire);
    switch (source)
    {
        case AtsTraceSource::Current:
            if (kind != AtsPreviewPlaybackKind::MorphPreview
                && kind != AtsPreviewPlaybackKind::MorphTimeline)
                return -1.0f;
            break;
        case AtsTraceSource::SourceA:
            if (kind != AtsPreviewPlaybackKind::SourceA
                && kind != AtsPreviewPlaybackKind::MorphTimeline)
                return -1.0f;
            break;
        case AtsTraceSource::SourceB:
            if (kind != AtsPreviewPlaybackKind::SourceB
                && kind != AtsPreviewPlaybackKind::MorphTimeline)
                return -1.0f;
            break;
    }

    const int activeIndex = activePlaybackBufferIndex.load(std::memory_order_acquire);
    if (activeIndex < 0 || activeIndex >= (int) playbackBuffers.size())
        return -1.0f;

    const int totalSamples = playbackBuffers[(size_t) activeIndex].getNumSamples();
    if (totalSamples <= 1)
        return -1.0f;

    const double anchorSamples = previewPlaybackAnchorSamples.load(std::memory_order_acquire);
    const double anchorMs = previewPlaybackAnchorTimeMs.load(std::memory_order_acquire);
    const double sampleRate = previewPlaybackHostSampleRate.load(std::memory_order_acquire);
    const double elapsedSec = (juce::Time::getMillisecondCounterHiRes() - anchorMs) * 0.001;
    const double interpolatedSamples = juce::jlimit(0.0,
                                                  (double) (totalSamples - 1),
                                                  anchorSamples + elapsedSec * sampleRate);
    return (float) (interpolatedSamples / (double) (totalSamples - 1));
}

bool AtsAudioProcessor::hasPreviewAudio() const
{
    const int idx = activePlaybackBufferIndex.load(std::memory_order_acquire);
    if (idx < 0 || idx >= (int) playbackBuffers.size())
        return false;
    return playbackBuffers[(size_t) idx].getNumSamples() > 0;
}

bool AtsAudioProcessor::hasMorphSourcePreviewAudio(int slotIndex) const
{
    if (slotIndex < 0 || slotIndex > 1)
        return false;
    return playbackBuffers[(size_t) slotIndex].getNumSamples() > 0;
}

bool AtsAudioProcessor::hasMorphPreviewAudio() const
{
    return playbackBuffers[(size_t) kMorphPlaybackBufferIndex].getNumSamples() > 0;
}

bool AtsAudioProcessor::playCurrentModelPreview(juce::String& error)
{
    return playCurrentModelPreview(error, false);
}

bool AtsAudioProcessor::playCurrentModelPreview(juce::String& error, bool liveSmear)
{
    return playCurrentModelPreview(AtsSourceAuditionMode::Both, liveSmear, error);
}

bool AtsAudioProcessor::playCurrentModelPreview(AtsSourceAuditionMode mode, bool liveSmear, juce::String& error)
{
    error.clear();
    if (isWorkerBusy())
    {
        atsPlayLog("playCurrentModelPreview REJECTED: worker busy");
        error = "Wait for the current analysis or render to finish before playing.";
        return false;
    }

    int partials = 0;
    int frames = 0;
    bool hasResidual = false;
    if (!getModelFootprint(partials, frames, hasResidual) || partials <= 0 || frames <= 0)
    {
        atsPlayLog("playCurrentModelPreview REJECTED: empty footprint p=" + juce::String(partials)
                   + " f=" + juce::String(frames));
        error = "Analyze or import an ATS model before playing.";
        return false;
    }
    atsPlayLog("playCurrentModelPreview mode=" + juce::String((int) mode)
               + " liveSmear=" + juce::String((int) liveSmear)
               + " p=" + juce::String(partials) + " f=" + juce::String(frames));

    if (mode == AtsSourceAuditionMode::ResidualOnly && !hasResidual)
    {
        error = "This model has no residual layer to audition.";
        return false;
    }

    const bool smearIsolate = mode == AtsSourceAuditionMode::SmearOnly;
    if (!renderCurrentModelToPlaybackBuffer(kMorphPlaybackBufferIndex, error,
                                            liveSmear || smearIsolate, smearIsolate))
        return false;

    activePlaybackBufferIndex.store(kMorphPlaybackBufferIndex, std::memory_order_release);
    previewAuditionMode.store((int) mode, std::memory_order_release);
    previewPlaybackKind.store((int) AtsPreviewPlaybackKind::MorphPreview, std::memory_order_release);
    startPreviewPlayback();
    return true;
}

bool AtsAudioProcessor::canPlayMorphPreview() const
{
    AtsMorphSourceSlotInfo sourceA;
    AtsMorphSourceSlotInfo sourceB;
    if (!getMorphSourceSlotInfo(0, sourceA) || !sourceA.hasModel
        || !getMorphSourceSlotInfo(1, sourceB) || !sourceB.hasModel)
    {
        return false;
    }

    return true;
}

bool AtsAudioProcessor::transportCacheTryReuse(int bufferIndex, int sourceTag, const ats::SynthParams& core,
                                              bool liveSmear, bool smearIsolate)
{
    if (bufferIndex < 0 || bufferIndex >= (int) transportRenderCache.size())
        return false;

    auto& e = transportRenderCache[(size_t) bufferIndex];
    if (!e.valid || e.buffer.getNumSamples() <= 0)
        return false;

    if (e.sourceTag != sourceTag
        || e.liveSmear != liveSmear
        || e.smearIsolate != smearIsolate
        || e.modelRevisionSnap != modelRevision.load(std::memory_order_acquire)
        || e.morphSourcesRevisionSnap != morphSourcesRevision.load(std::memory_order_acquire)
        || e.displayShapeRevisionSnap != displayShapeRevision.load(std::memory_order_acquire)
        || std::abs(e.hostSr - hostSampleRate) > 1e-6
        || !samePreviewLayerCore(e.core, core))
        return false;

    if ((liveSmear || smearIsolate)
        && (e.smearHash != computeSmearLiveHash()
            || e.smearOverlayRevisionSnap != smearnovOverlayRevision.load(std::memory_order_acquire)))
        return false;

    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();

    playbackBuffers[(size_t) bufferIndex].makeCopyOf(e.buffer);
    {
        auto& pb = playbackBuffers[(size_t) bufferIndex];
        atsPlayLog("cacheHIT buf=" + juce::String(bufferIndex) + " tag=" + juce::String(sourceTag)
                   + " n=" + juce::String(pb.getNumSamples()) + " ch=" + juce::String(pb.getNumChannels())
                   + " peak0=" + juce::String(pb.getNumSamples() > 0 ? pb.getMagnitude(0, 0, pb.getNumSamples()) : -1.0f));
    }
    return true;
}

void AtsAudioProcessor::transportCacheStore(int bufferIndex, int sourceTag, const ats::SynthParams& core,
                                            bool liveSmear, bool smearIsolate,
                                            const juce::AudioBuffer<float>& buffer)
{
    if (bufferIndex < 0 || bufferIndex >= (int) transportRenderCache.size())
        return;

    auto& e = transportRenderCache[(size_t) bufferIndex];
    e.valid = true;
    e.sourceTag = sourceTag;
    e.modelRevisionSnap = modelRevision.load(std::memory_order_acquire);
    e.morphSourcesRevisionSnap = morphSourcesRevision.load(std::memory_order_acquire);
    e.displayShapeRevisionSnap = displayShapeRevision.load(std::memory_order_acquire);
    e.hostSr = hostSampleRate;
    e.core = core;
    e.liveSmear = liveSmear;
    e.smearIsolate = smearIsolate;
    e.smearHash = (liveSmear || smearIsolate) ? computeSmearLiveHash() : 0;
    e.smearOverlayRevisionSnap = smearnovOverlayRevision.load(std::memory_order_acquire);
    e.buffer.makeCopyOf(buffer);
}

bool AtsAudioProcessor::renderCurrentModelToPlaybackBuffer(int bufferIndex, juce::String& error, bool liveSmear, bool smearIsolate)
{
    error.clear();
    if (bufferIndex < 0 || bufferIndex >= (int) playbackBuffers.size())
    {
        error = "Invalid playback buffer.";
        return false;
    }

    constexpr int kCurrentModelSourceTag = 100;
    {
        const ats::SynthParams cacheCore = readCoreSynthParamsFromApvts();
        if (transportCacheTryReuse(bufferIndex, kCurrentModelSourceTag, cacheCore, liveSmear, smearIsolate))
            return true;
    }

    juce::AudioBuffer<float> monoP;
    juce::AudioBuffer<float> monoR;
    juce::AudioBuffer<float> monoC;
    int modelSr = 44100;
    const ats::SynthParams core = readCoreSynthParamsFromApvts();

    ClassicLayerRender rendered;
    bool renderedLive = false;
    if (smearIsolate)
    {
        juce::String smearErr;
        const ATS_SOUND* smeared = getSmearedSound(AtsTraceSource::Current, smearErr);
        if (smeared == nullptr)
        {
            error = smearErr.isNotEmpty()
                        ? smearErr
                        : "Smear has no audible effect with the current settings (raise Blur or detect onsets first).";
            return false;
        }
        const ClassicLayerRender wet = renderClassicLayers(smeared, core); // private copy: no modelLock needed
        ClassicLayerRender dry;
        {
            const juce::ScopedLock ml(modelLock);
            if (analyzedModel.get() == nullptr)
            {
                error = "No analyzed ATS model to render.";
                return false;
            }
            dry = renderClassicLayers(analyzedModel.get(), core);
        }
        rendered = subtractClassicLayers(wet, dry);
        renderedLive = true;
    }
    else if (liveSmear)
    {
        juce::String smearErr;
        if (const ATS_SOUND* smeared = getSmearedSound(AtsTraceSource::Current, smearErr))
        {
            rendered = renderClassicLayers(smeared, core); // private copy: no modelLock needed
            renderedLive = true;
        }
    }
    if (!renderedLive)
    {
        const juce::ScopedLock ml(modelLock);
        if (analyzedModel.get() == nullptr)
        {
            error = "No analyzed ATS model to render.";
            return false;
        }
        rendered = renderClassicLayers(analyzedModel.get(), core);
    }
    {
        modelSr = rendered.modelSampleRate;
        monoP = std::move(rendered.partials);
        monoR = std::move(rendered.residual);
        monoC = std::move(rendered.combined);
    }

    const int nP = monoP.getNumSamples();
    const int nR = monoR.getNumSamples();
    const int nC = monoC.getNumSamples();
    if (nP <= 0 && nR <= 0 && nC <= 0)
    {
        error = "Synthesis produced silence.";
        return false;
    }

    std::vector<float> resampledP;
    std::vector<float> resampledR;
    std::vector<float> resampledC;
    const double renderSampleRate = hostSampleRate > 0.0 ? hostSampleRate : (double) modelSr;
    if (nP > 0)
        ats::resampleLinearMono(monoP.getReadPointer(0), nP, (double) modelSr, resampledP, renderSampleRate);
    if (nR > 0)
        ats::resampleLinearMono(monoR.getReadPointer(0), nR, (double) modelSr, resampledR, renderSampleRate);
    if (nC > 0)
        ats::resampleLinearMono(monoC.getReadPointer(0), nC, (double) modelSr, resampledC, renderSampleRate);

    const size_t outN = juce::jmax(resampledC.size(), juce::jmax(resampledP.size(), resampledR.size()));
    if (outN == 0)
    {
        error = "Synthesis produced silence.";
        return false;
    }

    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();

    // Wait for processBlock to finish reading this buffer before reallocating.
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();

    auto& playback = playbackBuffers[(size_t) bufferIndex];
    playback.setSize(resampledC.empty() ? 2 : 3, (int) outN);
    for (int i = 0; i < (int) outN; ++i)
    {
        const float p = i < (int) resampledP.size() ? resampledP[(size_t) i] : 0.0f;
        const float r = i < (int) resampledR.size() ? resampledR[(size_t) i] : 0.0f;
        playback.setSample(0, i, p);
        playback.setSample(1, i, r);
        if (playback.getNumChannels() > 2)
        {
            const float c = i < (int) resampledC.size() ? resampledC[(size_t) i] : p + r;
            playback.setSample(2, i, c);
        }
    }
    transportCacheStore(bufferIndex, kCurrentModelSourceTag, core, liveSmear, smearIsolate, playback);
    {
        auto peak = [&playback](int ch) {
            if (ch >= playback.getNumChannels()) return -1.0f;
            return playback.getMagnitude(ch, 0, playback.getNumSamples());
        };
        atsPlayLog("render buf=" + juce::String(bufferIndex) + " outN=" + juce::String((int) outN)
                   + " ch=" + juce::String(playback.getNumChannels())
                   + " timeFactor=" + juce::String(core.timeFactor)
                   + " duration=" + juce::String(core.duration)
                   + " freqFactor=" + juce::String(core.frequencyFactor)
                   + " sr=" + juce::String(core.sampleRate)
                   + " peakP=" + juce::String(peak(0)) + " peakR=" + juce::String(peak(1))
                   + " peakC=" + juce::String(peak(2)));
    }
    return true;
}

bool AtsAudioProcessor::renderMorphSourceSlotToPlaybackBuffer(int slotIndex, juce::String& error, bool liveSmear, bool smearIsolate)
{
    error.clear();
    if (slotIndex < 0 || slotIndex > 1)
    {
        error = "Invalid morph source slot.";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    juce::AudioBuffer<float> monoP;
    juce::AudioBuffer<float> monoR;
    juce::AudioBuffer<float> monoC;
    int modelSr = 44100;
    const ats::SynthParams core = readCoreSynthParamsFromApvts();
    const AtsTraceSource src = slotIndex == 0 ? AtsTraceSource::SourceA : AtsTraceSource::SourceB;

    if (transportCacheTryReuse(bufferIndex, slotIndex, core, liveSmear, smearIsolate))
        return true;

    ClassicLayerRender rendered;
    bool renderedLive = false;
    if (smearIsolate)
    {
        juce::String smearErr;
        const ATS_SOUND* smeared = getSmearedSound(src, smearErr);
        if (smeared == nullptr)
        {
            error = smearErr.isNotEmpty()
                        ? smearErr
                        : "Smear has no audible effect with the current settings (raise Blur or detect onsets first).";
            return false;
        }
        const ClassicLayerRender wet = renderClassicLayers(smeared, core); // private copy: no morphRecipeLock needed
        ClassicLayerRender dry;
        {
            const juce::ScopedLock ml(morphRecipeLock);
            auto* sound = morphSourceSounds[(size_t) slotIndex].get();
            if (sound == nullptr)
            {
                error = slotIndex == 0 ? "Source A has no rendered model." : "Source B has no rendered model.";
                return false;
            }
            dry = renderClassicLayers(sound, core);
        }
        rendered = subtractClassicLayers(wet, dry);
        renderedLive = true;
    }
    else if (liveSmear)
    {
        juce::String smearErr;
        if (const ATS_SOUND* smeared = getSmearedSound(src, smearErr))
        {
            rendered = renderClassicLayers(smeared, core); // private copy: no morphRecipeLock needed
            renderedLive = true;
        }
    }
    if (!renderedLive)
    {
        const juce::ScopedLock ml(morphRecipeLock);
        auto* sound = morphSourceSounds[(size_t) slotIndex].get();
        if (sound == nullptr)
        {
            error = slotIndex == 0 ? "Source A has no rendered model." : "Source B has no rendered model.";
            return false;
        }
        rendered = renderClassicLayers(sound, core);
    }
    {
        modelSr = rendered.modelSampleRate;
        monoP = std::move(rendered.partials);
        monoR = std::move(rendered.residual);
        monoC = std::move(rendered.combined);
    }

    const int nP = monoP.getNumSamples();
    const int nR = monoR.getNumSamples();
    const int nC = monoC.getNumSamples();
    if (nP <= 0 && nR <= 0 && nC <= 0)
    {
        error = "Synthesis produced silence.";
        return false;
    }

    std::vector<float> resampledP;
    std::vector<float> resampledR;
    std::vector<float> resampledC;
    const double renderSampleRate = hostSampleRate > 0.0 ? hostSampleRate : (double) modelSr;
    if (nP > 0)
        ats::resampleLinearMono(monoP.getReadPointer(0), nP, (double) modelSr, resampledP, renderSampleRate);
    if (nR > 0)
        ats::resampleLinearMono(monoR.getReadPointer(0), nR, (double) modelSr, resampledR, renderSampleRate);
    if (nC > 0)
        ats::resampleLinearMono(monoC.getReadPointer(0), nC, (double) modelSr, resampledC, renderSampleRate);

    const size_t outN = juce::jmax(resampledC.size(), juce::jmax(resampledP.size(), resampledR.size()));
    if (outN == 0)
    {
        error = "Synthesis produced silence.";
        return false;
    }

    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();

    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();

    auto& playback = playbackBuffers[(size_t) bufferIndex];
    playback.setSize(resampledC.empty() ? 2 : 3, (int) outN);
    for (int i = 0; i < (int) outN; ++i)
    {
        const float p = i < (int) resampledP.size() ? resampledP[(size_t) i] : 0.0f;
        const float r = i < (int) resampledR.size() ? resampledR[(size_t) i] : 0.0f;
        playback.setSample(0, i, p);
        playback.setSample(1, i, r);
        if (playback.getNumChannels() > 2)
        {
            const float c = i < (int) resampledC.size() ? resampledC[(size_t) i] : p + r;
            playback.setSample(2, i, c);
        }
    }
    transportCacheStore(bufferIndex, slotIndex, core, liveSmear, smearIsolate, playback);
    {
        auto peak = [&playback](int ch) {
            if (ch >= playback.getNumChannels()) return -1.0f;
            return playback.getMagnitude(ch, 0, playback.getNumSamples());
        };
        atsPlayLog("slotRender slot=" + juce::String(slotIndex) + " buf=" + juce::String(bufferIndex)
                   + " outN=" + juce::String((int) outN) + " ch=" + juce::String(playback.getNumChannels())
                   + " peakP=" + juce::String(peak(0)) + " peakR=" + juce::String(peak(1))
                   + " peakC=" + juce::String(peak(2)));
    }
    return true;
}

// Helpers for residual band noise synthesis — mirrors the anonymous-namespace
// implementations in AtsMorphRecipe.cpp so both paths produce consistent carriers.
static std::uint32_t morphTrajResidualHash(int bandIndex, int componentIndex)
{
    std::uint32_t x = static_cast<std::uint32_t>(bandIndex + 1) * 0x9e3779b9u;
    x ^= static_cast<std::uint32_t>(componentIndex + 17) * 0x85ebca6bu;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

static double morphTrajResidualUnitHash(int bandIndex, int componentIndex)
{
    return static_cast<double>(morphTrajResidualHash(bandIndex, componentIndex)) / 4294967295.0;
}

// LCG for per-band deterministic noise in renderMorphTrajectoryBuffer
static inline std::uint64_t morphTrajLcgNext(std::uint64_t s) noexcept
{
    return s * 6364136223846793005ULL + 1442695040888963407ULL;
}
static inline double morphTrajLcgDouble(std::uint64_t s) noexcept
{
    return static_cast<double>(s >> 11) / static_cast<double>(1ULL << 53); // [0, 1)
}

ats::BarkSpectralEnvelope buildBarkEnvelopeForFrame(const ATS_SOUND* sound,
                                                    int frame0,
                                                    int frame1,
                                                    double frac)
{
    ats::BarkSpectralEnvelope envelope;
    if (!sound || sound->frq == nullptr || sound->amp == nullptr)
        return envelope;

    std::vector<ats::SpectralEnvelopePoint> points;
    points.reserve(static_cast<size_t>(std::max(0, sound->partials)));
    for (int p = 0; p < sound->partials; ++p)
    {
        if (!sound->frq[p] || !sound->amp[p])
            continue;
        const double fq = sound->frq[p][frame0]
                          + (sound->frq[p][frame1] - sound->frq[p][frame0]) * frac;
        const double am = sound->amp[p][frame0]
                          + (sound->amp[p][frame1] - sound->amp[p][frame0]) * frac;
        points.push_back({ fq, am });
    }

    // True Envelope estimation auto-selects its cepstral order from the partials' Bark spacing,
    // so no per-frame smoothing heuristic is needed here.
    envelope.build(points, ats::BarkSpectralEnvelope::kDefaultBins);
    return envelope;
}

// Cache wrapper around buildBarkEnvelopeForFrame (see MorphEnvelopeCache). Returns a bit-identical
// result to the uncached build: hits are exact (frame indices and frac matched bit-for-bit) and a
// pointer/revision change rotates the relevant slot before any stale envelope can be served. The
// heavy build runs outside the lock; a second check guards against a concurrent cache rotation.
ats::BarkSpectralEnvelope buildBarkEnvelopeCached(MorphEnvelopeCache* cache, bool isSourceA,
                                                  const ATS_SOUND* sound, int frame0, int frame1,
                                                  double frac, uint64_t sourcesRevision)
{
    if (cache == nullptr)
        return buildBarkEnvelopeForFrame(sound, frame0, frame1, frac);

    uint64_t fracBits = 0;
    std::memcpy(&fracBits, &frac, sizeof fracBits);
    const auto key = std::make_tuple(frame0, frame1, fracBits);

    {
        const juce::ScopedLock sl(cache->lock);
        if (cache->revision != sourcesRevision)
        {
            cache->revision = sourcesRevision;
            cache->keyA = cache->keyB = nullptr;
            cache->mapA.clear();
            cache->mapB.clear();
        }
        const ATS_SOUND*& keyPtr = isSourceA ? cache->keyA : cache->keyB;
        auto& map = isSourceA ? cache->mapA : cache->mapB;
        if (keyPtr != sound)
        {
            keyPtr = sound;
            map.clear();
        }
        const auto it = map.find(key);
        if (it != map.end())
            return it->second;
    }

    ats::BarkSpectralEnvelope env = buildBarkEnvelopeForFrame(sound, frame0, frame1, frac);

    {
        const juce::ScopedLock sl(cache->lock);
        if (cache->revision == sourcesRevision)
        {
            const ATS_SOUND* keyPtr = isSourceA ? cache->keyA : cache->keyB;
            auto& map = isSourceA ? cache->mapA : cache->mapB;
            if (keyPtr == sound)
                map.emplace(key, env);
        }
    }
    return env;
}

ats::HybridizedPartialKind toHybridizedPartialKind(ats::MorphCorrespondenceKind kind)
{
    switch (kind)
    {
        case ats::MorphCorrespondenceKind::Matched:
            return ats::HybridizedPartialKind::Matched;
        case ats::MorphCorrespondenceKind::UnmatchedSource:
            return ats::HybridizedPartialKind::UnmatchedSource;
        case ats::MorphCorrespondenceKind::UnmatchedTarget:
            return ats::HybridizedPartialKind::UnmatchedTarget;
    }
    return ats::HybridizedPartialKind::Matched;
}

// Frame-by-frame oscillator-bank morph synthesis.
//
// Performance design:
//   • Timeline points are sanitized ONCE before the loop (not per-sample).
//   • cos/sin are computed once per ATS frame chunk per partial (every ~hopSecs samples),
//     stored as a complex rotation. The hot loop only does 4 multiplies + 2 adds per partial.
//   • Residual band energy is interpolated per-chunk; several jittered carriers per band advance
//     the same way as partials (no per-sample trig), spreading each band's noise across its range.
//   • This is ~100–300x faster than a naive per-sample std::sin() approach.
static juce::AudioBuffer<float> renderMorphTrajectoryBuffer(
    const ATS_SOUND* soundA,
    const ATS_SOUND* soundB,
    const ats::MorphRecipe& recipe,
    const std::vector<AtsMorphTimelinePoint>& timelinePoints,
    double fixedMorphAmount,
    double sampleRate,
    double durationSeconds,
    MorphEnvelopeCache* envCache = nullptr,
    uint64_t sourcesRevision = 0)
{
    const int totalSamples = juce::jmax(1, (int) std::llround(sampleRate * durationSeconds));
    juce::AudioBuffer<float> out(2, totalSamples);
    out.clear();

    if (soundA == nullptr || soundB == nullptr || recipe.correspondenceRecords.empty())
        return out;

    const auto& records = recipe.correspondenceRecords;
    const int nRec = (int) records.size();
    const double hopA = recipe.sourceA.frameHopSeconds;
    const double hopB = recipe.sourceB.frameHopSeconds;
    const int framesA = soundA->frames;
    const int framesB = soundB->frames;
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    const double invSr = 1.0 / sampleRate;

    // Sanitize once; eliminates per-sample vector copy + sort inside evaluateMorphTimeline.
    const auto safePoints = sanitizeMorphTimelinePoints(timelinePoints);
    const bool useTimeline = !timelinePoints.empty();

    // Only Morph-kind breakpoints drive the standard A/B partial blend.
    // Hybrid breakpoints are intentionally excluded; they affect only the Bark
    // envelope transfer path and must not cause partial/frequency blending.
    auto fastMorphAt = [&](double timeNorm) -> double
    {
        const double t = juce::jlimit(0.0, 1.0, timeNorm);
        const AtsMorphTimelinePoint* prev = nullptr;
        const AtsMorphTimelinePoint* next = nullptr;
        for (const auto& p : safePoints)
        {
            if (p.kind != MorphTimelinePointKind::Morph)
                continue;
            if (p.timeNorm <= t)
                prev = &p;
            else if (next == nullptr)
                next = &p;
        }
        if (prev == nullptr && next == nullptr) return 0.0;
        if (prev == nullptr) return next->morphAmount;
        if (next == nullptr) return prev->morphAmount;
        const double span = next->timeNorm - prev->timeNorm;
        if (span <= 1.0e-9) return next->morphAmount;
        return prev->morphAmount + (t - prev->timeNorm) / span * (next->morphAmount - prev->morphAmount);
    };

    // Chunk size = one ATS frame hop. Trig is computed once per chunk per partial.
    const double smallerHop = (hopA > 0.0 && hopB > 0.0) ? std::min(hopA, hopB)
                            : (hopA > 0.0 ? hopA : (hopB > 0.0 ? hopB : invSr));
    const int chunkSamples = juce::jmax(1, (int) std::floor(smallerHop * sampleRate));

    // Onset/time alignment: A and B are otherwise both sampled at the same wall-clock time, so if
    // their principal attacks sit at different times the morph midpoint hears a doubled/smeared
    // attack (two onsets at distinct instants). Shift B's time axis by a single, bounded offset so
    // its principal onset lands on A's. The shift is capped at the recipe's
    // maxOnsetAlignmentOffsetSeconds (a small nudge, not a full time-warp); a cap of 0 disables it.
    // Sustained material is unaffected to the ear by a ~20 ms global shift; only the attacks gain.
    const double onsetOffsetB = [&]() -> double
    {
        const double maxOff = std::max(0.0, recipe.transient.maxOnsetAlignmentOffsetSeconds);
        if (maxOff <= 0.0)
            return 0.0;
        auto principalOnset = [](const std::vector<ats::MorphTransientMarker>& markers) -> double
        {
            double maxStrength = 0.0;
            for (const auto& m : markers)
                if (std::isfinite(m.strength) && m.strength > maxStrength)
                    maxStrength = m.strength;
            if (maxStrength <= 0.0)
                return std::numeric_limits<double>::quiet_NaN();
            // Earliest onset that is at least half as strong as the strongest: the first salient
            // attack, which is what the ear locks onto, rather than a later louder accent.
            const double threshold = 0.5 * maxStrength;
            double best = std::numeric_limits<double>::quiet_NaN();
            for (const auto& m : markers)
                if (std::isfinite(m.timeSeconds) && std::isfinite(m.strength) && m.strength >= threshold)
                    if (std::isnan(best) || m.timeSeconds < best)
                        best = m.timeSeconds;
            return best;
        };
        const double tA = principalOnset(recipe.sourceATransients);
        const double tB = principalOnset(recipe.sourceBTransients);
        if (std::isnan(tA) || std::isnan(tB))
            return 0.0;
        // Read B at (t + offset) so B's onset tB is heard when playback reaches A's onset tA.
        return juce::jlimit(-maxOff, maxOff, tB - tA);
    }();

    // Phasor state per partial: complex unit vector {re, im} = {cos(phase), sin(phase)}.
    // Seeded below from the analysis phase (fallback {1, 0}); updated by complex multiplication.
    struct PartialState { double re = 1.0, im = 0.0, rotRe = 1.0, rotIm = 0.0, amp = 0.0, ampInc = 0.0; bool active = false; };
    std::vector<PartialState> ps(nRec);

    // Seed each partial's starting phase from the ATS analysis phase at the first frame, instead
    // of leaving every partial aligned at phase 0. All-zero start phases make the partials peak
    // simultaneously at t=0 — an unnatural in-phase impulse / buzzy attack — and discard the
    // recorded inter-partial phase relationships. The phasor then free-runs; only the seed comes
    // from analysis. For a matched partial the two sources' phases are blended on the unit circle
    // by the initial morph amount; unmatched partials take their own source's phase.
    {
        const bool hasPhaseA = soundA->pha != nullptr;
        const bool hasPhaseB = soundB->pha != nullptr;
        if (hasPhaseA || hasPhaseB)
        {
            const double morphAmount0 = juce::jlimit(0.0, 1.0,
                useTimeline ? fastMorphAt(0.0) : fixedMorphAmount);

            auto addPhase = [](double& cx, double& cy, const ATS_SOUND* snd, int partial,
                               bool has, double weight)
            {
                if (!has || weight <= 0.0 || partial < 0 || snd == nullptr || partial >= snd->partials
                    || snd->pha == nullptr || snd->pha[partial] == nullptr)
                    return;
                const double ph = snd->pha[partial][0];
                if (!std::isfinite(ph))
                    return;
                cx += weight * std::cos(ph);
                cy += weight * std::sin(ph);
            };

            for (int ri = 0; ri < nRec; ++ri)
            {
                const auto& rec = records[(size_t) ri];
                double cx = 0.0, cy = 0.0;

                switch (rec.kind)
                {
                    case ats::MorphCorrespondenceKind::Matched:
                        addPhase(cx, cy, soundA, rec.sourcePartialIndex, hasPhaseA, 1.0 - morphAmount0);
                        addPhase(cx, cy, soundB, rec.targetPartialIndex, hasPhaseB, morphAmount0);
                        break;
                    case ats::MorphCorrespondenceKind::UnmatchedSource:
                        addPhase(cx, cy, soundA, rec.sourcePartialIndex, hasPhaseA, 1.0);
                        break;
                    case ats::MorphCorrespondenceKind::UnmatchedTarget:
                        addPhase(cx, cy, soundB, rec.targetPartialIndex, hasPhaseB, 1.0);
                        break;
                }

                const double mag = std::sqrt(cx * cx + cy * cy);
                if (mag > 1.0e-9)
                {
                    ps[(size_t) ri].re = cx / mag;
                    ps[(size_t) ri].im = cy / mag;
                }
            }
        }
    }

    // Residual band noise: one phasor per (band × component) with fixed frequency and
    // time-varying amplitude driven by the morphed per-frame band_energy.
    constexpr int kResBands = ATSA_CRITICAL_BANDS;
    const bool hasResidualA = soundA->band_energy != nullptr;
    const bool hasResidualB = soundB->band_energy != nullptr;
    const bool doResidual   = hasResidualA || hasResidualB;

    // One carrier per band at the ATSA band center frequency, amplitude-modulated by
    // piecewise-linear band-limited noise (randi), matching do_synthesis exactly.
    // Effective output RMS per band = amp × RMS(sin) × RMS(randi) = amp/3, so
    // amp = morphed ENG_RMS (no extra scaling needed).
    struct ResState
    {
        double re = 1.0, im = 0.0, rotRe = 1.0, rotIm = 0.0; // carrier phasor
        double randiA1 = 0.0, randiA2 = 0.0;                  // randi interpolation endpoints
        int randiCnt = 0, randiSize = 1;                       // randi counter / period
        std::uint64_t randSeed = 1;                            // LCG state for next randi value
        double amp = 0.0, ampInc = 0.0;
        bool active = false;
        bool usable = false;
    };

    // Multiple carriers per critical band, each at a distinct (jittered) frequency inside the
    // band and driven by independent band-limited noise. A single band-center carrier collapses
    // each band's energy into a narrow tone, leaving spectral gaps that read as a watery/phasey
    // texture on sustained residual (pads). Spreading carriers across the band fills it with
    // smoother broadband noise. Mirrors the recipe's residual component layout.
    constexpr int kResCarriers = 5;
    std::vector<ResState> rs;
    std::vector<int> bandUsable;
    if (doResidual)
    {
        rs.resize((size_t) (kResBands * kResCarriers));
        bandUsable.assign((size_t) kResBands, 0);
        for (int band = 0; band < kResBands; ++band)
        {
            const double lo        = ATSA_CRITICAL_BAND_EDGES[band];
            const double hi        = ATSA_CRITICAL_BAND_EDGES[band + 1];
            const double bandwidth = juce::jmax(1.0, hi - lo);
            // Each carrier owns ~one sub-band; its modulator bandwidth matches so neighbouring
            // carriers overlap into continuous coverage rather than discrete tones.
            const double subBw = bandwidth / (double) kResCarriers;
            for (int c = 0; c < kResCarriers; ++c)
            {
                auto& r = rs[(size_t) (band * kResCarriers + c)];

                const double slot   = ((double) c + 0.5) / (double) kResCarriers;
                const double jitter = (morphTrajResidualUnitHash(band, c) - 0.5) / (double) kResCarriers;
                const double amount = juce::jlimit(0.05, 0.95, slot + jitter);
                const double freqHz = lo + amount * (hi - lo);
                if (freqHz <= 0.0 || freqHz >= sampleRate * 0.5)
                    continue;

                const double omega = twoPi * freqHz * invSr;
                r.rotRe = std::cos(omega);
                r.rotIm = std::sin(omega);

                const double phaseStart = morphTrajResidualUnitHash(band, c + 101) * twoPi;
                r.re = std::cos(phaseStart);
                r.im = std::sin(phaseStart);

                r.randiSize = juce::jmax(1, (int) std::round(sampleRate / juce::jmax(1.0, subBw)));

                // Deterministic per-(band,carrier) seed for reproducible playback
                std::uint64_t seed = static_cast<std::uint64_t>(morphTrajResidualHash(band, c))
                                     * 2654435761ULL + 1234567891ULL;
                r.randSeed = (seed == 0) ? 1 : seed;
                r.randSeed  = morphTrajLcgNext(r.randSeed);
                r.randiA1   = morphTrajLcgDouble(r.randSeed) * 2.0 - 1.0;
                r.randSeed  = morphTrajLcgNext(r.randSeed);
                r.randiA2   = morphTrajLcgDouble(r.randSeed) * 2.0 - 1.0;
                r.randiCnt  = 0;
                r.usable    = true;
                ++bandUsable[(size_t) band];
            }
        }
    }

    // Ceiling on hybrid envelope-transfer boost. The de-color/re-color ratio (barkB/barkA)
    // grows without bound for partials sitting in a spectral valley of source A — and as a
    // source decays toward its end its high partials shrink, so barkA shrinks and the boost
    // runs away into a rising high-frequency whistle. +30 dB is generous for genuine timbre
    // transfer while bounding the noise-floor runaway.
    constexpr double kMaxEnvBoostLinear = 31.6227766; // +30 dB

    // De-color divisor floor, relative to each source-A envelope's own peak. Where source A
    // has no real spectral support (e.g. its high partials have died near the end of the
    // sound), barkA edge-holds a tiny value and barkB/barkA runs away into a high-frequency
    // whistle/chirp. Flooring envA at -48 dB below A's peak bounds the de-color gain to A's
    // intrinsic dynamic range and keeps it stable frame-to-frame, so unsupported high bands
    // are not synthesised out of the noise floor.
    constexpr double kEnvDecolorRelFloor = 0.00398107; // -48 dB

    // Active partials/carriers are fixed for the duration of a chunk (set in per-chunk setup).
    // Collect their indices once per chunk so the per-sample hot loop iterates only live
    // entries instead of branching on `active` for every dead partial on every sample.
    std::vector<int> activePartials;
    activePartials.reserve((size_t) nRec);
    std::vector<int> activeCarriers;
    activeCarriers.reserve(rs.size());

    // Per-frame (time-varying) correspondence. The static records pair A and B partials by their
    // lifetime-average position, so a partial that glides and crosses another between frames stays
    // wrongly paired for its whole life. Here the matched pairs within each critical band are
    // re-assigned every chunk to the partials that are actually closest in pitch at that instant.
    // Phase continuity is preserved because each oscillator stays anchored to its A partial (the
    // record's sourcePartialIndex); only its B partner — which the blend glides toward — is
    // re-chosen, and swaps happen at frequency crossings where the two B partners nearly coincide,
    // so freqB barely steps. Group the matched records by band once (record identity is static).
    std::vector<std::vector<int>> matchedBandGroups; // each: record indices sharing a band (size>=2)
    {
        std::vector<std::vector<int>> byBand;
        std::vector<int> bandOf; // parallel: which band each group represents
        for (int ri = 0; ri < nRec; ++ri)
        {
            const auto& rec = records[(size_t) ri];
            if (rec.kind != ats::MorphCorrespondenceKind::Matched)
                continue;
            int g = -1;
            for (size_t k = 0; k < bandOf.size(); ++k)
                if (bandOf[k] == rec.bandIndex) { g = (int) k; break; }
            if (g < 0) { bandOf.push_back(rec.bandIndex); byBand.emplace_back(); g = (int) byBand.size() - 1; }
            byBand[(size_t) g].push_back(ri);
        }
        for (auto& grp : byBand)
            if (grp.size() >= 2)
                matchedBandGroups.push_back(std::move(grp));
    }
    // assignedTargetPartial[ri] is the B partial the matched record currently blends toward; it
    // starts at the static pairing and is updated per chunk by the re-assignment below.
    std::vector<int> assignedTargetPartial((size_t) nRec, -1);
    for (int ri = 0; ri < nRec; ++ri)
        if (records[(size_t) ri].kind == ats::MorphCorrespondenceKind::Matched)
            assignedTargetPartial[(size_t) ri] = records[(size_t) ri].targetPartialIndex;
    // Reused scratch so the chunk loop does not allocate.
    std::vector<double> reSrcLog, reTgtLog;
    std::vector<int> rePrev, reTargets;
    constexpr double kRepairHysteresisLogHz = 0.02; // ~0.35 semitone stickiness against flicker

    for (int s = 0; s < totalSamples; )
    {
        const int chunkEnd = juce::jmin(s + chunkSamples, totalSamples);
        const int chunkLen = juce::jmax(1, chunkEnd - s);
        const double t = (double) s * invSr;
        const double timeNorm = totalSamples > 1 ? (double) s / (double) (totalSamples - 1) : 0.0;

        const double morphAmount = juce::jlimit(0.0, 1.0,
            useTimeline ? fastMorphAt(timeNorm) : fixedMorphAmount);

        const double envTransferAmount = juce::jlimit(0.0, 1.0,
            useTimeline ? evaluateEnvTransferTimeline(safePoints, timeNorm) : 0.0);

        // Envelope-driven amplitude morph ("true morph"): blend A/B Bark envelopes and
        // resample each partial's amplitude from the interpolated envelope.
        const double envMorphAmount = juce::jlimit(0.0, 1.0,
            useTimeline ? evaluateLaneTimeline(safePoints, timeNorm,
                                               MorphTimelinePointKind::EnvelopeMorph)
                        : 0.0);

        // Fractional frame positions at this chunk's start
        const double fpA = hopA > 0.0 ? t / hopA : 0.0;
        const int fA0   = juce::jlimit(0, framesA - 1, (int) fpA);
        const int fA1   = juce::jmin(fA0 + 1, framesA - 1);
        const double fracA = fpA - (double) fA0;

        const double tB = std::max(0.0, t + onsetOffsetB); // bounded onset-aligned time for B
        const double fpB = hopB > 0.0 ? tB / hopB : 0.0;
        const int fB0   = juce::jlimit(0, framesB - 1, (int) fpB);
        const int fB1   = juce::jmin(fB0 + 1, framesB - 1);
        const double fracB = fpB - (double) fB0;

        // End-of-source fade. When a source overruns its last frame, fA0==fA1 and its partials
        // would otherwise hold the last-frame value forever, so each source is faded to silence
        // past its end. The fade is a raised cosine (zero slope at both ends) over ~20 ms rather
        // than a single linear hop: a source of finite length is usually truncated while still
        // energetic, and a linear one-hop ramp leaves slope corners that splatter as a broadband
        // "pop" when the other source is still sounding (any morph < 1). The smooth, longer
        // window removes the corners. Applied symmetrically to A and B.
        constexpr double kEndFadeSeconds = 0.020;
        auto endOfSourceFade = [&](double fp, int frames, double hop) -> double
        {
            const double overSec = (fp - (double) (frames - 1)) * hop; // seconds past last frame
            if (overSec <= 0.0)
                return 1.0;
            const double dur = std::max(hop, kEndFadeSeconds);
            if (overSec >= dur)
                return 0.0;
            return 0.5 * (1.0 + std::cos(juce::MathConstants<double>::pi * (overSec / dur)));
        };
        const double fadeA = endOfSourceFade(fpA, framesA, hopA > 0.0 ? hopA : invSr);
        const double fadeB = endOfSourceFade(fpB, framesB, hopB > 0.0 ? hopB : invSr);

        // Scale transfer amount by fadeA: as source A ends, the spectral gain exponent
        // approaches 0 so boosted partials fade naturally instead of cutting abruptly.
        const double effectiveEnvTransfer = envTransferAmount * fadeA;

        // Build Bark envelopes once per chunk — shared by Hybrid envelope transfer and the
        // EnvelopeMorph lane (both read A's and B's spectral envelope at partial frequencies).
        ats::BarkSpectralEnvelope barkA, barkB;
        if (effectiveEnvTransfer > 1.0e-9 || envMorphAmount > 1.0e-9)
        {
            barkA = buildBarkEnvelopeCached(envCache, true, soundA, fA0, fA1, fracA, sourcesRevision);
            barkB = buildBarkEnvelopeCached(envCache, false, soundB, fB0, fB1, fracB, sourcesRevision);
        }

        // For the EnvelopeMorph lane, align the two envelopes' formants and warp the Bark axis
        // so corresponding formants glide together before the log-amplitude blend (Caetano/Rodet).
        // Built once per chunk and read per partial below.
        ats::FormantAlignedEnvelopeMorph morphedEnv;
        if (envMorphAmount > 1.0e-9 && barkA.isValid() && barkB.isValid())
            morphedEnv.build(barkA, barkB, kEnvDecolorRelFloor);

        // Per-frame correspondence: re-pair matched partials within each band by their
        // instantaneous log-frequency at this chunk (see the precompute comment above).
        {
            auto instLogFreq = [](const ATS_SOUND* snd, int p, int f0, int f1, double frac) -> double
            {
                if (p < 0 || snd->frq == nullptr || p >= snd->partials || snd->frq[p] == nullptr)
                    return std::numeric_limits<double>::quiet_NaN();
                const double a = snd->frq[p][f0], b = snd->frq[p][f1];
                if (!std::isfinite(a) || !std::isfinite(b) || a <= 0.0 || b <= 0.0)
                    return std::numeric_limits<double>::quiet_NaN();
                return std::log(a) + (std::log(b) - std::log(a)) * frac;
            };
            for (const auto& group : matchedBandGroups)
            {
                const size_t k = group.size();
                reSrcLog.assign(k, 0.0);
                reTgtLog.assign(k, 0.0);
                reTargets.assign(k, -1);
                rePrev.assign(k, 0);
                for (size_t i = 0; i < k; ++i)
                {
                    const auto& rec = records[(size_t) group[i]];
                    reSrcLog[i] = instLogFreq(soundA, rec.sourcePartialIndex, fA0, fA1, fracA);
                    reTgtLog[i] = instLogFreq(soundB, rec.targetPartialIndex, fB0, fB1, fracB);
                    reTargets[i] = rec.targetPartialIndex; // static B-partial set for this band
                }
                // Map the current assignment to slice indices for hysteresis.
                for (size_t i = 0; i < k; ++i)
                {
                    const int cur = assignedTargetPartial[(size_t) group[i]];
                    int slice = (int) i;
                    for (size_t j = 0; j < k; ++j)
                        if (reTargets[j] == cur) { slice = (int) j; break; }
                    rePrev[i] = slice;
                }
                const auto newAssign = ats::reassignCorrespondenceByLogFrequency(
                    reSrcLog, reTgtLog, rePrev, kRepairHysteresisLogHz);
                for (size_t i = 0; i < k; ++i)
                    assignedTargetPartial[(size_t) group[i]] = reTargets[(size_t) newAssign[i]];
            }
        }

        // Per-chunk setup: compute freq/amp and the phasor rotation (cos/sin once per chunk).
        for (int ri = 0; ri < nRec; ++ri)
        {
            const auto& rec = records[(size_t) ri];
            double freqA = 0.0, ampA = 0.0, freqB = 0.0, ampB = 0.0;

            if (rec.kind != ats::MorphCorrespondenceKind::UnmatchedTarget)
            {
                const int pA = rec.sourcePartialIndex;
                if (pA >= 0 && pA < soundA->partials
                    && soundA->frq != nullptr && soundA->frq[pA] != nullptr
                    && soundA->amp != nullptr && soundA->amp[pA] != nullptr)
                {
                    const double fq0 = soundA->frq[pA][fA0], fq1 = soundA->frq[pA][fA1];
                    const double am0 = soundA->amp[pA][fA0], am1 = soundA->amp[pA][fA1];
                    // Log-frequency interp between adjacent analysis frames: a gliding partial's
                    // perceptual pitch is the geometric mean, so a linear-Hz step combs slightly
                    // sharp at the hop midpoint. Amplitude stays linear (ATS/MQ standard for a
                    // single partial's own envelope between frames).
                    if (std::isfinite(fq0) && std::isfinite(fq1))
                        freqA = (fq0 > 0.0 && fq1 > 0.0)
                                    ? std::exp(std::log(fq0) + (std::log(fq1) - std::log(fq0)) * fracA)
                                    : juce::jmax(0.0, fq0 + (fq1 - fq0) * fracA);
                    if (std::isfinite(am0) && std::isfinite(am1))
                        ampA  = juce::jmax(0.0, am0 + (am1 - am0) * fracA) * fadeA;
                }
            }

            if (rec.kind != ats::MorphCorrespondenceKind::UnmatchedSource)
            {
                const int pB = rec.kind == ats::MorphCorrespondenceKind::Matched
                                   ? assignedTargetPartial[(size_t) ri]
                                   : rec.targetPartialIndex;
                if (pB >= 0 && pB < soundB->partials
                    && soundB->frq != nullptr && soundB->frq[pB] != nullptr
                    && soundB->amp != nullptr && soundB->amp[pB] != nullptr)
                {
                    const double fq0 = soundB->frq[pB][fB0], fq1 = soundB->frq[pB][fB1];
                    const double am0 = soundB->amp[pB][fB0], am1 = soundB->amp[pB][fB1];
                    if (std::isfinite(fq0) && std::isfinite(fq1))
                        freqB = (fq0 > 0.0 && fq1 > 0.0)
                                    ? std::exp(std::log(fq0) + (std::log(fq1) - std::log(fq0)) * fracB)
                                    : juce::jmax(0.0, fq0 + (fq1 - fq0) * fracB);
                    if (std::isfinite(am0) && std::isfinite(am1))
                        ampB  = juce::jmax(0.0, am0 + (am1 - am0) * fracB) * fadeB;
                }
            }

            // Hybrid envelope transfer: de-color A by its envelope, then color it
            // by B's envelope at A's partial frequency.
            if (effectiveEnvTransfer > 1.0e-9 && freqA > 0.0 && ampA > 0.0
                && barkA.isValid() && barkB.isValid())
            {
                const double relFloorA = barkA.getPeakAmplitude() * kEnvDecolorRelFloor;
                const double envA = std::max(relFloorA, barkA.sampleAtHz(freqA));
                const double envB = barkB.sampleAtHz(freqA);
                const auto transfer = ats::applySpectralEnvelopeTransfer(ampA,
                                                                         envA,
                                                                         envB,
                                                                         effectiveEnvTransfer);
                if (transfer.ok)
                    ampA = ats::softCapEnvelopeBoost(transfer.outputAmplitude, ampA,
                                                     kMaxEnvBoostLinear);
            }

            // Unmatched partials cross-fade in place at their own frequency (no glide toward a
            // nearest neighbour): with dissimilar sources that turned the blend into a mass of
            // partials sliding to arbitrary targets — audible downward sweeps and tones present
            // in neither source. The midpoint stays a clean union of the two spectra.
            const double nearestTargetHz = 0.0;
            const double nearestSourceHz = 0.0;
            const auto resolved = ats::resolveHybridizedPartialRender(toHybridizedPartialKind(rec.kind),
                                                                      freqA,
                                                                      ampA,
                                                                      freqB,
                                                                      ampB,
                                                                      morphAmount,
                                                                      nearestTargetHz,
                                                                      nearestSourceHz);
            const double freq = resolved.frequencyHz;
            double amp = resolved.amplitude;

            // Envelope-driven amplitude morph: interpolate A's and B's Bark envelopes by the
            // morph position and resample this partial's amplitude from the blended envelope.
            // This decouples the formant/timbre shape from individual partial amplitudes, so
            // imperfect A↔B correspondence no longer combs the spectrum (Caetano/Rodet). The
            // partial's own presence is preserved: a partial that is already silent stays
            // silent (no resurrection), and the result is bounded by the sources' envelopes.
            if (envMorphAmount > 1.0e-9 && freq > 0.0 && amp > 1.0e-7 && morphedEnv.isValid())
            {
                // Formant-aligned blend: the envelopes' formant peaks are matched and the Bark
                // axis is warped so a formant that moves from A to B glides as a single peak
                // rather than double-imaging or dipping at the midpoint. Reduces to the plain
                // fixed-frequency log blend when no formants match (identity warp).
                const double blendedEnv = morphedEnv.sampleAtHz(freq, morphAmount);
                // Crossfade the independent per-partial amplitude toward its position on the
                // blended envelope. Geometric (dB-domain) interpolation when both endpoints are
                // audible: loudness is logarithmic, so a linear-amplitude crossfade biases toward
                // the louder endpoint and dips perceived level mid-sweep. The geometric blend is
                // still bounded by [min,max] of the two values. Falls back to linear when the
                // envelope sample is at/below the floor so the result stays zero-safe.
                constexpr double kEnvMorphGeomFloor = 1.0e-7;
                if (blendedEnv > kEnvMorphGeomFloor)
                    amp = std::exp((1.0 - envMorphAmount) * std::log(amp)
                                   + envMorphAmount * std::log(blendedEnv));
                else
                    amp += envMorphAmount * (blendedEnv - amp);
            }

            auto& p = ps[(size_t) ri];
            double targetAmp = 0.0;
            if (freq > 0.0 && amp > 0.0)
            {
                const double omega = twoPi * freq * invSr;
                p.rotRe = std::cos(omega); // only trig call — once per chunk per partial
                p.rotIm = std::sin(omega);
                targetAmp = amp;
            }
            // When targetAmp == 0 (partial dying) the previous rotor is kept so the
            // ramp-to-zero tail holds its last frequency — no pitch/phase discontinuity.

            // Linearly ramp amplitude across the chunk toward the target. This turns every
            // inter-chunk amplitude step — including the end-of-source fade to zero, which
            // otherwise cliffs in a single sample at the chunk boundary — into a click-free
            // glide. A partial stays active while it still has amplitude to ramp down.
            p.ampInc  = (targetAmp - p.amp) / (double) chunkLen;
            p.active  = (p.amp > 1.0e-7) || (targetAmp > 1.0e-7);
        }

        // Per-chunk residual setup: compute ENG_RMS per source via amplitude-space interpolation
        // (matching do_synthesis LIN3_INTERP-in-amplitude behavior).  The /3 level factor is
        // implicit: carrier × randi gives output RMS = ENG_RMS × (1/√2) × (√2/3) = ENG_RMS/3.
        if (doResidual)
        {
            const double wsA = hasResidualA ? (double) juce::jmax(1, soundA->window_size) : 1.0;
            const double wsB = hasResidualB ? (double) juce::jmax(1, soundB->window_size) : 1.0;
            constexpr double kNoiseVar = (double) ATSA_NOISE_VARIANCE;

            for (int band = 0; band < kResBands; ++band)
            {
                double rmsA = 0.0;
                if (hasResidualA && soundA->band_energy[band] != nullptr)
                {
                    const double e0 = soundA->band_energy[band][fA0];
                    const double e1 = soundA->band_energy[band][fA1];
                    if (std::isfinite(e0) && std::isfinite(e1))
                    {
                        // Interpolate in amplitude space (not energy space) to match do_synthesis
                        const double r0 = std::sqrt(juce::jmax(0.0, e0) / (wsA * kNoiseVar));
                        const double r1 = std::sqrt(juce::jmax(0.0, e1) / (wsA * kNoiseVar));
                        rmsA = (r0 + (r1 - r0) * fracA) * fadeA;
                    }
                }
                double rmsB = 0.0;
                if (hasResidualB && soundB->band_energy[band] != nullptr)
                {
                    const double e0 = soundB->band_energy[band][fB0];
                    const double e1 = soundB->band_energy[band][fB1];
                    if (std::isfinite(e0) && std::isfinite(e1))
                    {
                        const double r0 = std::sqrt(juce::jmax(0.0, e0) / (wsB * kNoiseVar));
                        const double r1 = std::sqrt(juce::jmax(0.0, e1) / (wsB * kNoiseVar));
                        rmsB = (r0 + (r1 - r0) * fracB) * fadeB;
                    }
                }
                // Apply Bark envelope transfer to residual noise — same Hybrid lane
                // that colors the deterministic partials, now extended to stochastic bands.
                static constexpr double kAtsBandCenterHz[ATSA_CRITICAL_BANDS] = {
                    50.0, 150.0, 250.0, 350.0, 455.0, 570.0, 700.0, 845.0, 1000.0,
                    1175.0, 1375.0, 1600.0, 1860.0, 2160.0, 2510.0, 2925.0, 3425.0,
                    4050.0, 4850.0, 5850.0, 7050.0, 8600.0, 10750.0, 13750.0, 17750.0
                };
                if (effectiveEnvTransfer > 1.0e-9 && rmsA > 0.0
                    && barkA.isValid() && barkB.isValid())
                {
                    const double centerHz = kAtsBandCenterHz[band];
                    const double relFloorA = barkA.getPeakAmplitude() * kEnvDecolorRelFloor;
                    const double envA = std::max(relFloorA, barkA.sampleAtHz(centerHz));
                    const double envB = barkB.sampleAtHz(centerHz);
                    const auto xfr = ats::applySpectralEnvelopeTransfer(rmsA, envA, envB,
                                                                        effectiveEnvTransfer);
                    if (xfr.ok)
                        rmsA = ats::softCapEnvelopeBoost(xfr.outputAmplitude, rmsA,
                                                         kMaxEnvBoostLinear);
                }
                // Equal-power (constant-energy) blend: the two bands are uncorrelated noise,
                // so their powers add. Interpolating RMS linearly dips ~3 dB at the midpoint
                // (the classic equal-gain crossfade hole); blending in the power domain keeps
                // the perceived loudness constant across the morph.
                const double morphedRms = std::sqrt(juce::jmax(0.0,
                    (1.0 - morphAmount) * rmsA * rmsA + morphAmount * rmsB * rmsB));
                // Split the band's energy across its carriers: with N uncorrelated carriers the
                // powers add, so amp/sqrt(N) per carrier preserves the band's total RMS.
                const int nUsable = bandUsable[(size_t) band];
                const double carrierTarget = nUsable > 0
                    ? morphedRms / std::sqrt((double) nUsable) : 0.0;
                for (int c = 0; c < kResCarriers; ++c)
                {
                    auto& rState = rs[(size_t) (band * kResCarriers + c)];
                    if (!rState.usable) { rState.active = false; continue; }
                    rState.ampInc = (carrierTarget - rState.amp) / (double) chunkLen;
                    rState.active = (rState.amp > 1.0e-9) || (carrierTarget > 1.0e-9);
                }
            }
        }

        // Collect this chunk's live entries so the per-sample loop skips dead partials/carriers
        // without a per-iteration branch (active state is constant across the chunk).
        activePartials.clear();
        for (int ri = 0; ri < nRec; ++ri)
            if (ps[(size_t) ri].active)
                activePartials.push_back(ri);

        activeCarriers.clear();
        if (doResidual)
            for (int idx = 0; idx < (int) rs.size(); ++idx)
                if (rs[(size_t) idx].active)
                    activeCarriers.push_back(idx);

        // Hot loop: phasor multiply only — no trig, no timeline lookup.
        // Write through cached channel pointers; out.setSample() per sample
        // re-fetches the channel pointer and bounds-checks on every call.
        float* const outL = out.getWritePointer(0);
        float* const outR = out.getWritePointer(1);
        const int nActiveP = (int) activePartials.size();
        const int nActiveR = (int) activeCarriers.size();
        const int* const activeP = activePartials.data();
        const int* const activeR = activeCarriers.data();
        for (int si = s; si < chunkEnd; ++si)
        {
            float sample = 0.0f;
            for (int ai = 0; ai < nActiveP; ++ai)
            {
                auto& p = ps[(size_t) activeP[ai]];
                const double newRe = p.re * p.rotRe - p.im * p.rotIm;
                const double newIm = p.re * p.rotIm + p.im * p.rotRe;
                p.re = newRe;
                p.im = newIm;
                sample += (float) (newIm * p.amp);
                p.amp += p.ampInc;
                if (p.amp < 0.0) p.amp = 0.0;
            }
            outL[si] = sample;

            if (doResidual)
            {
                float resSample = 0.0f;
                for (int ai = 0; ai < nActiveR; ++ai)
                {
                    auto& r = rs[(size_t) activeR[ai]];

                    // Advance carrier phasor
                    const double newRe = r.re * r.rotRe - r.im * r.rotIm;
                    const double newIm = r.re * r.rotIm + r.im * r.rotRe;
                    r.re = newRe;
                    r.im = newIm;

                    // Advance randi: piecewise-linear noise in [-1, 1] at bandwidth rate
                    if (r.randiCnt >= r.randiSize)
                    {
                        r.randiA1  = r.randiA2;
                        r.randSeed = morphTrajLcgNext(r.randSeed);
                        r.randiA2  = morphTrajLcgDouble(r.randSeed) * 2.0 - 1.0;
                        r.randiCnt = 0;
                    }
                    const double noiseVal = r.randiA1
                                           + (r.randiA2 - r.randiA1)
                                               * ((double) r.randiCnt / (double) r.randiSize);
                    ++r.randiCnt;

                    // sin(carrier) × noise × ENG_RMS — matches do_synthesis ioscilator × randi
                    resSample += (float) (newIm * noiseVal * r.amp);
                    r.amp += r.ampInc;
                    if (r.amp < 0.0) r.amp = 0.0;
                }
                outR[si] = resSample;
            }
        }

        s = chunkEnd;
    }

    return out;
}

bool AtsAudioProcessor::playMorphPreview(juce::String& error)
{
    error.clear();
    if (isWorkerBusy())
    {
        error = "Wait for the current analysis or render to finish before playing the morph.";
        return false;
    }

    ats::MorphRecipe recipe;
    std::array<std::vector<ats::MorphResidualNoiseBand>, 2> residualBands;
    const ATS_SOUND* sA = nullptr;
    const ATS_SOUND* sB = nullptr;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        syncMorphControlsToRecipe(apvts, morphRecipe);
        rebuildMorphCorrespondenceIfReady();
        serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        recipe = morphRecipe;
        residualBands = morphSourceResidualBands;
        sA = morphSourceSounds[0].get();
        sB = morphSourceSounds[1].get();
    }

    if (recipe.sourceA.partialCount <= 0 || recipe.sourceB.partialCount <= 0)
    {
        error = "Analyze Source A and Source B before playing the morph.";
        return false;
    }
    if (recipe.correspondenceRecords.empty())
    {
        error = "No morph correspondences are ready yet. Re-analyze Source A and Source B to prepare morph playback.";
        return false;
    }

    const double morphAmount = (double) getRawParamValue(apvts, ats_param::kMorphAmount, 0.0f);
    const double duration = juce::jmax(0.25, juce::jmax(recipe.sourceA.durationSeconds, recipe.sourceB.durationSeconds));
    const double renderSampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    const auto frequencyShape = readFrequencyShapeFromApvts();

    if (sA != nullptr && sB != nullptr)
    {
        // Frame-by-frame oscillator bank synthesis through actual ATS per-frame data.
        ats::AtsSoundPtr shapedA;
        ats::AtsSoundPtr shapedB;
        const ATS_SOUND* renderA = makeFrequencyShapedRenderSound(sA, frequencyShape, shapedA);
        const ATS_SOUND* renderB = makeFrequencyShapedRenderSound(sB, frequencyShape, shapedB);
        auto rendered = renderMorphTrajectoryBuffer(renderA, renderB, recipe, {}, morphAmount, renderSampleRate, duration,
                                                    morphEnvelopeCache.get(),
                                                    morphSourcesRevision.load(std::memory_order_acquire));
        const int totalSamples = rendered.getNumSamples();
        if (totalSamples <= 0)
        {
            error = "Morph trajectory produced no audio.";
            return false;
        }

        if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
            stopPreviewPlayback();
        while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
            std::this_thread::yield();

        playbackBuffers[(size_t) kMorphPlaybackBufferIndex].makeCopyOf(rendered);
        activePlaybackBufferIndex.store(kMorphPlaybackBufferIndex, std::memory_order_release);
        previewAuditionMode.store((int) AtsSourceAuditionMode::Both, std::memory_order_release);
        previewPlaying.store(false, std::memory_order_release);
        morphSnapshotState.store((int) AtsMorphSnapshotUiState::Ready, std::memory_order_release);
        previewPlaybackKind.store((int) AtsPreviewPlaybackKind::MorphPreview, std::memory_order_release);
    }
    else
    {
        // No raw frame data (e.g. after preset load): fall back to static snapshot.
        ats::MorphSnapshotOptions options;
        options.morphAmount = (float) morphAmount;
        options.frequency.domain =
            getRawChoiceValue(apvts, ats_param::kMorphFrequencyInterpolationDomain, kDefaultMorphFrequencyDomain) == 1
                ? ats::MorphFrequencyInterpolationDomain::BarkCoordinate
                : ats::MorphFrequencyInterpolationDomain::LogFrequency;
        options.amplitude.domain =
            getRawChoiceValue(apvts, ats_param::kMorphAmplitudeInterpolationDomain, kDefaultMorphAmplitudeDomain) == 1
                ? ats::MorphAmplitudeInterpolationDomain::Decibels
                : ats::MorphAmplitudeInterpolationDomain::Power;
        options.sourceResidualNoiseBands = residualBands[0];
        options.targetResidualNoiseBands = residualBands[1];

        const auto snapshot = ats::evaluateMorphRecipeSnapshot(recipe, options);
        if (!installPreparedMorphSnapshotForPlayback(snapshot, duration, error))
            return false;
    }

    startPreviewPlayback();
    return true;
}

bool AtsAudioProcessor::playMorphTimelinePreview(juce::String& error)
{
    return playMorphTimelinePreview(AtsSourceAuditionMode::Both, false, error);
}

bool AtsAudioProcessor::playMorphTimelinePreview(AtsSourceAuditionMode mode, juce::String& error)
{
    return playMorphTimelinePreview(mode, false, error);
}

bool AtsAudioProcessor::playMorphTimelinePreview(AtsSourceAuditionMode mode, bool liveSmear, juce::String& error)
{
    error.clear();
    if (isWorkerBusy())
    {
        error = "Wait for the current analysis or render to finish before playing the morph timeline.";
        return false;
    }

    ats::MorphRecipe recipe;
    std::array<std::vector<ats::MorphResidualNoiseBand>, 2> residualBands;
    std::vector<AtsMorphTimelinePoint> timelinePoints;
    const ATS_SOUND* sA = nullptr;
    const ATS_SOUND* sB = nullptr;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        syncMorphControlsToRecipe(apvts, morphRecipe);
        rebuildMorphCorrespondenceIfReady();
        timelinePoints = deserializeMorphTimelinePoints(getRecipeUnknownField(morphRecipe,
                                                                              kMorphTimelineUnknownField));
        upsertRecipeUnknownField(morphRecipe,
                                 kMorphTimelineUnknownField,
                                 serializeMorphTimelinePoints(timelinePoints));
        serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        recipe = morphRecipe;
        residualBands = morphSourceResidualBands;
        sA = morphSourceSounds[0].get();
        sB = morphSourceSounds[1].get();
    }

    if (recipe.sourceA.partialCount <= 0 || recipe.sourceB.partialCount <= 0)
    {
        error = "Analyze Source A and Source B before playing the morph timeline.";
        return false;
    }
    if (recipe.correspondenceRecords.empty())
    {
        error = "No morph correspondences are ready yet. Re-analyze Source A and Source B to prepare morph playback.";
        return false;
    }

    ats::MorphSnapshotOptions options;
    options.frequency.domain =
        getRawChoiceValue(apvts, ats_param::kMorphFrequencyInterpolationDomain, kDefaultMorphFrequencyDomain) == 1
            ? ats::MorphFrequencyInterpolationDomain::BarkCoordinate
            : ats::MorphFrequencyInterpolationDomain::LogFrequency;
    options.amplitude.domain =
        getRawChoiceValue(apvts, ats_param::kMorphAmplitudeInterpolationDomain, kDefaultMorphAmplitudeDomain) == 1
            ? ats::MorphAmplitudeInterpolationDomain::Decibels
            : ats::MorphAmplitudeInterpolationDomain::Power;
    options.sourceResidualNoiseBands = residualBands[0];
    options.targetResidualNoiseBands = residualBands[1];

    joinWorkers();

    ats::SynthParams core = readCoreSynthParamsFromApvts();
    const double renderSampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    const double duration = juce::jmax(0.25, juce::jmax(recipe.sourceA.durationSeconds,
                                                        recipe.sourceB.durationSeconds));
    const int strategy = getRawChoiceValue(apvts,
                                           ats_param::kMorphCorrespondenceStrategy,
                                           kDefaultMorphCorrespondenceStrategy);
    const int quality = getRawChoiceValue(apvts, ats_param::kMorphQuality, kDefaultMorphQuality);
    const double minMassFraction = morphQualityToMinMassFraction(quality);
    const uint64_t gen = jobGeneration.load(std::memory_order_acquire);
    morphSnapshotState.store((int) AtsMorphSnapshotUiState::Rebuilding, std::memory_order_release);
    transformThreadRunning.store(true, std::memory_order_release);

    transformThread = std::thread([this, recipe, timelinePoints, options, sA, sB, core, renderSampleRate, duration, gen, mode, liveSmear, strategy, quality, minMassFraction]() mutable {
        try
        {
            juce::AudioBuffer<float> timelineBuffer;

            // Live (non-destructive) smear: swap in smeared clones of both sources and rebuild
            // the morph correspondence on that geometry. The clones come from the A/B smear
            // caches (one-time detect+smear); the correspondence is cached separately so a
            // repeated Timeline play with unchanged sources/params skips the heavy rebuild.
            if (liveSmear && sA != nullptr && sB != nullptr)
            {
                juce::String smearErr;
                const ATS_SOUND* cloneA = getSmearedSound(AtsTraceSource::SourceA, smearErr);
                const ATS_SOUND* cloneB = getSmearedSound(AtsTraceSource::SourceB, smearErr);
                if (cloneA != nullptr || cloneB != nullptr)
                {
                    const ATS_SOUND* useA = cloneA != nullptr ? cloneA : sA;
                    const ATS_SOUND* useB = cloneB != nullptr ? cloneB : sB;

                    const uint64_t rev = morphSourcesRevision.load(std::memory_order_acquire);
                    const size_t ph = computeSmearLiveHash();

                    std::vector<ats::MorphCorrespondenceRecord> records;
                    {
                        const juce::ScopedLock cl(smearedCacheLock);
                        if (smearedTimeline.valid && smearedTimeline.revA == rev && smearedTimeline.revB == rev
                            && smearedTimeline.paramHash == ph && smearedTimeline.strategy == strategy
                            && smearedTimeline.quality == quality)
                            records = smearedTimeline.records;
                    }
                    if (records.empty())
                    {
                        const auto asgA = makeMorphAssignmentsFromSound(useA, recipe.frequencyGrid);
                        const auto asgB = makeMorphAssignmentsFromSound(useB, recipe.frequencyGrid);
                        records = buildMorphCorrespondence(asgA, asgB, strategy, minMassFraction);
                        const juce::ScopedLock cl(smearedCacheLock);
                        smearedTimeline.records = records;
                        smearedTimeline.revA = rev;
                        smearedTimeline.revB = rev;
                        smearedTimeline.paramHash = ph;
                        smearedTimeline.strategy = strategy;
                        smearedTimeline.quality = quality;
                        smearedTimeline.valid = true;
                    }
                    if (!records.empty())
                        recipe.correspondenceRecords = std::move(records);

                    options.sourceResidualNoiseBands = makeMorphResidualNoiseBands(useA);
                    options.targetResidualNoiseBands = makeMorphResidualNoiseBands(useB);
                    sA = useA;
                    sB = useB;
                }
            }

            int endpointSourceIndex = -1;
            if (sA != nullptr && sB != nullptr
                && getConstantMorphEndpointSourceIndex(timelinePoints, endpointSourceIndex))
            {
                // An all-A/all-B timeline is an A/B reference path. Use the same ATSA
                // renderer as source audition so residual-only playback nulls in level/timbre.
                timelineBuffer = renderClassicSourcePlaybackBuffer(endpointSourceIndex == 0 ? sA : sB,
                                                                   core,
                                                                   renderSampleRate);
            }
            else if (sA != nullptr && sB != nullptr)
            {
                // Frame-by-frame oscillator bank synthesis: animates through actual ATS per-frame
                // data so the morph sounds like the real sources at 0% and 100%.
                ats::AtsSoundPtr shapedA;
                ats::AtsSoundPtr shapedB;
                const ATS_SOUND* renderA = makeFrequencyShapedRenderSound(sA, core.frequencyShape, shapedA);
                const ATS_SOUND* renderB = makeFrequencyShapedRenderSound(sB, core.frequencyShape, shapedB);
                timelineBuffer = renderMorphTrajectoryBuffer(renderA, renderB, recipe, timelinePoints,
                                                            0.5, renderSampleRate, duration,
                                                            morphEnvelopeCache.get(),
                                                            morphSourcesRevision.load(std::memory_order_acquire));
            }
            else
            {
                // Fallback: frozen snapshot segments (pre-load state, no raw frame data available).
                const int totalSamples = juce::jmax(1, (int) std::llround(renderSampleRate * duration));
                const int segmentCount = juce::jlimit(1, 32, (int) std::ceil(duration / 0.25));
                const int fadeSamples = juce::jlimit(1,
                                                     juce::jmax(1, totalSamples / juce::jmax(1, segmentCount * 3)),
                                                     (int) std::llround(renderSampleRate * 0.01));

                timelineBuffer.setSize(2, totalSamples);
                timelineBuffer.clear();

                for (int segment = 0; segment < segmentCount; ++segment)
                {
                    if (jobGeneration.load(std::memory_order_acquire) != gen)
                    {
                        transformThreadRunning.store(false, std::memory_order_release);
                        return;
                    }

                    const int boundaryStart = (int) (((int64_t) totalSamples * segment) / segmentCount);
                    const int boundaryEnd = (int) (((int64_t) totalSamples * (segment + 1)) / segmentCount);
                    const int copyStart = segment == 0 ? boundaryStart : juce::jmax(0, boundaryStart - fadeSamples);
                    const int segmentSamples = juce::jmax(1, boundaryEnd - copyStart);
                    const double midSample = 0.5 * (double) (boundaryStart + boundaryEnd);
                    const double timeNorm = midSample / (double) juce::jmax(1, totalSamples - 1);

                    options.morphAmount = evaluateMorphTimeline(timelinePoints, timeNorm);
                    const auto snapshot = ats::evaluateMorphRecipeSnapshot(recipe, options);

                    ats::MorphSnapshotPlaybackSettings settings;
                    settings.sampleRate = renderSampleRate;
                    settings.durationSeconds = (double) segmentSamples / renderSampleRate;
                    const auto prepared = ats::renderMorphSnapshotPlaybackBuffer(snapshot, settings);
                    const int preparedSamples = juce::jmin(segmentSamples, prepared.getNumSamples());

                    for (int i = 0; i < preparedSamples; ++i)
                    {
                        const int dst = copyStart + i;
                        if (dst < 0 || dst >= totalSamples)
                            continue;

                        const float partial = i < static_cast<int>(prepared.partialSamples.size())
                                                  ? prepared.partialSamples[(size_t) i]
                                                  : 0.0f;
                        const float residual = i < static_cast<int>(prepared.residualSamples.size())
                                                   ? prepared.residualSamples[(size_t) i]
                                                   : 0.0f;
                        if (segment > 0 && dst < boundaryStart)
                        {
                            const float mix = (float) (dst - copyStart + 1)
                                              / (float) juce::jmax(1, boundaryStart - copyStart + 1);
                            timelineBuffer.setSample(0,
                                                     dst,
                                                     timelineBuffer.getSample(0, dst) * (1.0f - mix) + partial * mix);
                            timelineBuffer.setSample(1,
                                                     dst,
                                                     timelineBuffer.getSample(1, dst) * (1.0f - mix) + residual * mix);
                        }
                        else
                        {
                            timelineBuffer.setSample(0, dst, partial);
                            timelineBuffer.setSample(1, dst, residual);
                        }
                    }
                }
            }

            if (timelineBuffer.getNumSamples() <= 0)
            {
                transformThreadRunning.store(false, std::memory_order_release);
                morphSnapshotState.store((int) AtsMorphSnapshotUiState::Warning, std::memory_order_release);
                setError("Morph timeline render produced no audio.");
                return;
            }

            if (jobGeneration.load(std::memory_order_acquire) != gen)
            {
                transformThreadRunning.store(false, std::memory_order_release);
                return;
            }

            if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
                stopPreviewPlayback();

            while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex
                   && jobGeneration.load(std::memory_order_acquire) == gen)
            {
                std::this_thread::yield();
            }

            if (jobGeneration.load(std::memory_order_acquire) != gen)
            {
                transformThreadRunning.store(false, std::memory_order_release);
                return;
            }

            playbackBuffers[(size_t) kMorphPlaybackBufferIndex].makeCopyOf(timelineBuffer);
            activePlaybackBufferIndex.store(kMorphPlaybackBufferIndex, std::memory_order_release);
            previewAuditionMode.store((int) mode, std::memory_order_release);
            previewPlaying.store(false, std::memory_order_release);
            morphSnapshotState.store((int) AtsMorphSnapshotUiState::Ready, std::memory_order_release);
            previewPlaybackKind.store((int) AtsPreviewPlaybackKind::MorphTimeline, std::memory_order_release);
            transformThreadRunning.store(false, std::memory_order_release);
            startPreviewPlayback();
        }
        catch (const std::exception& e)
        {
            transformThreadRunning.store(false, std::memory_order_release);
            morphSnapshotState.store((int) AtsMorphSnapshotUiState::Warning, std::memory_order_release);
            setError("Could not render morph timeline: " + juce::String(e.what()));
        }
        catch (...)
        {
            transformThreadRunning.store(false, std::memory_order_release);
            morphSnapshotState.store((int) AtsMorphSnapshotUiState::Warning, std::memory_order_release);
            setError("Could not render morph timeline.");
        }
    });

    return true;
}

bool AtsAudioProcessor::bounceMorphTimelineToFile(const juce::File& file,
                                                  AtsSourceAuditionMode mode,
                                                  juce::String& error)
{
    error.clear();
    if (isWorkerBusy())
    {
        error = "Wait for the current analysis or render to finish before bouncing the morph timeline.";
        return false;
    }

    ats::MorphRecipe recipe;
    std::array<std::vector<ats::MorphResidualNoiseBand>, 2> residualBands;
    std::vector<AtsMorphTimelinePoint> timelinePoints;
    const ATS_SOUND* sA = nullptr;
    const ATS_SOUND* sB = nullptr;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        syncMorphControlsToRecipe(apvts, morphRecipe);
        rebuildMorphCorrespondenceIfReady();
        timelinePoints = deserializeMorphTimelinePoints(getRecipeUnknownField(morphRecipe,
                                                                              kMorphTimelineUnknownField));
        upsertRecipeUnknownField(morphRecipe,
                                 kMorphTimelineUnknownField,
                                 serializeMorphTimelinePoints(timelinePoints));
        serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        recipe = morphRecipe;
        residualBands = morphSourceResidualBands;
        sA = morphSourceSounds[0].get();
        sB = morphSourceSounds[1].get();
    }

    if (recipe.sourceA.partialCount <= 0 || recipe.sourceB.partialCount <= 0)
    {
        error = "Analyze Source A and Source B before bouncing the morph timeline.";
        return false;
    }
    if (recipe.correspondenceRecords.empty())
    {
        error = "No morph correspondences are ready yet. Re-analyze Source A and Source B first.";
        return false;
    }

    ats::MorphSnapshotOptions options;
    options.frequency.domain =
        getRawChoiceValue(apvts, ats_param::kMorphFrequencyInterpolationDomain, kDefaultMorphFrequencyDomain) == 1
            ? ats::MorphFrequencyInterpolationDomain::BarkCoordinate
            : ats::MorphFrequencyInterpolationDomain::LogFrequency;
    options.amplitude.domain =
        getRawChoiceValue(apvts, ats_param::kMorphAmplitudeInterpolationDomain, kDefaultMorphAmplitudeDomain) == 1
            ? ats::MorphAmplitudeInterpolationDomain::Decibels
            : ats::MorphAmplitudeInterpolationDomain::Power;
    options.sourceResidualNoiseBands = residualBands[0];
    options.targetResidualNoiseBands = residualBands[1];

    ats::SynthParams core = readCoreSynthParamsFromApvts();
    const double renderSampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    const double duration = juce::jmax(0.25, juce::jmax(recipe.sourceA.durationSeconds,
                                                        recipe.sourceB.durationSeconds));

    juce::AudioBuffer<float> timelineBuffer;
    int endpointSourceIndex = -1;
    if (sA != nullptr && sB != nullptr
        && getConstantMorphEndpointSourceIndex(timelinePoints, endpointSourceIndex))
    {
        timelineBuffer = renderClassicSourcePlaybackBuffer(endpointSourceIndex == 0 ? sA : sB,
                                                           core,
                                                           renderSampleRate);
    }
    else if (sA != nullptr && sB != nullptr)
    {
        ats::AtsSoundPtr shapedA;
        ats::AtsSoundPtr shapedB;
        const ATS_SOUND* renderA = makeFrequencyShapedRenderSound(sA, core.frequencyShape, shapedA);
        const ATS_SOUND* renderB = makeFrequencyShapedRenderSound(sB, core.frequencyShape, shapedB);
        timelineBuffer = renderMorphTrajectoryBuffer(renderA, renderB, recipe, timelinePoints,
                                                     0.5, renderSampleRate, duration,
                                                     morphEnvelopeCache.get(),
                                                     morphSourcesRevision.load(std::memory_order_acquire));
    }
    else
    {
        const int totalSamples = juce::jmax(1, (int) std::llround(renderSampleRate * duration));
        const int segmentCount = juce::jlimit(1, 32, (int) std::ceil(duration / 0.25));
        const int fadeSamples = juce::jlimit(1,
                                             juce::jmax(1, totalSamples / juce::jmax(1, segmentCount * 3)),
                                             (int) std::llround(renderSampleRate * 0.01));

        timelineBuffer.setSize(2, totalSamples);
        timelineBuffer.clear();

        for (int segment = 0; segment < segmentCount; ++segment)
        {
            const int boundaryStart = (int) (((int64_t) totalSamples * segment) / segmentCount);
            const int boundaryEnd = (int) (((int64_t) totalSamples * (segment + 1)) / segmentCount);
            const int copyStart = segment == 0 ? boundaryStart : juce::jmax(0, boundaryStart - fadeSamples);
            const int segmentSamples = juce::jmax(1, boundaryEnd - copyStart);
            const double midSample = 0.5 * (double) (boundaryStart + boundaryEnd);
            const double timeNorm = midSample / (double) juce::jmax(1, totalSamples - 1);

            options.morphAmount = evaluateMorphTimeline(timelinePoints, timeNorm);
            const auto snapshot = ats::evaluateMorphRecipeSnapshot(recipe, options);

            ats::MorphSnapshotPlaybackSettings settings;
            settings.sampleRate = renderSampleRate;
            settings.durationSeconds = (double) segmentSamples / renderSampleRate;
            const auto prepared = ats::renderMorphSnapshotPlaybackBuffer(snapshot, settings);
            const int preparedSamples = juce::jmin(segmentSamples, prepared.getNumSamples());

            for (int i = 0; i < preparedSamples; ++i)
            {
                const int dst = copyStart + i;
                if (dst < 0 || dst >= totalSamples)
                    continue;

                const float partial = i < static_cast<int>(prepared.partialSamples.size())
                                          ? prepared.partialSamples[(size_t) i]
                                          : 0.0f;
                const float residual = i < static_cast<int>(prepared.residualSamples.size())
                                           ? prepared.residualSamples[(size_t) i]
                                           : 0.0f;
                if (segment > 0 && dst < boundaryStart)
                {
                    const float mix = (float) (dst - copyStart + 1)
                                      / (float) juce::jmax(1, boundaryStart - copyStart + 1);
                    timelineBuffer.setSample(0,
                                             dst,
                                             timelineBuffer.getSample(0, dst) * (1.0f - mix) + partial * mix);
                    timelineBuffer.setSample(1,
                                             dst,
                                             timelineBuffer.getSample(1, dst) * (1.0f - mix) + residual * mix);
                }
                else
                {
                    timelineBuffer.setSample(0, dst, partial);
                    timelineBuffer.setSample(1, dst, residual);
                }
            }
        }
    }

    if (timelineBuffer.getNumSamples() <= 0)
    {
        error = "Morph timeline render produced no audio.";
        return false;
    }

    // Bounce should match timeline audition mode (Both/Partials/Residual) as heard in preview.
    juce::AudioBuffer<float> bounceBuffer(2, timelineBuffer.getNumSamples());
    const float trim = outputTrimRaw->load(std::memory_order_relaxed);
    const float partGain = partGainRaw->load(std::memory_order_relaxed);
    const float resGain = resGainRaw->load(std::memory_order_relaxed);
    const bool includePartials = mode != AtsSourceAuditionMode::ResidualOnly;
    const bool includeResidual = mode != AtsSourceAuditionMode::PartialsOnly;
    const bool useClassicBothPlayback =
        mode == AtsSourceAuditionMode::Both
        && timelineBuffer.getNumChannels() > 2
        && juce::jmax(0, juce::roundToInt(classicPreviewModeRaw->load(std::memory_order_relaxed))) == 0;
    for (int i = 0; i < timelineBuffer.getNumSamples(); ++i)
    {
        const float partial = timelineBuffer.getNumChannels() > 0 ? timelineBuffer.getSample(0, i) : 0.0f;
        const float residual = timelineBuffer.getNumChannels() > 1 ? timelineBuffer.getSample(1, i) : 0.0f;
        const float mixed = softLimitPreviewSample((useClassicBothPlayback
                                                        ? timelineBuffer.getSample(2, i)
                                                        : ((includePartials ? partial * partGain : 0.0f)
                                                           + (includeResidual ? residual * resGain : 0.0f)))
                                                   * trim);
        bounceBuffer.setSample(0, i, mixed);
        bounceBuffer.setSample(1, i, mixed);
    }

    juce::File outFile = file;
    if (!outFile.hasFileExtension(".wav"))
        outFile = outFile.withFileExtension("wav");
    if (outFile.existsAsFile() && !outFile.deleteFile())
    {
        error = "Could not overwrite existing file.";
        return false;
    }

    auto outStream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream());
    if (outStream == nullptr || !outStream->openedOk())
    {
        error = "Could not create output file.";
        return false;
    }

    juce::WavAudioFormat wav;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(outStream.get(),
                                                                                renderSampleRate,
                                                                                (unsigned int) bounceBuffer.getNumChannels(),
                                                                                24,
                                                                                {},
                                                                                0));
    if (writer == nullptr)
    {
        error = "Could not create WAV writer.";
        return false;
    }
    outStream.release();

    if (!writer->writeFromAudioSampleBuffer(bounceBuffer, 0, bounceBuffer.getNumSamples()))
    {
        error = "Could not write timeline WAV.";
        return false;
    }

    return true;
}

std::vector<AtsMorphTimelinePoint> AtsAudioProcessor::getMorphTimelinePoints() const
{
    const juce::ScopedLock ml(morphRecipeLock);
    return deserializeMorphTimelinePoints(getRecipeUnknownField(morphRecipe, kMorphTimelineUnknownField));
}

void AtsAudioProcessor::setMorphTimelinePoints(const std::vector<AtsMorphTimelinePoint>& points)
{
    const auto safePoints = sanitizeMorphTimelinePoints(points);
    {
        const juce::ScopedLock ml(morphRecipeLock);
        upsertRecipeUnknownField(morphRecipe,
                                 kMorphTimelineUnknownField,
                                 serializeMorphTimelinePoints(safePoints));
        serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
    }
    markMorphSnapshotStale();
}

// ============================================================================
//  Undo / redo
// ============================================================================
namespace
{
/** Deep-copy snapshot of an ATS model that frees itself the way AtsSoundPtr does. */
AtsAudioProcessor::AtsSoundSnapshot makeModelSnapshot(const ATS_SOUND* src)
{
    if (src == nullptr)
        return {};
    ATS_SOUND* copy = copy_sound(const_cast<ATS_SOUND*>(src));
    if (copy == nullptr)
        return {};
    return AtsAudioProcessor::AtsSoundSnapshot(copy, [](ATS_SOUND* p) {
        if (p != nullptr)
        {
            free_sound(p);
            std::free(p);
        }
    });
}

/** Rough heap footprint of a snapshot in KB, for UndoManager memory accounting. */
int snapshotSizeKb(const AtsAudioProcessor::AtsSoundSnapshot& s)
{
    if (!s)
        return 1;
    const auto* p = s.get();
    // 6 sinusoidal arrays (time/frq/amp/pha/smr/res) + band energy, doubles.
    const double cells = (double) p->partials * (double) p->frames * 6.0
                         + (double) ATSA_CRITICAL_BANDS * (double) p->frames;
    return juce::jmax(1, (int) (cells * sizeof(double) / 1024.0));
}

/** Replaces the live model for a source with a stored snapshot on undo/redo. The
 *  edit that created the action already applied the "after" state, so the first
 *  perform() is a no-op; later perform()s (redo) re-install the after state. */
class ModelEditAction : public juce::UndoableAction
{
public:
    ModelEditAction(AtsAudioProcessor& p,
                    AtsTraceSource src,
                    AtsAudioProcessor::AtsSoundSnapshot before,
                    AtsAudioProcessor::AtsSoundSnapshot after)
        : proc(p), source(src), beforeSnap(std::move(before)), afterSnap(std::move(after))
    {
    }

    bool perform() override
    {
        if (firstPerform)
        {
            firstPerform = false; // state already applied by the originating edit
            return true;
        }
        proc.installModelSnapshot(source, afterSnap);
        return true;
    }

    bool undo() override
    {
        proc.installModelSnapshot(source, beforeSnap);
        return true;
    }

    int getSizeInUnits() override { return snapshotSizeKb(beforeSnap) + snapshotSizeKb(afterSnap); }

private:
    AtsAudioProcessor& proc;
    AtsTraceSource source;
    AtsAudioProcessor::AtsSoundSnapshot beforeSnap, afterSnap;
    bool firstPerform = true;
};

/** Undo entry for morph-timeline point edits. */
class TimelineEditAction : public juce::UndoableAction
{
public:
    TimelineEditAction(AtsAudioProcessor& p,
                       std::vector<AtsMorphTimelinePoint> before,
                       std::vector<AtsMorphTimelinePoint> after)
        : proc(p), beforePts(std::move(before)), afterPts(std::move(after))
    {
    }

    bool perform() override
    {
        if (firstPerform)
        {
            firstPerform = false;
            return true;
        }
        proc.setMorphTimelinePoints(afterPts);
        return true;
    }

    bool undo() override
    {
        proc.setMorphTimelinePoints(beforePts);
        return true;
    }

    int getSizeInUnits() override
    {
        return juce::jmax(1, (int) ((beforePts.size() + afterPts.size())
                                    * sizeof(AtsMorphTimelinePoint) / 1024));
    }

private:
    AtsAudioProcessor& proc;
    std::vector<AtsMorphTimelinePoint> beforePts, afterPts;
    bool firstPerform = true;
};
} // namespace

AtsAudioProcessor::AtsSoundSnapshot AtsAudioProcessor::captureModelSnapshot(AtsTraceSource source) const
{
    if (source == AtsTraceSource::Current)
    {
        const juce::ScopedLock ml(modelLock);
        return makeModelSnapshot(analyzedModel.get());
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    const juce::ScopedLock ml(morphRecipeLock);
    return makeModelSnapshot(morphSourceSounds[(size_t) slotIndex].get());
}

void AtsAudioProcessor::installModelSnapshot(AtsTraceSource source, const AtsSoundSnapshot& snapshot)
{
    if (!snapshot)
        return;

    if (source == AtsTraceSource::Current)
    {
        {
            const juce::ScopedLock ml(modelLock);
            analyzedModel.reset(copy_sound(snapshot.get()));
        }

        // Mirror the morph-playback invalidation that destructive Current edits do.
        const auto playbackKind = (AtsPreviewPlaybackKind) previewPlaybackKind.load(std::memory_order_acquire);
        if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex
            && playbackKind != AtsPreviewPlaybackKind::MorphTimeline)
        {
            stopPreviewPlayback();
            activePlaybackBufferIndex.store(-1, std::memory_order_release);
            while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
                std::this_thread::yield();
            playbackBuffers[(size_t) kMorphPlaybackBufferIndex].setSize(0, 0);
        }

        modelRevision.fetch_add(1, std::memory_order_relaxed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
        return;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        morphSourceSounds[(size_t) slotIndex].reset(copy_sound(snapshot.get()));
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        morphSourceAssignments[(size_t) slotIndex] = makeMorphAssignmentsFromSound(s, morphRecipe.frequencyGrid);
        rebuildMorphCorrespondenceIfReady();
        serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
}

void AtsAudioProcessor::recordModelEdit(AtsTraceSource source, AtsSoundSnapshot before, const juce::String& label)
{
    if (!before)
        return;
    auto after = captureModelSnapshot(source);
    if (!after)
        return;

    undoManager.beginNewTransaction(label);
    undoManager.perform(new ModelEditAction(*this, source, std::move(before), std::move(after)));
    undoManager.beginNewTransaction(); // seal: keep later parameter edits out of this transaction
}

AtsAudioProcessor::ScopedModelEdit::ScopedModelEdit(AtsAudioProcessor& p, AtsTraceSource s, juce::String l)
    : proc(p), source(s), label(std::move(l))
{
    if (proc.modelEditRecursion++ == 0)
    {
        active = true;
        revAtStart = proc.modelRevision.load(std::memory_order_relaxed);
        before = proc.captureModelSnapshot(source);
    }
}

AtsAudioProcessor::ScopedModelEdit::~ScopedModelEdit()
{
    --proc.modelEditRecursion;
    if (active && before
        && proc.modelRevision.load(std::memory_order_relaxed) != revAtStart)
    {
        proc.recordModelEdit(source, before, label);
    }
}

void AtsAudioProcessor::setMorphTimelinePointsUndoable(const std::vector<AtsMorphTimelinePoint>& points,
                                                       const juce::String& label)
{
    auto before = getMorphTimelinePoints();
    setMorphTimelinePoints(points);
    auto after = getMorphTimelinePoints();

    // onPointsChanged fires on every mouseDrag tick, so coalesce a continuous
    // drag (ticks <500 ms apart) into a single undo transaction rather than one
    // entry per pixel. Undoing the transaction reverts to the pre-drag state.
    const auto nowMs = juce::Time::currentTimeMillis();
    if (nowMs - lastTimelineEditMs > 500)
        undoManager.beginNewTransaction(label);
    lastTimelineEditMs = nowMs;

    undoManager.perform(new TimelineEditAction(*this, std::move(before), std::move(after)));
}

void AtsAudioProcessor::undo()
{
    if (undoManager.canUndo())
        undoManager.undo();
}

void AtsAudioProcessor::redo()
{
    if (undoManager.canRedo())
        undoManager.redo();
}

bool AtsAudioProcessor::canUndo() const { return undoManager.canUndo(); }
bool AtsAudioProcessor::canRedo() const { return undoManager.canRedo(); }
juce::String AtsAudioProcessor::getUndoDescription() const { return undoManager.getUndoDescription(); }
juce::String AtsAudioProcessor::getRedoDescription() const { return undoManager.getRedoDescription(); }

bool AtsAudioProcessor::installPreparedMorphSnapshotForPlayback(const ats::MorphRenderSnapshot& snapshot,
                                                                double durationSeconds,
                                                                juce::String& error)
{
    error.clear();
    if (!snapshot.safeForAudioThreadPlayback)
    {
        error = "Morph snapshot is not marked audio-thread safe.";
        return false;
    }

    ats::MorphSnapshotPlaybackSettings settings;
    settings.sampleRate = hostSampleRate;
    settings.durationSeconds = durationSeconds > 0.0 ? durationSeconds : 1.0;
    const auto prepared = ats::renderMorphSnapshotPlaybackBuffer(snapshot, settings);
    const int totalSamples = prepared.getNumSamples();
    if (totalSamples <= 0)
    {
        error = "Morph snapshot produced no preview audio.";
        return false;
    }

    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
        stopPreviewPlayback();

    // Wait for processBlock to finish reading this buffer before reallocating.
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
        std::this_thread::yield();

    auto& stereo = playbackBuffers[(size_t) kMorphPlaybackBufferIndex];
    stereo.setSize(2, totalSamples);
    for (int i = 0; i < totalSamples; ++i)
    {
        const float partial = i < (int) prepared.partialSamples.size()
                                  ? prepared.partialSamples[(size_t) i]
                                  : 0.0f;
        const float residual = i < (int) prepared.residualSamples.size()
                                   ? prepared.residualSamples[(size_t) i]
                                   : 0.0f;
        stereo.setSample(0, i, partial);
        stereo.setSample(1, i, residual);
    }

    activePlaybackBufferIndex.store(kMorphPlaybackBufferIndex, std::memory_order_release);
    previewAuditionMode.store((int) AtsSourceAuditionMode::Both, std::memory_order_release);
    previewPlaying.store(false, std::memory_order_release);
    morphSnapshotState.store((int) AtsMorphSnapshotUiState::Ready, std::memory_order_release);
    previewPlaybackKind.store((int) AtsPreviewPlaybackKind::MorphPreview, std::memory_order_release);
    return true;
}

bool AtsAudioProcessor::loadAndAnalyzeMorphSourceSlot(int slotIndex,
                                                      const juce::File& file,
                                                      juce::String& error)
{
    error.clear();
    if (slotIndex < 0 || slotIndex > 1)
    {
        error = "Invalid morph source slot.";
        return false;
    }
    if (!file.existsAsFile())
    {
        error = "Select a WAV file for the morph source.";
        return false;
    }
    if (isWorkerBusy())
    {
        error = "Wait for the current analysis to finish before loading another morph source.";
        return false;
    }

    loadSourceFile(file);
    if (getUiState() != AtsUiState::ReadyToAnalyze)
    {
        error = getLastError();
        if (error.isEmpty())
            error = "Could not load WAV file for morph source.";
        return false;
    }

    {
        const juce::ScopedLock sl(sourceLock);
        morphSourceFiles[(size_t) slotIndex] = file;
    }

    pendingMorphSourceSlotAfterAnalysis.store(slotIndex, std::memory_order_release);
    startAnalysis();
    return true;
}

bool AtsAudioProcessor::reanalyzeMorphSourceSlot(int slotIndex, juce::String& error)
{
    error.clear();
    if (slotIndex < 0 || slotIndex > 1)
    {
        error = "Invalid morph source slot.";
        return false;
    }
    if (isWorkerBusy())
    {
        error = "Wait for the current analysis to finish before re-analyzing a morph source.";
        return false;
    }

    juce::File sourceFile;
    {
        const juce::ScopedLock sl(sourceLock);
        sourceFile = morphSourceFiles[(size_t) slotIndex];
    }

    if (sourceFile.getFullPathName().isEmpty())
    {
        error = slotIndex == 0 ? "Choose Source A before re-analyzing."
                               : "Choose Source B before re-analyzing.";
        return false;
    }
    if (!sourceFile.existsAsFile())
    {
        error = slotIndex == 0 ? "Source A WAV no longer exists. Choose Source A again."
                               : "Source B WAV no longer exists. Choose Source B again.";
        return false;
    }

    return loadAndAnalyzeMorphSourceSlot(slotIndex, sourceFile, error);
}

bool AtsAudioProcessor::getMorphSourceFile(int slotIndex, juce::File& out) const
{
    if (slotIndex < 0 || slotIndex > 1)
        return false;

    const juce::ScopedLock sl(sourceLock);
    out = morphSourceFiles[(size_t) slotIndex];
    return out.getFullPathName().isNotEmpty();
}

void AtsAudioProcessor::markMorphSnapshotStale()
{
    // Any morph-source change invalidates the live-smear A/B + timeline caches.
    morphSourcesRevision.fetch_add(1, std::memory_order_relaxed);

    const auto current = (AtsMorphSnapshotUiState) morphSnapshotState.load(std::memory_order_acquire);
    if (current == AtsMorphSnapshotUiState::Ready)
        morphSnapshotState.store((int) AtsMorphSnapshotUiState::Stale, std::memory_order_release);
}

void AtsAudioProcessor::rebuildMorphCorrespondenceIfReady()
{
    if (morphSourceAssignments[0].empty() || morphSourceAssignments[1].empty())
        return;

    const int strategy = getRawChoiceValue(apvts,
                                           ats_param::kMorphCorrespondenceStrategy,
                                           kDefaultMorphCorrespondenceStrategy);
    const int quality = getRawChoiceValue(apvts, ats_param::kMorphQuality, kDefaultMorphQuality);
    morphRecipe.correspondenceRecords =
        buildMorphCorrespondence(morphSourceAssignments[0], morphSourceAssignments[1], strategy,
                                 morphQualityToMinMassFraction(quality));
}

bool AtsAudioProcessor::getMorphSourceSlotInfo(int slotIndex, AtsMorphSourceSlotInfo& out) const
{
    if (slotIndex < 0 || slotIndex > 1)
        return false;

    out = {};
    out.slotName = slotIndex == 0 ? "Source A" : "Source B";

    const juce::ScopedLock ml(morphRecipeLock);
    const auto& source = slotIndex == 0 ? morphRecipe.sourceA : morphRecipe.sourceB;
    const auto& transients = slotIndex == 0 ? morphRecipe.sourceATransients : morphRecipe.sourceBTransients;
    const auto diagnostics = morphSourceDiagnostics[(size_t) slotIndex];

    out.sourceName = juce::String(source.name.c_str());
    out.sourceHash = juce::String(source.hash.c_str());
    out.durationSeconds = source.durationSeconds;
    out.sampleRate = source.sampleRate;
    out.frameCount = source.frameCount;
    out.partialCount = source.partialCount;
    out.hasPhase = source.hasPhase;
    out.hasResidual = source.hasResidual;
    out.residualDiagnostics = diagnostics;
    out.hasTransientMarkers = !transients.empty();
    out.hasModel = !source.id.empty() || !source.hash.empty() || source.sampleRate > 0.0
                   || source.frameCount > 0 || source.partialCount > 0;
    return true;
}

bool AtsAudioProcessor::setCurrentModelAsMorphSourceSlot(int slotIndex, juce::String& error)
{
    error.clear();
    if (slotIndex < 0 || slotIndex > 1)
    {
        error = "Invalid morph source slot.";
        return false;
    }

    ats::MorphSourceMetadata metadata;
    AtsResidualDiagnostics residualDiagnostics;
    std::vector<ats::MorphResidualNoiseBand> residualBands;
    std::vector<ats::MorphBandAssignment> assignments;
    ATS_SOUND* soundCopy = nullptr;
    {
        const juce::ScopedLock ml(modelLock);
        if (analyzedModel.get() == nullptr)
        {
            error = "No analyzed ATS model to assign.";
            return false;
        }

        metadata = makeMorphSourceMetadata(
            analyzedModel.get(), slotIndex, currentModelName, modelRevision.load(std::memory_order_relaxed));
        residualDiagnostics = makeResidualDiagnostics(analyzedModel.get());
        residualBands = makeMorphResidualNoiseBands(analyzedModel.get());
        soundCopy = copy_sound(analyzedModel.get()); // deep copy for frame-by-frame morph synthesis

        const juce::ScopedLock rl(morphRecipeLock);
        assignments = makeMorphAssignmentsFromSound(analyzedModel.get(), morphRecipe.frequencyGrid);
    }

    auto transientMarkers = detectMorphTransients(soundCopy, metadata.frameHopSeconds);

    if (!renderCurrentModelToPlaybackBuffer(slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex,
                                            error))
    {
        if (soundCopy != nullptr)
        {
            free_sound(soundCopy);
            std::free(soundCopy);
        }
        return false;
    }

    {
        const juce::ScopedLock ml(morphRecipeLock);
        if (slotIndex == 0)
        {
            morphRecipe.sourceA = metadata;
            morphRecipe.sourceATransients = std::move(transientMarkers);
        }
        else
        {
            morphRecipe.sourceB = metadata;
            morphRecipe.sourceBTransients = std::move(transientMarkers);
        }
        morphSourceAssignments[(size_t) slotIndex] = std::move(assignments);
        morphSourceDiagnostics[(size_t) slotIndex] = residualDiagnostics;
        morphSourceResidualBands[(size_t) slotIndex] = std::move(residualBands);
        morphSourceSounds[(size_t) slotIndex].reset(soundCopy);
        syncMorphControlsToRecipe(apvts, morphRecipe);
        rebuildMorphCorrespondenceIfReady();
        serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        morphRecipeRevision.fetch_add(1, std::memory_order_release);
    }

    clearSmearnovOnsetOverrides(slotIndex == 0 ? AtsTraceSource::SourceA : AtsTraceSource::SourceB);

    markMorphSnapshotStale();
    {
        const juce::SpinLock::ScopedLockType sl(errorLock);
        lastError.clear();
    }
    return true;
}

bool AtsAudioProcessor::listenMorphSourceSlot(int slotIndex, AtsSourceAuditionMode mode, juce::String& error)
{
    return listenMorphSourceSlot(slotIndex, mode, false, error);
}

bool AtsAudioProcessor::listenMorphSourceSlot(int slotIndex, AtsSourceAuditionMode mode, bool liveSmear, juce::String& error)
{
    error.clear();
    AtsMorphSourceSlotInfo info;
    if (!getMorphSourceSlotInfo(slotIndex, info) || !info.hasModel)
    {
        atsPlayLog("listenMorphSourceSlot slot=" + juce::String(slotIndex)
                   + " REJECTED: no model (hasModel=" + juce::String((int) info.hasModel) + ")");
        error = "Assign a source slot before listening.";
        return false;
    }
    if (mode == AtsSourceAuditionMode::ResidualOnly && !info.hasResidual)
    {
        error = "This source has no residual layer. Re-analyze with analysis type 3 or 4.";
        return false;
    }
    atsPlayLog("listenMorphSourceSlot slot=" + juce::String(slotIndex)
               + " mode=" + juce::String((int) mode) + " liveSmear=" + juce::String((int) liveSmear));

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    const bool smearIsolate = mode == AtsSourceAuditionMode::SmearOnly;
    if (!renderMorphSourceSlotToPlaybackBuffer(slotIndex, error, liveSmear || smearIsolate, smearIsolate))
    {
        if (error.isEmpty())
            error = "No rendered source preview is available. Analyze this source again.";
        return false;
    }

    activePlaybackBufferIndex.store(bufferIndex, std::memory_order_release);
    previewAuditionMode.store((int) mode, std::memory_order_release);
    previewPlaybackKind.store((int) (slotIndex == 0 ? AtsPreviewPlaybackKind::SourceA
                                                     : AtsPreviewPlaybackKind::SourceB),
                              std::memory_order_release);
    startPreviewPlayback();
    return true;
}

bool AtsAudioProcessor::listenMorphSourceSlot(int slotIndex, juce::String& error)
{
    return listenMorphSourceSlot(slotIndex, AtsSourceAuditionMode::Both, error);
}

std::vector<AtsMorphBandDiagnostic> AtsAudioProcessor::buildMorphBandDiagnostics() const
{
    ats::MorphRecipe recipe;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        recipe = morphRecipe;
    }

    const int bandCount = juce::jmax(0, recipe.frequencyGrid.bandCount);
    std::vector<AtsMorphBandDiagnostic> diagnostics((size_t) bandCount);
    for (int i = 0; i < bandCount; ++i)
    {
        auto& band = diagnostics[(size_t) i];
        band.bandIndex = i;
        if ((int) recipe.frequencyGrid.edgeFrequenciesHz.size() > i + 1)
        {
            band.lowHz = recipe.frequencyGrid.edgeFrequenciesHz[(size_t) i];
            band.highHz = recipe.frequencyGrid.edgeFrequenciesHz[(size_t) i + 1];
        }
    }

    ats::MorphBandEnergyOptions energyOptions;
    energyOptions.morphAmount = getRawParamValue(apvts, ats_param::kMorphAmount, 0.0f);
    energyOptions.unmatchedPolicies = recipe.unmatchedPartialPolicies;
    energyOptions.amplitudeInterpolation.domain =
        getRawChoiceValue(apvts,
                          ats_param::kMorphAmplitudeInterpolationDomain,
                          kDefaultMorphAmplitudeDomain)
                == 1
            ? ats::MorphAmplitudeInterpolationDomain::Decibels
            : ats::MorphAmplitudeInterpolationDomain::Power;
    const auto energyReport = ats::calculateBandEnergyReport(recipe.correspondenceRecords, energyOptions);
    for (const auto& energy : energyReport.bands)
    {
        if (!juce::isPositiveAndBelow(energy.bandIndex, bandCount))
            continue;

        auto& band = diagnostics[(size_t) energy.bandIndex];
        band.sourcePower = energy.sourceSinusoidalPower;
        band.targetPower = energy.targetSinusoidalPower;
        band.outputPower = energy.outputSinusoidalPower + energy.outputResidualPower;
        band.expectedPower = energy.expectedOutputPower;
        band.energyDelta = energy.powerDelta;
        band.warning = energy.exceedsTolerance;
    }

    const auto transientReport = ats::analyzeMorphTransientAlignment(recipe.sourceATransients,
                                                                     recipe.sourceBTransients,
                                                                     recipe.transient);
    std::vector<std::vector<const ats::MorphCorrespondenceRecord*>> recordsByBand((size_t) bandCount);
    for (const auto& record : recipe.correspondenceRecords)
    {
        if (!juce::isPositiveAndBelow(record.bandIndex, bandCount))
            continue;

        auto& band = diagnostics[(size_t) record.bandIndex];
        recordsByBand[(size_t) record.bandIndex].push_back(&record);
        switch (record.kind)
        {
            case ats::MorphCorrespondenceKind::Matched:
                ++band.matchedCount;
                break;
            case ats::MorphCorrespondenceKind::UnmatchedSource:
                ++band.unmatchedSourceCount;
                break;
            case ats::MorphCorrespondenceKind::UnmatchedTarget:
                ++band.unmatchedTargetCount;
                break;
        }
    }

    const auto sourcePolicy = juce::String(ats::morphUnmatchedPartialPolicyName(
        recipe.unmatchedPartialPolicies.sourcePolicy));
    const auto targetPolicy = juce::String(ats::morphUnmatchedPartialPolicyName(
        recipe.unmatchedPartialPolicies.targetPolicy));
    const auto phasePolicy = juce::String(ats::morphPhaseStrategyName(recipe.phase.strategy));
    const auto transientPolicy = juce::String(ats::morphTransientPolicyName(recipe.transient.policy));
    const auto gridType = juce::String(ats::morphFrequencyGridTypeName(recipe.frequencyGrid.type));
    const auto correspondence = correspondenceStrategyDisplayName(
        getRawChoiceValue(apvts,
                          ats_param::kMorphCorrespondenceStrategy,
                          kDefaultMorphCorrespondenceStrategy));
    const auto frequencyDomain = frequencyDomainName(getRawChoiceValue(apvts,
                                                                       ats_param::kMorphFrequencyInterpolationDomain,
                                                                       kDefaultMorphFrequencyDomain));
    const auto amplitudeDomain = amplitudeDomainName(getRawChoiceValue(apvts,
                                                                       ats_param::kMorphAmplitudeInterpolationDomain,
                                                                       kDefaultMorphAmplitudeDomain));

    for (int i = 0; i < bandCount; ++i)
    {
        auto& band = diagnostics[(size_t) i];
        const auto& records = recordsByBand[(size_t) i];
        band.warning = band.warning || band.unmatchedSourceCount > 0 || band.unmatchedTargetCount > 0
                       || transientReport.warningCount > 0;

        juce::String text;
        text << "Band " << juce::String(i + 1) << " | "
             << juce::String(band.lowHz, 0) << "-" << juce::String(band.highHz, 0) << " Hz"
             << "\nGrid " << gridType << " | Correspondence " << correspondence
             << "\nFreq " << frequencyDomain << " | Amp " << amplitudeDomain
             << "\nMatched " << juce::String(band.matchedCount)
             << " | Source-only " << juce::String(band.unmatchedSourceCount)
             << " | Target-only " << juce::String(band.unmatchedTargetCount)
             << "\nPower A " << formatPower(band.sourcePower)
             << " | B " << formatPower(band.targetPower)
             << " | Out " << formatPower(band.outputPower)
             << " | Delta " << formatPower(std::abs(band.energyDelta))
             << (band.warning ? " | Warning" : "")
             << "\nPolicies S " << sourcePolicy << " / T " << targetPolicy
             << " | Phase " << phasePolicy
             << "\nTransient " << transientPolicy
             << " | transient warnings " << juce::String(transientReport.warningCount)
             << "\nLifecycle birth " << juce::String(recipe.lifecycle.birthFadeFrames)
             << "f death " << juce::String(recipe.lifecycle.deathFadeFrames)
             << "f max step " << juce::String(recipe.lifecycle.maxGainStepPerFrame, 3);

        if (records.empty())
        {
            text << "\nNo correspondence records for this band yet.";
        }
        else
        {
            text << "\nPairs:";
            const int limit = juce::jmin(4, (int) records.size());
            for (int r = 0; r < limit; ++r)
            {
                const auto& record = *records[(size_t) r];
                text << "\n#" << juce::String(record.rank)
                     << " " << ats::morphCorrespondenceKindName(record.kind)
                     << " A" << juce::String(record.sourcePartialIndex)
                     << " -> B" << juce::String(record.targetPartialIndex);
                if (record.hasCostDiagnostics)
                    text << " cost " << juce::String(record.totalCost, 4);
            }
            if ((int) records.size() > limit)
                text << "\n+" << juce::String((int) records.size() - limit) << " more";
        }

        band.inspectorText = text;
    }

    return diagnostics;
}

void AtsAudioProcessor::applyMorphQuickSnapshot(float amount)
{
    setParameterValue(apvts, ats_param::kMorphAmount, juce::jlimit(0.0f, 1.0f, amount));
    markMorphSnapshotStale();
}

bool AtsAudioProcessor::saveCurrentMorphVariation(int slotIndex, juce::String& error)
{
    error.clear();
    if (slotIndex < 0 || slotIndex >= ats_param::kMorphVariationSlotCount)
    {
        error = "Invalid morph variation slot.";
        return false;
    }

    auto slot = captureMorphSnapshotSlot(apvts, "V" + juce::String(slotIndex + 1));
    {
        const juce::ScopedLock sl(morphSnapshotLock);
        morphVariationSlots[(size_t) slotIndex] = slot;
    }
    return true;
}

bool AtsAudioProcessor::recallMorphVariation(int slotIndex, juce::String& error)
{
    error.clear();
    AtsMorphSnapshotSlotInfo slot;
    if (!getMorphVariationSlotInfo(slotIndex, slot))
    {
        error = "Invalid morph variation slot.";
        return false;
    }
    if (!slot.hasSnapshot)
    {
        error = "Variation slot is empty.";
        return false;
    }

    setParameterValue(apvts, ats_param::kMorphAmount, slot.morphAmount);
    setParameterValue(apvts, ats_param::kMorphPerformanceMacro, slot.performanceMacro);
    setParameterValue(apvts, ats_param::kMorphSmoothingMs, slot.smoothingMs);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        setParameterValue(apvts, ats_param::kMorphLaneAmountIds[i], slot.laneAmounts[i]);
        setParameterBool(apvts, ats_param::kMorphLaneLinkedIds[i], slot.laneLinked[i]);
    }
    markMorphSnapshotStale();
    return true;
}

bool AtsAudioProcessor::getMorphVariationSlotInfo(int slotIndex, AtsMorphSnapshotSlotInfo& out) const
{
    if (slotIndex < 0 || slotIndex >= ats_param::kMorphVariationSlotCount)
        return false;

    const juce::ScopedLock sl(morphSnapshotLock);
    out = morphVariationSlots[(size_t) slotIndex];
    if (out.name.isEmpty())
        out.name = "V" + juce::String(slotIndex + 1);
    return true;
}

AtsMorphSnapshotUiState AtsAudioProcessor::getMorphSnapshotUiState() const
{
    if (isWorkerBusy())
        return AtsMorphSnapshotUiState::Rebuilding;

    AtsMorphSourceSlotInfo sourceA;
    AtsMorphSourceSlotInfo sourceB;
    getMorphSourceSlotInfo(0, sourceA);
    getMorphSourceSlotInfo(1, sourceB);
    if (!sourceA.hasModel || !sourceB.hasModel)
        return AtsMorphSnapshotUiState::Warning;

    return (AtsMorphSnapshotUiState) morphSnapshotState.load(std::memory_order_acquire);
}

juce::String AtsAudioProcessor::getMorphSnapshotStatusText() const
{
    switch (getMorphSnapshotUiState())
    {
        case AtsMorphSnapshotUiState::Ready:
            return "Ready: snapshot current";
        case AtsMorphSnapshotUiState::Stale:
            return "Stale: last valid snapshot still playing";
        case AtsMorphSnapshotUiState::Rebuilding:
            return "Rebuilding: keeping last valid snapshot";
        case AtsMorphSnapshotUiState::Warning:
        default:
            return "Warning: assign source A and B";
    }
}

void AtsAudioProcessor::setMorphPresetMetadata(const juce::String& presetName,
                                               const juce::String& description)
{
    const juce::ScopedLock ml(morphRecipeLock);
    syncMorphControlsToRecipe(apvts, morphRecipe);
    upsertRecipeUnknownField(morphRecipe, "ui.morph_preset_name", presetName.toStdString());
    upsertRecipeUnknownField(morphRecipe, "ui.morph_preset_description", description.toStdString());
    serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
    markMorphSnapshotStale();
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
    writeFrequencyShape(mos, sp.frequencyShape);
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

    if (kStateVersion >= 11)
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kClassicPreviewMode, kDefaultClassicPreviewMode));

    if (kStateVersion >= 5)
    {
        std::string recipeText;
        {
            const juce::ScopedLock ml(morphRecipeLock);
            syncMorphControlsToRecipe(apvts, morphRecipe);
            recipeText = ats::serializeMorphRecipe(morphRecipe);
            serializedMorphRecipe = recipeText;
        }
        writeStringChunk(mos, recipeText);
    }

    if (kStateVersion >= 6)
        mos.writeDouble(getRawParamValue(apvts, ats_param::kMorphAmount, 0.0f));

    if (kStateVersion >= 7)
    {
        mos.writeDouble(getRawParamValue(apvts,
                                         ats_param::kMorphPerformanceMacro,
                                         kDefaultMorphPerformanceMacro));
        mos.writeDouble(getRawParamValue(apvts, ats_param::kMorphSmoothingMs, kDefaultMorphSmoothingMs));
        for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
        {
            mos.writeDouble(getRawParamValue(apvts, ats_param::kMorphLaneAmountIds[i], 0.0f));
            mos.writeByte(getRawParamValue(apvts, ats_param::kMorphLaneLinkedIds[i], 1.0f) > 0.5f
                              ? (juce::uint8) 1
                              : (juce::uint8) 0);
        }
    }

    if (kStateVersion >= 8)
    {
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kMorphGridType, kDefaultMorphGridType));
        mos.writeInt(getRawChoiceValue(apvts,
                                       ats_param::kMorphCorrespondenceStrategy,
                                       kDefaultMorphCorrespondenceStrategy));
        mos.writeInt(getRawChoiceValue(apvts,
                                       ats_param::kMorphUnmatchedSourcePolicy,
                                       kDefaultMorphUnmatchedSourcePolicy));
        mos.writeInt(getRawChoiceValue(apvts,
                                       ats_param::kMorphUnmatchedTargetPolicy,
                                       kDefaultMorphUnmatchedTargetPolicy));
        mos.writeByte(getRawParamValue(apvts, ats_param::kMorphDiscardOptIn, 0.0f) > 0.5f
                          ? (juce::uint8) 1
                          : (juce::uint8) 0);
        mos.writeInt(getRawChoiceValue(apvts,
                                       ats_param::kMorphFrequencyInterpolationDomain,
                                       kDefaultMorphFrequencyDomain));
        mos.writeInt(getRawChoiceValue(apvts,
                                       ats_param::kMorphAmplitudeInterpolationDomain,
                                       kDefaultMorphAmplitudeDomain));
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kMorphPhaseStrategy, kDefaultMorphPhaseStrategy));
        mos.writeInt(getRawChoiceValue(apvts,
                                       ats_param::kMorphTransientPolicy,
                                       kDefaultMorphTransientPolicy));
    }

    if (kStateVersion >= 9)
    {
        const juce::ScopedLock sl(morphSnapshotLock);
        mos.writeInt(ats_param::kMorphVariationSlotCount);
        for (const auto& slot : morphVariationSlots)
        {
            mos.writeByte(slot.hasSnapshot ? (juce::uint8) 1 : (juce::uint8) 0);
            writeStringChunk(mos, slot.name.toStdString());
            mos.writeDouble(slot.morphAmount);
            mos.writeDouble(slot.performanceMacro);
            mos.writeDouble(slot.smoothingMs);
            for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
            {
                mos.writeDouble(slot.laneAmounts[i]);
                mos.writeByte(slot.laneLinked[i] ? (juce::uint8) 1 : (juce::uint8) 0);
            }
        }
    }

    if (kStateVersion >= 10)
    {
        std::array<juce::File, 2> sourceFiles;
        {
            const juce::ScopedLock sl(sourceLock);
            sourceFiles = morphSourceFiles;
        }
        for (const auto& file : sourceFiles)
            writeStringChunk(mos, file.getFullPathName().toStdString());
    }

    if (kStateVersion >= 12)
    {
        std::array<AtsResidualDiagnostics, 2> diagnostics;
        std::array<std::vector<ats::MorphResidualNoiseBand>, 2> residualBands;
        {
            const juce::ScopedLock ml(morphRecipeLock);
            diagnostics = morphSourceDiagnostics;
            residualBands = morphSourceResidualBands;
        }

        mos.writeInt((int) diagnostics.size());
        for (size_t i = 0; i < diagnostics.size(); ++i)
            writeResidualState(mos, diagnostics[i], residualBands[i]);
    }

    if (kStateVersion >= 14)
    {
        // Smearnov parameters + active control-cluster tab (SMEARNOV_SPEC §4).
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovBlurMs, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovSharpenAmount, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovExaggerateDb, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovMaxDb, 0.0f));
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kSmearnovTargets, 0));
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kSmearnovScope, 0));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovWeightDetFlux, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovWeightResFlux, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovWeightHfcFlux, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovWeightBirth, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovWeightPhaseDev, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovThresholdLambda, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovThresholdAutoScale, 0.0f));
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovPreMarginMs, 0.0f));
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kSmearnovPeakWindowFrames, 0));
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kSmearnovMinGapFrames, 0));
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kSmearnovMaxAttackFrames, 0));
        mos.writeByte((char) (getRawParamValue(apvts, ats_param::kSmearnovUsePhaseDeviation, 0.0f) > 0.5f ? 1 : 0));
        mos.writeByte((char) (getRawParamValue(apvts, ats_param::kSmearnovUseRpcaFallback, 0.0f) > 0.5f ? 1 : 0));
        mos.writeInt(activeControlTab.load(std::memory_order_acquire));
    }

    if (kStateVersion >= 15)
        mos.writeDouble((double) getRawParamValue(apvts, ats_param::kSmearnovMix, 100.0f));

    if (kStateVersion >= 16)
        mos.writeInt(getRawChoiceValue(apvts, ats_param::kMorphQuality, kDefaultMorphQuality));

    destData = mos.getMemoryBlock();
}

void AtsAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restoring a session installs a fresh model + parameter tree; any in-memory
    // undo history belongs to the previous session and must not survive.
    undoManager.clearUndoHistory();
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

    ats::FrequencyShapeSettings frequencyShape;
    if (ver >= 13 && !readFrequencyShape(mis, frequencyShape))
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

    int classicPreviewMode = kDefaultClassicPreviewMode;
    if (ver >= 11)
    {
        if (mis.getNumBytesRemaining() < (juce::int64) sizeof(int))
            return;
        classicPreviewMode = mis.readInt();
    }

    std::string morphRecipeText;
    if (ver >= 5)
    {
        if (!readStringChunk(mis, morphRecipeText))
            return;
    }

    float morphAmount = 0.0f;
    if (ver >= 6)
    {
        if (mis.getNumBytesRemaining() < (juce::int64) sizeof(double))
            return;
        morphAmount = (float) mis.readDouble();
    }

    float morphPerformanceMacro = kDefaultMorphPerformanceMacro;
    float morphSmoothingMs = kDefaultMorphSmoothingMs;
    std::array<float, ats_param::kMorphLaneCount> morphLaneAmounts {};
    std::array<bool, ats_param::kMorphLaneCount> morphLaneLinked {};
    morphLaneLinked.fill(true);
    bool hasExplicitLaneState = false;
    if (ver >= 7)
    {
        if (mis.getNumBytesRemaining() < (juce::int64) (sizeof(double) * 2))
            return;
        morphPerformanceMacro = (float) mis.readDouble();
        morphSmoothingMs = (float) mis.readDouble();
        for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
        {
            if (mis.getNumBytesRemaining() < (juce::int64) (sizeof(double) + 1))
                return;
            morphLaneAmounts[i] = (float) mis.readDouble();
            morphLaneLinked[i] = mis.readByte() != 0;
        }
        hasExplicitLaneState = true;
    }

    int morphGridType = kDefaultMorphGridType;
    int morphCorrespondenceStrategy = kDefaultMorphCorrespondenceStrategy;
    int morphQuality = kDefaultMorphQuality;
    int morphUnmatchedSourcePolicy = kDefaultMorphUnmatchedSourcePolicy;
    int morphUnmatchedTargetPolicy = kDefaultMorphUnmatchedTargetPolicy;
    bool morphDiscardOptIn = false;
    int morphFrequencyDomain = kDefaultMorphFrequencyDomain;
    int morphAmplitudeDomain = kDefaultMorphAmplitudeDomain;
    int morphPhaseStrategy = kDefaultMorphPhaseStrategy;
    int morphTransientPolicy = kDefaultMorphTransientPolicy;
    bool hasExplicitPolicyState = false;
    if (ver >= 8)
    {
        if (mis.getNumBytesRemaining() < (juce::int64) (sizeof(int) * 8 + 1))
            return;
        morphGridType = mis.readInt();
        morphCorrespondenceStrategy = mis.readInt();
        morphUnmatchedSourcePolicy = mis.readInt();
        morphUnmatchedTargetPolicy = mis.readInt();
        morphDiscardOptIn = mis.readByte() != 0;
        morphFrequencyDomain = mis.readInt();
        morphAmplitudeDomain = mis.readInt();
        morphPhaseStrategy = mis.readInt();
        morphTransientPolicy = mis.readInt();
        hasExplicitPolicyState = true;
    }

    std::array<AtsMorphSnapshotSlotInfo, ats_param::kMorphVariationSlotCount> loadedVariationSlots;
    bool hasExplicitVariationSlots = false;
    if (ver >= 9)
    {
        if (mis.getNumBytesRemaining() < (juce::int64) sizeof(int))
            return;

        const int slotCount = mis.readInt();
        if (slotCount < 0 || slotCount > 16)
            return;

        for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        {
            if (mis.getNumBytesRemaining() < 1)
                return;

            AtsMorphSnapshotSlotInfo slot;
            slot.hasSnapshot = mis.readByte() != 0;
            std::string slotName;
            if (!readStringChunk(mis, slotName))
                return;
            slot.name = juce::String(slotName.c_str());

            if (mis.getNumBytesRemaining() < (juce::int64) (sizeof(double) * 3))
                return;
            slot.morphAmount = (float) mis.readDouble();
            slot.performanceMacro = (float) mis.readDouble();
            slot.smoothingMs = (float) mis.readDouble();

            for (size_t laneIndex = 0; laneIndex < ats_param::kMorphLaneCount; ++laneIndex)
            {
                if (mis.getNumBytesRemaining() < (juce::int64) (sizeof(double) + 1))
                    return;
                slot.laneAmounts[laneIndex] = (float) mis.readDouble();
                slot.laneLinked[laneIndex] = mis.readByte() != 0;
            }

            if (slotIndex < ats_param::kMorphVariationSlotCount)
                loadedVariationSlots[(size_t) slotIndex] = slot;
        }
        hasExplicitVariationSlots = true;
    }

    std::array<juce::File, 2> loadedMorphSourceFiles;
    if (ver >= 10)
    {
        for (auto& file : loadedMorphSourceFiles)
        {
            std::string path;
            if (!readStringChunk(mis, path))
                return;
            if (!path.empty())
                file = juce::File(juce::String(path.c_str()));
        }
    }

    std::array<AtsResidualDiagnostics, 2> loadedMorphSourceDiagnostics;
    std::array<std::vector<ats::MorphResidualNoiseBand>, 2> loadedMorphSourceResidualBands;
    bool hasExplicitResidualState = false;
    if (ver >= 12)
    {
        if (mis.getNumBytesRemaining() < (juce::int64) sizeof(int))
            return;

        const int slotCount = mis.readInt();
        if (slotCount < 0 || slotCount > 16)
            return;

        for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        {
            AtsResidualDiagnostics diagnostics;
            std::vector<ats::MorphResidualNoiseBand> bands;
            if (!readResidualState(mis, diagnostics, bands))
                return;

            if (slotIndex < (int) loadedMorphSourceDiagnostics.size())
            {
                loadedMorphSourceDiagnostics[(size_t) slotIndex] = diagnostics;
                loadedMorphSourceResidualBands[(size_t) slotIndex] = std::move(bands);
            }
        }
        hasExplicitResidualState = true;
    }

    struct SmearnovState
    {
        double blurMs = 0.0, sharpen = 0.0, exaggerateDb = 0.0, maxDb = 0.0;
        int targets = 0, scope = 0;
        double wDet = 0.0, wRes = 0.0, wHfc = 0.0, wBirth = 0.0, wPhase = 0.0;
        double threshLambda = 0.0, threshScale = 0.0, preMarginMs = 0.0;
        int peakWindow = 0, minGap = 0, maxAttack = 0;
        bool usePhaseDev = false, useRpcaFallback = false;
        int controlTab = 0;
        double mix = 100.0; // percent; older states predate the dry/wet mix (full effect)
    } smearnov;
    bool hasSmearnovState = false;
    if (ver >= 14)
    {
        // 4 doubles + 2 ints + 8 doubles + 3 ints + 2 bytes + 1 int.
        const juce::int64 needed = (juce::int64) (12 * sizeof(double) + 6 * sizeof(int) + 2);
        if (mis.getNumBytesRemaining() < needed)
            return;

        smearnov.blurMs = mis.readDouble();
        smearnov.sharpen = mis.readDouble();
        smearnov.exaggerateDb = mis.readDouble();
        smearnov.maxDb = mis.readDouble();
        smearnov.targets = mis.readInt();
        smearnov.scope = mis.readInt();
        smearnov.wDet = mis.readDouble();
        smearnov.wRes = mis.readDouble();
        smearnov.wHfc = mis.readDouble();
        smearnov.wBirth = mis.readDouble();
        smearnov.wPhase = mis.readDouble();
        smearnov.threshLambda = mis.readDouble();
        smearnov.threshScale = mis.readDouble();
        smearnov.preMarginMs = mis.readDouble();
        smearnov.peakWindow = mis.readInt();
        smearnov.minGap = mis.readInt();
        smearnov.maxAttack = mis.readInt();
        smearnov.usePhaseDev = mis.readByte() != 0;
        smearnov.useRpcaFallback = mis.readByte() != 0;
        smearnov.controlTab = mis.readInt();
        hasSmearnovState = true;

        if (ver >= 15 && mis.getNumBytesRemaining() >= (juce::int64) sizeof(double))
            smearnov.mix = mis.readDouble();
    }

    if (ver >= 16 && mis.getNumBytesRemaining() >= (juce::int64) sizeof(int))
        morphQuality = mis.readInt();

    setChoiceParameter(apvts, ats_param::kAnalysisType, juce::jlimit(1, 4, ap.type) - 1);
    setParameterValue(apvts, ats_param::kAnalysisStart, (float) ap.start);
    setParameterValue(apvts, ats_param::kAnalysisDuration, (float) ap.duration);
    setParameterValue(apvts, ats_param::kAnalysisLowestFreq, (float) ap.lowest_freq);
    setParameterValue(apvts, ats_param::kAnalysisHighestFreq, (float) ap.highest_freq);
    setParameterValue(apvts, ats_param::kAnalysisFreqDev, (float) ap.freq_dev);
    setParameterValue(apvts, ats_param::kAnalysisWinCycles, (float) ap.win_cycles);
    setChoiceParameter(apvts, ats_param::kAnalysisWinType, juce::jlimit(0, 3, ap.win_type));
    setParameterValue(apvts, ats_param::kAnalysisHopSize, (float) ap.hop_size);
    setParameterValue(apvts, ats_param::kAnalysisLowestMag, (float) ap.lowest_mag);
    setParameterValue(apvts, ats_param::kAnalysisTrackLen, (float) ap.track_len);
    setParameterValue(apvts, ats_param::kAnalysisMinSegLen, (float) ap.min_seg_len);
    setParameterValue(apvts, ats_param::kAnalysisMinGapLen, (float) ap.min_gap_len);
    setParameterValue(apvts, ats_param::kAnalysisSmrThreshold, (float) ap.SMR_thres);
    setParameterValue(apvts, ats_param::kAnalysisMinSegSmr, (float) ap.min_seg_SMR);
    setParameterValue(apvts, ats_param::kAnalysisLastPeakContribution, (float) ap.last_peak_cont);
    setParameterValue(apvts, ats_param::kAnalysisSmrContribution, (float) ap.SMR_cont);

    if (auto* p = apvts.getParameter("partGain"))
        p->setValueNotifyingHost(apvts.getParameterRange("partGain").convertTo0to1((float) sp.partialsGain));
    if (auto* p = apvts.getParameter("resGain"))
        p->setValueNotifyingHost(apvts.getParameterRange("resGain").convertTo0to1((float) sp.residualGain));
    if (auto* p = apvts.getParameter("freq"))
        p->setValueNotifyingHost(apvts.getParameterRange("freq").convertTo0to1((float) sp.frequencyFactor));
    setChoiceParameter(apvts, ats_param::kFreqShapeMode, (int) frequencyShape.mode);
    setParameterValue(apvts, ats_param::kFreqShapeAmount, (float) frequencyShape.amount);
    setParameterValue(apvts, ats_param::kFreqShapePivotHz, (float) frequencyShape.pivotHz);
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
    setChoiceParameter(apvts, ats_param::kClassicPreviewMode, classicPreviewMode);
    if (auto* p = apvts.getParameter("outputTrim"))
        p->setValueNotifyingHost(apvts.getParameterRange("outputTrim").convertTo0to1(outputTrim));

    if (hasSmearnovState)
    {
        setParameterValue(apvts, ats_param::kSmearnovBlurMs, (float) smearnov.blurMs);
        setParameterValue(apvts, ats_param::kSmearnovSharpenAmount, (float) smearnov.sharpen);
        setParameterValue(apvts, ats_param::kSmearnovExaggerateDb, (float) smearnov.exaggerateDb);
        setParameterValue(apvts, ats_param::kSmearnovMix, (float) smearnov.mix);
        setParameterValue(apvts, ats_param::kSmearnovMaxDb, (float) smearnov.maxDb);
        setChoiceParameter(apvts, ats_param::kSmearnovTargets, smearnov.targets);
        setChoiceParameter(apvts, ats_param::kSmearnovScope, smearnov.scope);
        setParameterValue(apvts, ats_param::kSmearnovWeightDetFlux, (float) smearnov.wDet);
        setParameterValue(apvts, ats_param::kSmearnovWeightResFlux, (float) smearnov.wRes);
        setParameterValue(apvts, ats_param::kSmearnovWeightHfcFlux, (float) smearnov.wHfc);
        setParameterValue(apvts, ats_param::kSmearnovWeightBirth, (float) smearnov.wBirth);
        setParameterValue(apvts, ats_param::kSmearnovWeightPhaseDev, (float) smearnov.wPhase);
        setParameterValue(apvts, ats_param::kSmearnovThresholdLambda, (float) smearnov.threshLambda);
        setParameterValue(apvts, ats_param::kSmearnovThresholdAutoScale, (float) smearnov.threshScale);
        setParameterValue(apvts, ats_param::kSmearnovPreMarginMs, (float) smearnov.preMarginMs);
        setChoiceParameter(apvts, ats_param::kSmearnovPeakWindowFrames, smearnov.peakWindow);
        setChoiceParameter(apvts, ats_param::kSmearnovMinGapFrames, smearnov.minGap);
        setChoiceParameter(apvts, ats_param::kSmearnovMaxAttackFrames, smearnov.maxAttack);
        setParameterBool(apvts, ats_param::kSmearnovUsePhaseDeviation, smearnov.usePhaseDev);
        setParameterBool(apvts, ats_param::kSmearnovUseRpcaFallback, smearnov.useRpcaFallback);
        setActiveControlTab(smearnov.controlTab);
    }

    {
        const juce::ScopedLock ml(morphRecipeLock);
        if (!morphRecipeText.empty())
        {
            ats::MorphRecipe parsedRecipe;
            std::string parseError;
            if (ats::deserializeMorphRecipe(morphRecipeText, parsedRecipe, &parseError))
            {
                morphRecipe = parsedRecipe;
                serializedMorphRecipe = morphRecipeText;
            }
            else
            {
                morphRecipe = ats::makeDefaultMorphRecipe();
                serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
            }
        }
        else
        {
            morphRecipe = ats::makeDefaultMorphRecipe();
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
        morphRecipeRevision.fetch_add(1, std::memory_order_release);
    }

    if (ver < 6)
    {
        const juce::ScopedLock ml(morphRecipeLock);
        morphAmount = (float) morphRecipe.defaultMorphAmount;
    }

    {
        const juce::ScopedLock ml(morphRecipeLock);
        if (!hasExplicitLaneState)
        {
            for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
            {
                morphLaneAmounts[i] = (float) morphRecipe.lanes[i].amount;
                morphLaneLinked[i] = morphRecipe.lanes[i].linkedToMainMorph;
            }
        }
        else
        {
            morphRecipe.defaultMorphAmount = morphAmount;
            for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
            {
                morphRecipe.lanes[i].amount = morphLaneAmounts[i];
                morphRecipe.lanes[i].linkedToMainMorph = morphLaneLinked[i];
            }
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }

        if (!hasExplicitPolicyState)
        {
            morphGridType = morphRecipe.frequencyGrid.type == ats::MorphFrequencyGridType::Mel ? 1 : 0;
            morphUnmatchedSourcePolicy = unmatchedPolicyToChoice(morphRecipe.unmatchedPartialPolicies.sourcePolicy);
            morphUnmatchedTargetPolicy = unmatchedPolicyToChoice(morphRecipe.unmatchedPartialPolicies.targetPolicy);
            morphDiscardOptIn = morphRecipe.unmatchedPartialPolicies.discardOptIn;
            morphPhaseStrategy = phaseStrategyToChoice(morphRecipe.phase.strategy);
            morphTransientPolicy = transientPolicyToChoice(morphRecipe.transient.policy);
        }
        else
        {
            const auto currentGrid = morphRecipe.frequencyGrid;
            if (morphGridType == 1)
                morphRecipe.frequencyGrid = ats::makeMelFrequencyGrid(currentGrid.bandCount,
                                                                       currentGrid.lowestFrequencyHz,
                                                                       currentGrid.highestFrequencyHz);
            else
                morphRecipe.frequencyGrid = ats::makeBarkFrequencyGrid(currentGrid.bandCount,
                                                                        currentGrid.lowestFrequencyHz,
                                                                        currentGrid.highestFrequencyHz);

            morphRecipe.unmatchedPartialPolicies.sourcePolicy =
                unmatchedPolicyFromChoice(morphUnmatchedSourcePolicy);
            morphRecipe.unmatchedPartialPolicies.targetPolicy =
                unmatchedPolicyFromChoice(morphUnmatchedTargetPolicy);
            morphRecipe.unmatchedPartialPolicies.discardOptIn = morphDiscardOptIn;
            if (!morphRecipe.unmatchedPartialPolicies.discardOptIn)
            {
                if (morphRecipe.unmatchedPartialPolicies.sourcePolicy == ats::MorphUnmatchedPartialPolicy::Discard)
                    morphRecipe.unmatchedPartialPolicies.sourcePolicy = ats::MorphUnmatchedPartialPolicy::FadeOut;
                if (morphRecipe.unmatchedPartialPolicies.targetPolicy == ats::MorphUnmatchedPartialPolicy::Discard)
                    morphRecipe.unmatchedPartialPolicies.targetPolicy = ats::MorphUnmatchedPartialPolicy::FadeIn;
            }
            morphRecipe.phase.strategy = phaseStrategyFromChoice(morphPhaseStrategy);
            morphRecipe.transient.policy = transientPolicyFromChoice(morphTransientPolicy);
            upsertRecipeUnknownField(morphRecipe,
                                     "ui.correspondence_strategy",
                                     correspondenceStrategyName(morphCorrespondenceStrategy));
            upsertRecipeUnknownField(morphRecipe,
                                     "ui.frequency_interpolation_domain",
                                     morphFrequencyDomain == 1 ? "bark_coordinate" : "log_frequency");
            upsertRecipeUnknownField(morphRecipe,
                                     "ui.amplitude_interpolation_domain",
                                     morphAmplitudeDomain == 1 ? "decibels" : "power");
            upsertRecipeUnknownField(morphRecipe,
                                     "ui.discard_opt_in",
                                     morphRecipe.unmatchedPartialPolicies.discardOptIn ? "1" : "0");
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }

    setParameterValue(apvts, ats_param::kMorphAmount, morphAmount);
    setParameterValue(apvts, ats_param::kMorphPerformanceMacro, morphPerformanceMacro);
    setParameterValue(apvts, ats_param::kMorphSmoothingMs, morphSmoothingMs);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        setParameterValue(apvts, ats_param::kMorphLaneAmountIds[i], morphLaneAmounts[i]);
        setParameterBool(apvts, ats_param::kMorphLaneLinkedIds[i], morphLaneLinked[i]);
    }
    setChoiceParameter(apvts, ats_param::kMorphGridType, morphGridType);
    setChoiceParameter(apvts, ats_param::kMorphCorrespondenceStrategy, morphCorrespondenceStrategy);
    setChoiceParameter(apvts, ats_param::kMorphQuality, morphQuality);
    setChoiceParameter(apvts, ats_param::kMorphUnmatchedSourcePolicy, morphUnmatchedSourcePolicy);
    setChoiceParameter(apvts, ats_param::kMorphUnmatchedTargetPolicy, morphUnmatchedTargetPolicy);
    setParameterBool(apvts, ats_param::kMorphDiscardOptIn, morphDiscardOptIn);
    setChoiceParameter(apvts, ats_param::kMorphFrequencyInterpolationDomain, morphFrequencyDomain);
    setChoiceParameter(apvts, ats_param::kMorphAmplitudeInterpolationDomain, morphAmplitudeDomain);
    setChoiceParameter(apvts, ats_param::kMorphPhaseStrategy, morphPhaseStrategy);
    setChoiceParameter(apvts, ats_param::kMorphTransientPolicy, morphTransientPolicy);

    if (hasExplicitVariationSlots)
    {
        const juce::ScopedLock sl(morphSnapshotLock);
        morphVariationSlots = loadedVariationSlots;
    }
    if (ver >= 10)
    {
        const juce::ScopedLock sl(sourceLock);
        morphSourceFiles = loadedMorphSourceFiles;
    }
    {
        const juce::ScopedLock ml(morphRecipeLock);
        // Per-frame ATS data for morph sources is not persisted in state; clear so
        // the trajectory renderer falls back to static snapshots until re-analyzed.
        morphSourceSounds[0] = ats::AtsSoundPtr{};
        morphSourceSounds[1] = ats::AtsSoundPtr{};
        if (hasExplicitResidualState)
        {
            morphSourceDiagnostics = loadedMorphSourceDiagnostics;
            morphSourceResidualBands = std::move(loadedMorphSourceResidualBands);
        }
        else
        {
            morphSourceDiagnostics = {};
            morphSourceResidualBands = {};
        }
    }
    morphSnapshotState.store((int) AtsMorphSnapshotUiState::Ready, std::memory_order_release);

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
            currentModelName = "Restored ATS model";
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
            currentModelName.clear();
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
        currentModelName.clear();
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

    clearSmearnovOnsetOverrides(AtsTraceSource::Current);

    // The parameter writes above are routed through the UndoManager; drop them so
    // a freshly restored session starts with an empty undo history.
    undoManager.clearUndoHistory();
    undoManager.beginNewTransaction();
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
    ScopedModelEdit edit(*this, AtsTraceSource::Current, "Edit Partial Cell");
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

    // Arm an undo snapshot (message thread); recorded in handleAsyncUpdate on success.
    asyncUndoBefore = captureModelSnapshot(AtsTraceSource::Current);
    asyncUndoSource = AtsTraceSource::Current;
    asyncUndoLabel = "Run Math Script";
    asyncUndoCommit.store(false, std::memory_order_relaxed);

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
            asyncUndoCommit.store(true, std::memory_order_release);
            triggerAsyncUpdate();
        }
        setUiState(AtsUiState::Analyzed);
    });
}

bool AtsAudioProcessor::detectOnsets(AtsTraceSource source,
                                     const ats::SmearnovDetectParams& params,
                                     std::vector<ats::AtsOnset>& out,
                                     juce::String& error) const
{
    error.clear();
    out.clear();

    ats::AtsSoundPtr snapshot;
    if (source == AtsTraceSource::Current)
    {
        const juce::ScopedLock ml(modelLock);
        auto* s = analyzedModel.get();
        if (s == nullptr)
        {
            error = "No model.";
            return false;
        }
        snapshot.reset(copy_sound(s));
    }
    else
    {
        const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        if (s == nullptr)
        {
            error = source == AtsTraceSource::SourceA ? "Source A has no model." : "Source B has no model.";
            return false;
        }
        snapshot.reset(copy_sound(s));
    }

    if (snapshot.get() == nullptr)
    {
        error = "Could not copy ATS model for onset detection.";
        return false;
    }

    out = ats::detectOnsets(snapshot.get(), params);
    return true;
}

bool AtsAudioProcessor::detectOnsets(AtsTraceSource source,
                                     std::vector<ats::AtsOnset>& out,
                                     juce::String& error) const
{
    // RPCA segmentation (C11) is not implemented; the toggle falls through to
    // standard flux detection so a stale persisted "true" never bricks detect.
    return detectOnsets(source, readSmearnovDetectParamsFromApvts(), out, error);
}

bool AtsAudioProcessor::smearAttacks(AtsTraceSource source,
                                     const std::vector<ats::AtsOnset>& onsets,
                                     const ats::SmearnovParams& params,
                                     juce::String& error)
{
    error.clear();
    if (onsets.empty())
    {
        error = "No onsets to smear.";
        return false;
    }

    if (source == AtsTraceSource::Current)
    {
        bool changed = false;
        {
            const juce::ScopedLock ml(modelLock);
            auto* s = analyzedModel.get();
            if (s == nullptr)
            {
                error = "No model.";
                return false;
            }
            changed = ats::smearAttacks(s, onsets, params);
        }

        if (!changed)
        {
            error = "No current-model attack windows were changed.";
            return false;
        }

        clearPreviewLayerCache();
        modelRevision.fetch_add(1, std::memory_order_relaxed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
        return true;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    bool changed = false;
    ATS_SOUND* editedSound = nullptr;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        editedSound = morphSourceSounds[(size_t) slotIndex].get();
        if (editedSound == nullptr)
        {
            error = source == AtsTraceSource::SourceA ? "Source A has no model." : "Source B has no model.";
            return false;
        }

        changed = ats::smearAttacks(editedSound, onsets, params);
        if (changed)
        {
            morphSourceAssignments[(size_t) slotIndex] =
                makeMorphAssignmentsFromSound(editedSound, morphRecipe.frequencyGrid);
            morphSourceDiagnostics[(size_t) slotIndex] = makeResidualDiagnostics(editedSound);
            morphSourceResidualBands[(size_t) slotIndex] = makeMorphResidualNoiseBands(editedSound);
            rebuildMorphCorrespondenceIfReady();
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }

    if (!changed)
    {
        error = source == AtsTraceSource::SourceA ? "No Source A attack windows were changed."
                                                  : "No Source B attack windows were changed.";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    clearPreviewLayerCache();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AtsAudioProcessor::smearAttacks(AtsTraceSource source,
                                     const std::vector<ats::AtsOnset>& onsets,
                                     juce::String& error)
{
    return smearAttacks(source, onsets, readSmearnovParamsFromApvts(), error);
}

// Filters detected onsets per the Scope selector:
//   0 = All        — keep every onset.
//   1 = Selection  — keep onsets whose attack window [preFrame, peakFrame]
//                    intersects the display selection [selFirst, selLast].
//   2 = Sensitivity — keep onsets at or above the mean detection strength
//                    (the more salient attacks), parameter-free.
// Sets scopeError + msg when Selection is requested with no display selection.
static std::vector<ats::AtsOnset> applySmearnovScope(int scope,
                                                     std::vector<ats::AtsOnset> onsets,
                                                     int selFirst,
                                                     int selLast,
                                                     juce::String& msg,
                                                     bool& scopeError)
{
    scopeError = false;

    if (scope == 1) // Selection
    {
        if (selFirst < 0 || selLast < 0)
        {
            scopeError = true;
            msg = "Select a region in the display, or set Scope to All.";
            onsets.clear();
            return onsets;
        }

        const int lo = std::min(selFirst, selLast);
        const int hi = std::max(selFirst, selLast);
        std::vector<ats::AtsOnset> kept;
        kept.reserve(onsets.size());
        for (const auto& o : onsets)
            if (o.peakFrame >= lo && o.preFrame <= hi)
                kept.push_back(o);
        return kept;
    }

    if (scope == 2) // Sensitivity: keep onsets at or above mean strength
    {
        if (onsets.size() <= 1)
            return onsets;

        double sum = 0.0;
        for (const auto& o : onsets)
            sum += o.strength;
        const double mean = sum / (double) onsets.size();

        std::vector<ats::AtsOnset> kept;
        kept.reserve(onsets.size());
        for (const auto& o : onsets)
            if (o.strength >= mean)
                kept.push_back(o);
        return kept;
    }

    return onsets; // All
}

void AtsAudioProcessor::startSmearnovApply(AtsTraceSource source,
                                           int selectionFirstFrame,
                                           int selectionLastFrame)
{
    cancelJobs();
    joinWorkers();

    const auto detectParams = readSmearnovDetectParamsFromApvts();
    const auto smearParams = readSmearnovParamsFromApvts();
    const int scope = getRawChoiceValue(apvts, ats_param::kSmearnovScope, 0);
    const uint64_t gen = jobGeneration.load();

    // Arm an undo snapshot (message thread); recorded in timerCallback on success.
    asyncUndoBefore = captureModelSnapshot(source);
    asyncUndoSource = source;
    asyncUndoLabel = "Smear Attacks";
    asyncUndoCommit.store(false, std::memory_order_relaxed);

    transformThreadRunning.store(true);
    setUiState(AtsUiState::ApplyingMath);
    setMathStatus("Detecting attacks...");

    transformThread = std::thread([this, source, detectParams, smearParams, scope,
                                   selectionFirstFrame, selectionLastFrame, gen]() {
        juce::String msg;
        bool applied = false;

        {
            std::vector<ats::AtsOnset> onsets;
            // Edited onsets (drag/disable in the overlay) take precedence over
            // a fresh detection pass so the user's window edits are smeared.
            const bool useOverride = copySmearnovOnsetOverride(source, onsets);
            if (useOverride || detectOnsets(source, detectParams, onsets, msg))
            {
                if (jobGeneration.load() != gen)
                {
                    transformThreadRunning.store(false);
                    setMathStatus("Cancelled.");
                    setUiState(AtsUiState::Analyzed);
                    return;
                }

                bool scopeError = false;
                if (!onsets.empty())
                    onsets = applySmearnovScope(scope, std::move(onsets),
                                                selectionFirstFrame, selectionLastFrame,
                                                msg, scopeError);

                if (scopeError)
                {
                    // msg already set by applySmearnovScope (e.g. no selection).
                }
                else if (onsets.empty())
                {
                    msg = "No attacks detected.";
                }
                else if (smearAttacks(source, onsets, smearParams, msg))
                {
                    applied = true;
                    msg = "Smeared " + juce::String((int) onsets.size()) + " attack"
                          + (onsets.size() == 1 ? "." : "s.");
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
            asyncUndoCommit.store(true, std::memory_order_release);
        setUiState(AtsUiState::Analyzed);
    });
}

static int smearnovSourceSlot(AtsTraceSource source)
{
    switch (source)
    {
        case AtsTraceSource::SourceA: return 1;
        case AtsTraceSource::SourceB: return 2;
        case AtsTraceSource::Current:
        default: return 0;
    }
}

size_t AtsAudioProcessor::computeSmearLiveHash() const
{
    const auto d = readSmearnovDetectParamsFromApvts();
    const auto s = readSmearnovParamsFromApvts();
    const int rawScope = getRawChoiceValue(apvts, ats_param::kSmearnovScope, 0);
    const int scope = (rawScope == 1) ? 0 : rawScope; // Selection narrows only the destructive bake

    size_t h = 1469598103934665603ull;
    auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
    auto mixD = [&](double v) { mix(std::hash<double>{}(v)); };
    auto mixI = [&](long long v) { mix(std::hash<long long>{}(v)); };

    mixD(d.weightDetFlux); mixD(d.weightResFlux); mixD(d.weightHfcFlux);
    mixD(d.weightBirth); mixD(d.weightPhaseDev);
    mixI(d.usePhaseDeviation); mixI(d.useSmrWeighting); mixD(d.smrWeightFloor);
    mixI(d.normWindowRadiusFrames); mixI(d.peakWindowRadiusFrames);
    mixD(d.thresholdLambda); mixD(d.thresholdAutoScale); mixD(d.onsetFloor);
    mixD(d.minGapSeconds); mixI(d.minGapFramesOverride);
    mixD(d.preMarginSeconds); mixI(d.maxAttackFrames);

    mixD(s.blurMs); mixD(s.sharpenAmount); mixD(s.exaggerateDb); mixD(s.maxDb);
    mixD(s.mix);
    mixI((long long) s.targets);

    mixI(scope);
    mixI((long long) smearnovOverlayRevision.load(std::memory_order_acquire));
    return h;
}

bool AtsAudioProcessor::applyLiveSmearToClone(AtsTraceSource source, ATS_SOUND* clone) const
{
    if (clone == nullptr)
        return false;

    const auto detect = readSmearnovDetectParamsFromApvts();
    const auto smear = readSmearnovParamsFromApvts();

    std::vector<ats::AtsOnset> onsets;
    if (!copySmearnovOnsetOverride(source, onsets))
        onsets = ats::detectOnsets(clone, detect);

    const int rawScope = getRawChoiceValue(apvts, ats_param::kSmearnovScope, 0);
    const int scope = (rawScope == 1) ? 0 : rawScope; // Selection narrows only the bake; live uses All
    juce::String msg;
    bool scopeError = false;
    if (!onsets.empty())
        onsets = applySmearnovScope(scope, std::move(onsets), -1, -1, msg, scopeError);

    if (onsets.empty())
        return false;

    return ats::smearAttacks(clone, onsets, smear);
}

const ATS_SOUND* AtsAudioProcessor::getSmearedSound(AtsTraceSource source, juce::String& error)
{
    error.clear();
    const juce::ScopedLock cl(smearedCacheLock);

    SmearedSourceCache* cache = nullptr;
    uint64_t rev = 0;
    switch (source)
    {
        case AtsTraceSource::SourceA: cache = &smearedA; rev = morphSourcesRevision.load(std::memory_order_acquire); break;
        case AtsTraceSource::SourceB: cache = &smearedB; rev = morphSourcesRevision.load(std::memory_order_acquire); break;
        case AtsTraceSource::Current:
        default:                      cache = &smearedCurrent; rev = (uint64_t) modelRevision.load(std::memory_order_acquire); break;
    }

    const size_t hash = computeSmearLiveHash();
    if (cache->valid && cache->sourceRevision == rev && cache->paramHash == hash)
        return cache->sound.get(); // may be nullptr (cached "no change")

    // Snapshot the source under its own lock, then smear the private copy unlocked.
    ats::AtsSoundPtr clone;
    if (source == AtsTraceSource::Current)
    {
        const juce::ScopedLock ml(modelLock);
        if (analyzedModel.get() == nullptr)
        {
            error = "No analyzed ATS model to smear.";
            return nullptr;
        }
        clone.reset(copy_sound(analyzedModel.get()));
    }
    else
    {
        const int slot = source == AtsTraceSource::SourceA ? 0 : 1;
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slot].get();
        if (s == nullptr)
        {
            error = slot == 0 ? "Source A has no model." : "Source B has no model.";
            return nullptr;
        }
        clone.reset(copy_sound(s));
    }

    if (clone.get() == nullptr)
    {
        error = "Could not copy ATS model for smear preview.";
        return nullptr;
    }

    const bool changed = applyLiveSmearToClone(source, clone.get());
    cache->sourceRevision = rev;
    cache->paramHash = hash;
    cache->valid = true;
    if (!changed)
    {
        cache->sound.reset();
        return nullptr; // caller renders the unmodified source
    }
    cache->sound = std::move(clone);
    return cache->sound.get();
}

bool AtsAudioProcessor::copySmearnovOnsetOverride(AtsTraceSource source,
                                                  std::vector<ats::AtsOnset>& out) const
{
    const int slot = smearnovSourceSlot(source);
    const juce::ScopedLock sl(smearnovOnsetLock);
    if (!smearnovOnsetOverrideActive[(size_t) slot])
    {
        out.clear();
        return false;
    }
    out = smearnovOnsetOverrides[(size_t) slot];
    return true;
}

bool AtsAudioProcessor::hasSmearnovOnsetOverrides(AtsTraceSource source) const
{
    const int slot = smearnovSourceSlot(source);
    const juce::ScopedLock sl(smearnovOnsetLock);
    return smearnovOnsetOverrideActive[(size_t) slot];
}

std::vector<ats::AtsOnset> AtsAudioProcessor::getSmearnovOnsets(AtsTraceSource source) const
{
    std::vector<ats::AtsOnset> onsets;
    if (copySmearnovOnsetOverride(source, onsets))
        return onsets;

    juce::String error;
    if (detectOnsets(source, onsets, error))
        return onsets;
    onsets.clear();
    return onsets;
}

void AtsAudioProcessor::setSmearnovOnsetOverrides(AtsTraceSource source,
                                                  std::vector<ats::AtsOnset> onsets,
                                                  bool userEdited)
{
    const int slot = smearnovSourceSlot(source);
    {
        const juce::ScopedLock sl(smearnovOnsetLock);
        smearnovOnsetOverrides[(size_t) slot] = std::move(onsets);
        smearnovOnsetOverrideActive[(size_t) slot] = true;
        smearnovOnsetOverrideUserEdited[(size_t) slot] = userEdited;
    }
    smearnovOverlayRevision.fetch_add(1, std::memory_order_release);
}

void AtsAudioProcessor::clearSmearnovOnsetOverrides(AtsTraceSource source)
{
    const int slot = smearnovSourceSlot(source);
    bool changed = false;
    {
        const juce::ScopedLock sl(smearnovOnsetLock);
        if (smearnovOnsetOverrideActive[(size_t) slot])
        {
            smearnovOnsetOverrideActive[(size_t) slot] = false;
            smearnovOnsetOverrideUserEdited[(size_t) slot] = false;
            smearnovOnsetOverrides[(size_t) slot].clear();
            changed = true;
        }
    }
    if (changed)
        smearnovOverlayRevision.fetch_add(1, std::memory_order_release);
}

bool AtsAudioProcessor::isSmearnovDetectionParameter(const juce::String& parameterID)
{
    for (const char* id : kSmearnovDetectionParamIds)
        if (parameterID == id)
            return true;
    return false;
}

void AtsAudioProcessor::invalidateAutoDetectedOnsetOverrides()
{
    bool changed = false;
    {
        const juce::ScopedLock sl(smearnovOnsetLock);
        for (size_t slot = 0; slot < smearnovOnsetOverrideActive.size(); ++slot)
        {
            if (smearnovOnsetOverrideActive[slot] && !smearnovOnsetOverrideUserEdited[slot])
            {
                smearnovOnsetOverrideActive[slot] = false;
                smearnovOnsetOverrides[slot].clear();
                changed = true;
            }
        }
    }
    if (changed)
        smearnovOverlayRevision.fetch_add(1, std::memory_order_release);
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

bool AtsAudioProcessor::buildSpectrogramTexture(bool partialsMode, int width, int height, juce::Image& out)
{
    if (width <= 0 || height <= 0)
        return false;

    out = juce::Image(juce::Image::ARGB, width, height, true);
    out.clear(juce::Rectangle<int>(width, height), juce::Colours::black);

    const juce::ScopedLock ml(modelLock);
    auto* s = analyzedModel.get();
    if (s == nullptr || s->frames <= 0)
        return false;

    juce::Image::BitmapData bd(out, juce::Image::BitmapData::writeOnly);

    const float dbMin = -90.0f;
    const float dbMax = -10.0f;

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

static void resolveSpectralTraceDisplayScaling(const AtsSpectralTraceDisplayReference& displayReference,
                                               double measuredMaxAmp,
                                               double measuredPeakEnergy,
                                               double& ampReferencePeak,
                                               double& maxAmpDb,
                                               double& minAmpDb,
                                               double& ampDbRange,
                                               double& residualReferencePeak,
                                               double& peakDb,
                                               double& floorDb,
                                               bool& hasResidualEnergy)
{
    ampReferencePeak = displayReference.ampPeak > 0.0 ? displayReference.ampPeak : measuredMaxAmp;
    maxAmpDb = 20.0 * std::log10(juce::jmax(1.0e-15, ampReferencePeak));
    minAmpDb = maxAmpDb - 72.0;
    ampDbRange = juce::jmax(1.0, maxAmpDb - minAmpDb);

    residualReferencePeak = displayReference.residualPeak > 0.0 ? displayReference.residualPeak
                                                                : measuredPeakEnergy;
    hasResidualEnergy = measuredPeakEnergy > 0.0 || residualReferencePeak > 0.0;
    peakDb = 10.0 * std::log10(juce::jmax(1.0e-20, residualReferencePeak));
    floorDb = peakDb - 72.0;
}

static bool buildSpectralTraceSnapshotForSound(const ATS_SOUND* s,
                                               AtsSpectralTraceSnapshot& out,
                                               int maxPartials,
                                               int maxPointsPerPartial,
                                               int residualColumns,
                                               const AtsSpectralTraceDisplayReference& displayReference,
                                               const ats::FrequencyShapeSettings& frequencyShape,
                                               const ats::SmearnovDetectParams& onsetDetectParams,
                                               uint64_t shapeRevision,
                                               const std::vector<ats::AtsOnset>* onsetOverride)
{
    out = {};
    if (s == nullptr || s->frames <= 0)
        return false;

    const auto resolvedShape = ats::resolveFrequencyShapeSettingsForSound(s, frequencyShape);
    out.frequencyShapeMode = resolvedShape.mode;
    out.frequencyShapeAmount = resolvedShape.amount;
    out.frequencyShapePivotHz = resolvedShape.pivotHz;
    out.resolvedFrequencyShapePivotHz = resolvedShape.resolvedPivotHz;
    out.displayShapeRevision = shapeRevision;
    out.partialCount = s->partials;
    out.frameCount = s->frames;
    out.durationSeconds = s->dur;
    out.hasResidual = (s->band_energy != nullptr);
    ats::SmearnovDetectionFunctions onsetFunctions;
    auto detectedOnsets = ats::detectOnsets(s, onsetDetectParams, &onsetFunctions);
    // User edits (drag/disable) take precedence over auto-detection so the
    // overlay reflects the edited window; detection curves still come from the
    // raw detector for the foot-of-plot display.
    out.onsets = onsetOverride != nullptr ? *onsetOverride : std::move(detectedOnsets);
    out.smearnovDetectionCurve = std::move(onsetFunctions.combined);
    out.smearnovThresholdCurve = std::move(onsetFunctions.threshold);

    if (s->partials <= 0 || s->frq == nullptr || s->amp == nullptr)
        return true;

    struct Candidate
    {
        int partialIndex = -1;
        double peakAmp = 0.0;
        double weightedHz = 0.0;
        double ampSum = 0.0;
        int activeFrames = 0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve((size_t) s->partials);

    double minHz = std::numeric_limits<double>::max();
    double maxHz = 0.0;
    double maxAmp = 0.0;
    for (int p = 0; p < s->partials; ++p)
    {
        if (s->frq[p] == nullptr || s->amp[p] == nullptr)
            continue;

        Candidate candidate;
        candidate.partialIndex = p;
        for (int f = 0; f < s->frames; ++f)
        {
            const double amp = s->amp[p][f];
            const double hz = s->frq[p][f];
            if (!std::isfinite(amp) || amp <= 0.0 || !std::isfinite(hz) || hz <= 0.0)
                continue;

            candidate.peakAmp = std::max(candidate.peakAmp, amp);
            candidate.weightedHz += hz * amp;
            candidate.ampSum += amp;
            ++candidate.activeFrames;
            minHz = std::min(minHz, hz);
            maxHz = std::max(maxHz, hz);
            maxAmp = std::max(maxAmp, amp);
        }

        if (candidate.activeFrames > 0)
            candidates.push_back(candidate);
    }

    if (candidates.empty())
        return true;

    const auto rankByPartial = ats::buildFrequencyShapeRanks(s);
    const double nyquistHz = s->srate > 0 ? (double) s->srate * 0.5 : 22050.0;
    auto rankForPartial = [&](int partialIndex) {
        return partialIndex >= 0 && partialIndex < (int) rankByPartial.size()
                   ? rankByPartial[(size_t) partialIndex]
                   : 0;
    };
    auto displayFrequencyFor = [&](double hz, int partialIndex) {
        return ats::applyFrequencyShape(hz, rankForPartial(partialIndex), resolvedShape, nyquistHz);
    };

    if (ats::isFrequencyShapeModeEnabled(resolvedShape))
    {
        minHz = std::numeric_limits<double>::max();
        maxHz = 0.0;
        for (const auto& candidate : candidates)
        {
            const int p = candidate.partialIndex;
            for (int f = 0; f < s->frames; ++f)
            {
                const double amp = s->amp[p][f];
                const double hz = s->frq[p][f];
                if (!std::isfinite(amp) || amp <= 0.0 || !std::isfinite(hz) || hz <= 0.0)
                    continue;

                const double displayHz = displayFrequencyFor(hz, p);
                if (std::isfinite(displayHz) && displayHz > 0.0)
                {
                    minHz = std::min(minHz, displayHz);
                    maxHz = std::max(maxHz, displayHz);
                }
            }
        }
    }

    if (!std::isfinite(minHz) || minHz <= 0.0 || maxHz <= minHz)
    {
        minHz = 20.0;
        maxHz = juce::jmax(20000.0, (double) s->srate * 0.5);
    }
    else
    {
        const double logMin = std::log(juce::jmax(1.0, minHz));
        const double logMax = std::log(juce::jmax(minHz * 1.01, maxHz));
        const double pad = juce::jmax(0.04, (logMax - logMin) * 0.06);
        minHz = std::exp(logMin - pad);
        maxHz = std::exp(logMax + pad);
    }

    out.minFrequencyHz = juce::jmax(1.0, minHz);
    out.maxFrequencyHz = juce::jmax(out.minFrequencyHz * 1.2, maxHz);

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.peakAmp > b.peakAmp;
    });

    if (maxPartials > 0 && (int) candidates.size() > maxPartials)
        candidates.resize((size_t) maxPartials);

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.peakAmp < b.peakAmp;
    });

    const int pointBudget = juce::jmax(32, maxPointsPerPartial);
    const int pointStep = juce::jmax(1, (int) std::ceil((double) s->frames / (double) pointBudget));
    const double logRange = std::log(out.maxFrequencyHz / out.minFrequencyHz);

    double peakEnergy = 0.0;
    if (s->band_energy != nullptr)
    {
        for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
        {
            if (s->band_energy[band] == nullptr)
                continue;
            for (int f = 0; f < s->frames; ++f)
            {
                const double energy = s->band_energy[band][f];
                if (std::isfinite(energy) && energy > 0.0)
                    peakEnergy = std::max(peakEnergy, energy);
            }
        }
    }

    double ampReferencePeak = 0.0;
    double maxAmpDb = 0.0;
    double minAmpDb = 0.0;
    double ampDbRange = 1.0;
    double residualReferencePeak = 0.0;
    double peakDb = 0.0;
    double floorDb = 0.0;
    bool hasResidualEnergy = false;
    resolveSpectralTraceDisplayScaling(displayReference,
                                       maxAmp,
                                       peakEnergy,
                                       ampReferencePeak,
                                       maxAmpDb,
                                       minAmpDb,
                                       ampDbRange,
                                       residualReferencePeak,
                                       peakDb,
                                       floorDb,
                                       hasResidualEnergy);
    out.ampDisplayReferencePeak = ampReferencePeak;
    out.residualDisplayReferencePeak = residualReferencePeak;

    out.partials.reserve(candidates.size());
    for (const auto& candidate : candidates)
    {
        const int p = candidate.partialIndex;
        AtsPartialTrace trace;
        trace.partialIndex = p;
        trace.peakAmpNorm = (float) juce::jmax(0.0,
                                               (20.0 * std::log10(juce::jmax(1.0e-15, candidate.peakAmp))
                                                - minAmpDb)
                                                   / ampDbRange);
        trace.sourceMeanFrequencyHz = candidate.ampSum > 0.0 ? candidate.weightedHz / candidate.ampSum : 0.0;
        trace.frequencyShapeRank = rankForPartial(p);
        double weightedDisplayHz = 0.0;
        double displayAmpSum = 0.0;
        for (int f = 0; f < s->frames; ++f)
        {
            const double amp = s->amp[p][f];
            const double hz = s->frq[p][f];
            if (!std::isfinite(amp) || amp <= 0.0 || !std::isfinite(hz) || hz <= 0.0)
                continue;

            weightedDisplayHz += displayFrequencyFor(hz, p) * amp;
            displayAmpSum += amp;
        }
        trace.meanFrequencyHz = displayAmpSum > 0.0 ? weightedDisplayHz / displayAmpSum
                                                    : trace.sourceMeanFrequencyHz;
        trace.points.reserve((size_t) pointBudget + 2);

        auto appendFrame = [&](int f) {
            AtsPartialTracePoint point;
            point.frameIndex = f;
            point.timeNorm = s->frames > 1 ? (float) f / (float) (s->frames - 1) : 0.0f;

            const double amp = s->amp[p][f];
            const double hz = s->frq[p][f];
            point.amplitude = amp;
            point.sourceFrequencyHz = hz;
            point.active = std::isfinite(amp) && amp > 0.0 && std::isfinite(hz) && hz > 0.0;
            point.frequencyHz = point.active ? displayFrequencyFor(hz, p) : hz;
            if (point.active)
            {
                const double clampedHz = juce::jlimit(out.minFrequencyHz, out.maxFrequencyHz, point.frequencyHz);
                point.freqNorm = (float) juce::jlimit(0.0,
                                                      1.0,
                                                      std::log(clampedHz / out.minFrequencyHz) / logRange);
                const double ampDb = 20.0 * std::log10(juce::jmax(1.0e-15, amp));
                point.ampNorm = (float) juce::jmax(0.0, (ampDb - minAmpDb) / ampDbRange);
            }
            trace.points.push_back(point);
        };

        int lastFrame = -1;
        for (int f = 0; f < s->frames; f += pointStep)
        {
            appendFrame(f);
            lastFrame = f;
        }
        if (lastFrame != s->frames - 1)
            appendFrame(s->frames - 1);

        out.partials.push_back(std::move(trace));
    }
    out.visiblePartialCount = (int) out.partials.size();

    if (s->band_energy != nullptr && residualColumns > 0)
    {
        const int columns = juce::jlimit(16, 512, residualColumns);
        const int framesPerColumn = juce::jmax(1, (int) std::ceil((double) s->frames / (double) columns));
        out.residualBands.reserve(ATSA_CRITICAL_BANDS);
        for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
        {
            if (s->band_energy[band] == nullptr)
                continue;

            AtsResidualBandTrace residual;
            residual.bandIndex = band;
            residual.lowHz = ATSA_CRITICAL_BAND_EDGES[band];
            residual.highHz = ATSA_CRITICAL_BAND_EDGES[band + 1];
            residual.energyNorm.reserve((size_t) columns);
            for (int column = 0; column < columns; ++column)
            {
                const int firstFrame = column * framesPerColumn;
                const int lastFrame = juce::jmin(s->frames, firstFrame + framesPerColumn);
                double maxEnergy = 0.0;
                for (int f = firstFrame; f < lastFrame; ++f)
                {
                    const double energy = s->band_energy[band][f];
                    if (std::isfinite(energy) && energy > 0.0)
                        maxEnergy = std::max(maxEnergy, energy);
                }

                const double db = 10.0 * std::log10(juce::jmax(1.0e-20, maxEnergy));
                residual.energyNorm.push_back(hasResidualEnergy
                                                  ? (float) juce::jmax(0.0, (db - floorDb) / 72.0)
                                                  : 0.0f);
            }
            out.residualBands.push_back(std::move(residual));
        }
    }

    return true;
}

bool AtsAudioProcessor::buildSpectralTraceSnapshot(AtsSpectralTraceSnapshot& out,
                                                   int maxPartials,
                                                   int maxPointsPerPartial,
                                                   int residualColumns,
                                                   const AtsSpectralTraceDisplayReference& displayReference) const
{
    const auto shape = readFrequencyShapeFromApvts();
    const auto onsetDetectParams = readSmearnovDetectParamsFromApvts();
    const auto shapeRevision = displayShapeRevision.load(std::memory_order_acquire);

    // Acquire lock only long enough to copy the model; the heavy
    // O(partials×frames) snapshot computation runs outside the lock so
    // the message thread is not blocked for the full snapshot duration.
    ats::AtsSoundPtr snap;
    {
        const juce::ScopedLock ml(modelLock);
        if (analyzedModel.get() == nullptr)
            return false;
        snap.reset(copy_sound(const_cast<ATS_SOUND*>(analyzedModel.get())));
    }
    if (snap.get() == nullptr)
        return false;

    std::vector<ats::AtsOnset> onsetOverride;
    const bool hasOverride = copySmearnovOnsetOverride(AtsTraceSource::Current, onsetOverride);
    return buildSpectralTraceSnapshotForSound(snap.get(),
                                              out,
                                              maxPartials,
                                              maxPointsPerPartial,
                                              residualColumns,
                                              displayReference,
                                              shape,
                                              onsetDetectParams,
                                              shapeRevision,
                                              hasOverride ? &onsetOverride : nullptr);
}

bool AtsAudioProcessor::buildSpectralTraceSnapshot(AtsTraceSource source,
                                                   AtsSpectralTraceSnapshot& out,
                                                   int maxPartials,
                                                   int maxPointsPerPartial,
                                                   int residualColumns,
                                                   const AtsSpectralTraceDisplayReference& displayReference) const
{
    if (source == AtsTraceSource::Current)
        return buildSpectralTraceSnapshot(out, maxPartials, maxPointsPerPartial, residualColumns, displayReference);

    const auto shape = readFrequencyShapeFromApvts();
    const auto onsetDetectParams = readSmearnovDetectParamsFromApvts();
    const auto shapeRevision = displayShapeRevision.load(std::memory_order_acquire);
    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;

    ats::AtsSoundPtr snap;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        if (s == nullptr)
            return false;
        snap.reset(copy_sound(const_cast<ATS_SOUND*>(s)));
    }
    if (snap.get() == nullptr)
        return false;

    std::vector<ats::AtsOnset> onsetOverride;
    const bool hasOverride = copySmearnovOnsetOverride(source, onsetOverride);
    return buildSpectralTraceSnapshotForSound(snap.get(),
                                              out,
                                              maxPartials,
                                              maxPointsPerPartial,
                                              residualColumns,
                                              displayReference,
                                              shape,
                                              onsetDetectParams,
                                              shapeRevision,
                                              hasOverride ? &onsetOverride : nullptr);
}

bool AtsAudioProcessor::mutePartials(const std::vector<int>& partialIndices, juce::String& error)
{
    ScopedModelEdit edit(*this, AtsTraceSource::Current, "Mute Partials");
    error.clear();
    if (partialIndices.empty())
    {
        error = "No partials selected.";
        return false;
    }

    std::vector<int> uniqueIndices = partialIndices;
    std::sort(uniqueIndices.begin(), uniqueIndices.end());
    uniqueIndices.erase(std::unique(uniqueIndices.begin(), uniqueIndices.end()), uniqueIndices.end());

    bool mutedAny = false;
    {
        const juce::ScopedLock ml(modelLock);
        auto* s = analyzedModel.get();
        if (s == nullptr || s->amp == nullptr)
        {
            error = "No model.";
            return false;
        }

        for (const int partialIndex : uniqueIndices)
        {
            if (partialIndex < 0 || partialIndex >= s->partials || s->amp[partialIndex] == nullptr)
                continue;

            for (int f = 0; f < s->frames; ++f)
                s->amp[partialIndex][f] = 0.0;

            if (s->res != nullptr && s->res[partialIndex] != nullptr)
            {
                for (int f = 0; f < s->frames; ++f)
                    s->res[partialIndex][f] = 0.0;
            }
            mutedAny = true;
        }
    }

    if (!mutedAny)
    {
        error = "Selected partials are out of range.";
        return false;
    }

    const auto playbackKind = (AtsPreviewPlaybackKind) previewPlaybackKind.load(std::memory_order_acquire);
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex
        && playbackKind != AtsPreviewPlaybackKind::MorphTimeline)
    {
        stopPreviewPlayback();
        activePlaybackBufferIndex.store(-1, std::memory_order_release);
        while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
            std::this_thread::yield();
        playbackBuffers[(size_t) kMorphPlaybackBufferIndex].setSize(0, 0);
    }

    modelRevision.fetch_add(1, std::memory_order_relaxed);
    synthParamsDirty.store(true);
    triggerAsyncUpdate();
    return true;
}

bool AtsAudioProcessor::mutePartials(AtsTraceSource source,
                                     const std::vector<int>& partialIndices,
                                     juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Mute Partials");
    if (source == AtsTraceSource::Current)
        return mutePartials(partialIndices, error);

    error.clear();
    if (partialIndices.empty())
    {
        error = "No partials selected.";
        return false;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    bool mutedAny = false;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        if (s == nullptr || s->amp == nullptr)
        {
            error = source == AtsTraceSource::SourceA ? "Source A has no model." : "Source B has no model.";
            return false;
        }

        for (const int partialIndex : partialIndices)
        {
            if (partialIndex < 0 || partialIndex >= s->partials || s->amp[partialIndex] == nullptr)
                continue;

            for (int frame = 0; frame < s->frames; ++frame)
                s->amp[partialIndex][frame] = 0.0;
            if (s->res != nullptr && s->res[partialIndex] != nullptr)
            {
                for (int frame = 0; frame < s->frames; ++frame)
                    s->res[partialIndex][frame] = 0.0;
            }
            mutedAny = true;
        }

        if (mutedAny)
        {
            morphSourceAssignments[(size_t) slotIndex] = makeMorphAssignmentsFromSound(s, morphRecipe.frequencyGrid);
            rebuildMorphCorrespondenceIfReady();
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }

    if (!mutedAny)
    {
        error = "Selected partials are out of range.";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AtsAudioProcessor::scalePartials(AtsTraceSource source,
                                      const std::vector<int>& partialIndices,
                                      double amplitudeScale,
                                      juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Scale Partials");
    error.clear();
    if (partialIndices.empty())
    {
        error = "No partials selected.";
        return false;
    }
    if (!std::isfinite(amplitudeScale) || amplitudeScale < 0.0)
    {
        error = "Partial scale must be a finite non-negative value.";
        return false;
    }
    if (amplitudeScale == 0.0)
        return mutePartials(source, partialIndices, error);

    auto scaleSound = [&](ATS_SOUND* s) -> bool {
        if (s == nullptr || s->amp == nullptr)
            return false;
        bool scaledAny = false;
        for (const int partialIndex : partialIndices)
        {
            if (partialIndex < 0 || partialIndex >= s->partials || s->amp[partialIndex] == nullptr)
                continue;

            for (int frame = 0; frame < s->frames; ++frame)
                s->amp[partialIndex][frame] = juce::jmax(0.0, s->amp[partialIndex][frame] * amplitudeScale);
            scaledAny = true;
        }
        return scaledAny;
    };

    bool scaledAny = false;
    if (source == AtsTraceSource::Current)
    {
        {
            const juce::ScopedLock ml(modelLock);
            scaledAny = scaleSound(analyzedModel.get());
        }
        if (!scaledAny)
        {
            error = "No selected partials in the current model.";
            return false;
        }
        modelRevision.fetch_add(1, std::memory_order_relaxed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
        return true;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        scaledAny = scaleSound(s);
        if (scaledAny)
        {
            morphSourceAssignments[(size_t) slotIndex] = makeMorphAssignmentsFromSound(s, morphRecipe.frequencyGrid);
            rebuildMorphCorrespondenceIfReady();
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }
    if (!scaledAny)
    {
        error = source == AtsTraceSource::SourceA ? "No selected partials in Source A."
                                                  : "No selected partials in Source B.";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AtsAudioProcessor::smoothPartialEnvelope(AtsTraceSource source,
                                              int firstFrame,
                                              int lastFrame,
                                              double smoothness,
                                              juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Smooth Spectral Envelope");
    error.clear();
    if (!std::isfinite(smoothness))
    {
        error = "Smoothness must be a finite value.";
        return false;
    }
    smoothness = juce::jlimit(0.0, 1.0, smoothness);

    auto smoothSound = [&](ATS_SOUND* s) -> bool {
        if (s == nullptr || s->amp == nullptr || s->frq == nullptr || s->partials <= 0 || s->frames <= 0)
            return false;

        const int f0 = juce::jlimit(0, s->frames - 1, juce::jmin(firstFrame, lastFrame));
        const int f1 = juce::jlimit(0, s->frames - 1, juce::jmax(firstFrame, lastFrame));

        // Model-wide active frequency span for a temporally consistent Bark basis.
        double minHz = std::numeric_limits<double>::max();
        double maxHz = 1.0;
        for (int p = 0; p < s->partials; ++p)
        {
            if (s->frq[p] == nullptr || s->amp[p] == nullptr)
                continue;
            for (int f = 0; f < s->frames; ++f)
            {
                if (s->amp[p][f] <= ats::BarkSpectralEnvelope::kDefaultAmplitudeFloor)
                    continue;
                const double hz = s->frq[p][f];
                if (hz <= 0.0)
                    continue;
                minHz = std::min(minHz, hz);
                maxHz = std::max(maxHz, hz);
            }
        }
        if (minHz >= maxHz)
            return false;

        std::vector<double> freqs((size_t) s->partials);
        std::vector<double> amps((size_t) s->partials);
        bool changedAny = false;
        for (int f = f0; f <= f1; ++f)
        {
            for (int p = 0; p < s->partials; ++p)
            {
                freqs[(size_t) p] = (s->frq[p] != nullptr) ? s->frq[p][f] : 0.0;
                amps[(size_t) p] = (s->amp[p] != nullptr) ? s->amp[p][f] : 0.0;
            }
            if (!ats::reimposeRdcEnvelopeFrame(freqs, amps, smoothness, minHz, maxHz))
                continue;
            for (int p = 0; p < s->partials; ++p)
                if (s->amp[p] != nullptr)
                    s->amp[p][f] = juce::jmax(0.0, amps[(size_t) p]);
            changedAny = true;
        }
        return changedAny;
    };

    bool changedAny = false;
    if (source == AtsTraceSource::Current)
    {
        {
            const juce::ScopedLock ml(modelLock);
            changedAny = smoothSound(analyzedModel.get());
        }
        if (!changedAny)
        {
            error = "No partials to smooth in the selected range.";
            return false;
        }
        modelRevision.fetch_add(1, std::memory_order_relaxed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
        return true;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        changedAny = smoothSound(s);
        if (changedAny)
        {
            morphSourceAssignments[(size_t) slotIndex] = makeMorphAssignmentsFromSound(s, morphRecipe.frequencyGrid);
            rebuildMorphCorrespondenceIfReady();
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }
    if (!changedAny)
    {
        error = source == AtsTraceSource::SourceA ? "No partials to smooth in Source A."
                                                  : "No partials to smooth in Source B.";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AtsAudioProcessor::gainPartialEnvelope(AtsTraceSource source,
                                            int firstFrame,
                                            int lastFrame,
                                            double freqLoHz,
                                            double freqHiHz,
                                            double gainFactor,
                                            juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Gain Spectral Envelope");
    error.clear();
    if (!std::isfinite(gainFactor) || gainFactor < 0.0)
    {
        error = "Gain must be a finite non-negative value.";
        return false;
    }

    const bool freqAll = (freqLoHz <= 0.0 && freqHiHz <= 0.0);
    const double loHz = freqAll ? 0.0 : juce::jmin(freqLoHz, freqHiHz);
    const double hiHz = freqAll ? 0.0 : juce::jmax(freqLoHz, freqHiHz);

    auto gainSound = [&](ATS_SOUND* s) -> bool {
        if (s == nullptr || s->amp == nullptr || s->frq == nullptr || s->partials <= 0 || s->frames <= 0)
            return false;

        const int f0 = juce::jlimit(0, s->frames - 1, juce::jmin(firstFrame, lastFrame));
        const int f1 = juce::jlimit(0, s->frames - 1, juce::jmax(firstFrame, lastFrame));
        bool changedAny = false;
        for (int p = 0; p < s->partials; ++p)
        {
            if (s->amp[p] == nullptr || s->frq[p] == nullptr)
                continue;
            for (int f = f0; f <= f1; ++f)
            {
                if (!freqAll)
                {
                    const double hz = s->frq[p][f];
                    if (hz < loHz || hz > hiHz)
                        continue;
                }
                s->amp[p][f] = juce::jmax(0.0, s->amp[p][f] * gainFactor);
                changedAny = true;
            }
        }
        return changedAny;
    };

    bool changedAny = false;
    if (source == AtsTraceSource::Current)
    {
        {
            const juce::ScopedLock ml(modelLock);
            changedAny = gainSound(analyzedModel.get());
        }
        if (!changedAny)
        {
            error = "No partials in the selected box.";
            return false;
        }
        modelRevision.fetch_add(1, std::memory_order_relaxed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
        return true;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        changedAny = gainSound(s);
        if (changedAny)
        {
            morphSourceAssignments[(size_t) slotIndex] = makeMorphAssignmentsFromSound(s, morphRecipe.frequencyGrid);
            rebuildMorphCorrespondenceIfReady();
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }
    if (!changedAny)
    {
        error = source == AtsTraceSource::SourceA ? "No partials in the selected box (Source A)."
                                                  : "No partials in the selected box (Source B).";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AtsAudioProcessor::muteResidualBands(const std::vector<int>& bandIndices,
                                          int firstFrame,
                                          int lastFrame,
                                          juce::String& error)
{
    ScopedModelEdit edit(*this, AtsTraceSource::Current, "Mute Residual Bands");
    error.clear();
    if (bandIndices.empty())
    {
        error = "No residual bands selected.";
        return false;
    }

    std::vector<int> uniqueBands = bandIndices;
    std::sort(uniqueBands.begin(), uniqueBands.end());
    uniqueBands.erase(std::unique(uniqueBands.begin(), uniqueBands.end()), uniqueBands.end());

    bool mutedAny = false;
    {
        const juce::ScopedLock ml(modelLock);
        auto* s = analyzedModel.get();
        if (s == nullptr || s->band_energy == nullptr || s->frames <= 0)
        {
            error = "No residual-band data in the current model.";
            return false;
        }

        const int startFrame = juce::jlimit(0, s->frames - 1, std::min(firstFrame, lastFrame));
        const int endFrame = juce::jlimit(0, s->frames - 1, std::max(firstFrame, lastFrame));
        for (const int bandIndex : uniqueBands)
        {
            if (bandIndex < 0 || bandIndex >= ATSA_CRITICAL_BANDS || s->band_energy[bandIndex] == nullptr)
                continue;

            for (int frame = startFrame; frame <= endFrame; ++frame)
                s->band_energy[bandIndex][frame] = 0.0;

            if (s->res != nullptr && s->frq != nullptr)
            {
                const double lowHz = ATSA_CRITICAL_BAND_EDGES[bandIndex];
                const double highHz = ATSA_CRITICAL_BAND_EDGES[bandIndex + 1];
                for (int partial = 0; partial < s->partials; ++partial)
                {
                    if (s->res[partial] == nullptr || s->frq[partial] == nullptr)
                        continue;

                    for (int frame = startFrame; frame <= endFrame; ++frame)
                    {
                        const double hz = s->frq[partial][frame];
                        if (std::isfinite(hz) && hz >= lowHz && hz < highHz)
                            s->res[partial][frame] = 0.0;
                    }
                }
            }

            mutedAny = true;
        }
    }

    if (!mutedAny)
    {
        error = "Selected residual bands are out of range.";
        return false;
    }

    const auto playbackKind = (AtsPreviewPlaybackKind) previewPlaybackKind.load(std::memory_order_acquire);
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex
        && playbackKind != AtsPreviewPlaybackKind::MorphTimeline)
    {
        stopPreviewPlayback();
        activePlaybackBufferIndex.store(-1, std::memory_order_release);
        while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
            std::this_thread::yield();
        playbackBuffers[(size_t) kMorphPlaybackBufferIndex].setSize(0, 0);
    }

    modelRevision.fetch_add(1, std::memory_order_relaxed);
    synthParamsDirty.store(true);
    triggerAsyncUpdate();
    return true;
}

bool AtsAudioProcessor::muteResidualBands(AtsTraceSource source,
                                          const std::vector<int>& bandIndices,
                                          int firstFrame,
                                          int lastFrame,
                                          juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Mute Residual Bands");
    if (source == AtsTraceSource::Current)
        return muteResidualBands(bandIndices, firstFrame, lastFrame, error);

    return scaleResidualBands(source, bandIndices, firstFrame, lastFrame, 0.0, error);
}

bool AtsAudioProcessor::scaleResidualBands(AtsTraceSource source,
                                           const std::vector<int>& bandIndices,
                                           int firstFrame,
                                           int lastFrame,
                                           double energyScale,
                                           juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Scale Residual Bands");
    error.clear();
    if (bandIndices.empty())
    {
        error = "No residual bands selected.";
        return false;
    }
    if (!std::isfinite(energyScale) || energyScale < 0.0)
    {
        error = "Residual scale must be a finite non-negative value.";
        return false;
    }

    auto scaleSound = [&](ATS_SOUND* s) -> bool {
        if (s == nullptr || s->band_energy == nullptr || s->frames <= 0)
            return false;

        const int startFrame = juce::jlimit(0, s->frames - 1, std::min(firstFrame, lastFrame));
        const int endFrame = juce::jlimit(0, s->frames - 1, std::max(firstFrame, lastFrame));
        bool scaledAny = false;
        for (const int bandIndex : bandIndices)
        {
            if (bandIndex < 0 || bandIndex >= ATSA_CRITICAL_BANDS || s->band_energy[bandIndex] == nullptr)
                continue;

            for (int frame = startFrame; frame <= endFrame; ++frame)
                s->band_energy[bandIndex][frame] = juce::jmax(0.0, s->band_energy[bandIndex][frame] * energyScale);

            if (s->res != nullptr && s->frq != nullptr)
            {
                const double lowHz = ATSA_CRITICAL_BAND_EDGES[bandIndex];
                const double highHz = ATSA_CRITICAL_BAND_EDGES[bandIndex + 1];
                for (int partial = 0; partial < s->partials; ++partial)
                {
                    if (s->res[partial] == nullptr || s->frq[partial] == nullptr)
                        continue;

                    for (int frame = startFrame; frame <= endFrame; ++frame)
                    {
                        const double hz = s->frq[partial][frame];
                        if (std::isfinite(hz) && hz >= lowHz && hz < highHz)
                            s->res[partial][frame] = juce::jmax(0.0, s->res[partial][frame] * energyScale);
                    }
                }
            }
            scaledAny = true;
        }
        return scaledAny;
    };

    bool scaledAny = false;
    if (source == AtsTraceSource::Current)
    {
        {
            const juce::ScopedLock ml(modelLock);
            scaledAny = scaleSound(analyzedModel.get());
        }
        if (!scaledAny)
        {
            error = "No residual-band data in the current model.";
            return false;
        }

        const auto playbackKind = (AtsPreviewPlaybackKind) previewPlaybackKind.load(std::memory_order_acquire);
        if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex
            && playbackKind != AtsPreviewPlaybackKind::MorphTimeline)
        {
            stopPreviewPlayback();
            activePlaybackBufferIndex.store(-1, std::memory_order_release);
            while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
                std::this_thread::yield();
            playbackBuffers[(size_t) kMorphPlaybackBufferIndex].setSize(0, 0);
        }

        modelRevision.fetch_add(1, std::memory_order_relaxed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
        return true;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        auto* s = morphSourceSounds[(size_t) slotIndex].get();
        scaledAny = scaleSound(s);
        if (scaledAny)
        {
            morphSourceDiagnostics[(size_t) slotIndex] = makeResidualDiagnostics(s);
            morphSourceResidualBands[(size_t) slotIndex] = makeMorphResidualNoiseBands(s);
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }
    if (!scaledAny)
    {
        error = source == AtsTraceSource::SourceA ? "No residual-band data in Source A."
                                                  : "No residual-band data in Source B.";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    return true;
}

namespace
{
constexpr double kResidualDbFloor = -120.0;

double residualEnergyToDb(double energy)
{
    return 10.0 * std::log10(juce::jmax(1.0e-20, energy));
}

double residualDbToEnergy(double db)
{
    return std::pow(10.0, juce::jlimit(kResidualDbFloor, 48.0, db) / 10.0);
}

void scalePartialResidualSamplesInBand(ATS_SOUND* sound, int bandIndex, int frame, double scale)
{
    if (sound == nullptr || scale == 1.0 || sound->res == nullptr || sound->frq == nullptr)
        return;

    const double lowHz = ATSA_CRITICAL_BAND_EDGES[bandIndex];
    const double highHz = ATSA_CRITICAL_BAND_EDGES[bandIndex + 1];
    for (int partial = 0; partial < sound->partials; ++partial)
    {
        if (sound->res[partial] == nullptr || sound->frq[partial] == nullptr)
            continue;

        const double hz = sound->frq[partial][frame];
        if (std::isfinite(hz) && hz >= lowHz && hz < highHz)
            sound->res[partial][frame] = juce::jmax(0.0, sound->res[partial][frame] * scale);
    }
}

void writeBandEnergyAtFrame(ATS_SOUND* sound, int bandIndex, int frame, double newEnergy)
{
    if (sound == nullptr || sound->band_energy == nullptr || sound->band_energy[bandIndex] == nullptr)
        return;

    newEnergy = juce::jmax(0.0, newEnergy);
    const double oldEnergy = sound->band_energy[bandIndex][frame];
    if (oldEnergy > 0.0)
        scalePartialResidualSamplesInBand(sound, bandIndex, frame, newEnergy / oldEnergy);
    else if (newEnergy <= 0.0)
        scalePartialResidualSamplesInBand(sound, bandIndex, frame, 0.0);

    sound->band_energy[bandIndex][frame] = newEnergy;
}

std::vector<int> uniqueSortedBandIndices(const std::vector<int>& bandIndices)
{
    std::vector<int> unique = bandIndices;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    return unique;
}

bool mutateSoundResidualFrames(ATS_SOUND* sound,
                               const std::vector<int>& bandIndices,
                               int firstFrame,
                               int lastFrame,
                               const AtsAudioProcessor::ResidualBandFrameMutator& mutator)
{
    if (sound == nullptr || sound->band_energy == nullptr || sound->frames <= 0 || !mutator)
        return false;

    const int startFrame = juce::jlimit(0, sound->frames - 1, std::min(firstFrame, lastFrame));
    const int endFrame = juce::jlimit(0, sound->frames - 1, std::max(firstFrame, lastFrame));
    bool changedAny = false;

    for (const int bandIndex : bandIndices)
    {
        if (bandIndex < 0 || bandIndex >= ATSA_CRITICAL_BANDS || sound->band_energy[bandIndex] == nullptr)
            continue;

        for (int frame = startFrame; frame <= endFrame; ++frame)
        {
            double energy = sound->band_energy[bandIndex][frame];
            mutator(bandIndex, frame, energy);
            if (!std::isfinite(energy) || energy < 0.0)
                energy = 0.0;
            writeBandEnergyAtFrame(sound, bandIndex, frame, energy);
            changedAny = true;
        }
    }

    return changedAny;
}
} // namespace

bool AtsAudioProcessor::mutateResidualBandFrames(AtsTraceSource source,
                                                 const std::vector<int>& bandIndices,
                                                 int firstFrame,
                                                 int lastFrame,
                                                 const ResidualBandFrameMutator& mutator,
                                                 juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Edit Residual Bands");
    error.clear();
    const auto bands = uniqueSortedBandIndices(bandIndices);
    if (bands.empty())
    {
        error = "No residual bands selected.";
        return false;
    }
    if (mutator == nullptr)
    {
        error = "Invalid residual edit.";
        return false;
    }

    if (source == AtsTraceSource::Current)
    {
        bool changedAny = false;
        {
            const juce::ScopedLock ml(modelLock);
            changedAny = mutateSoundResidualFrames(analyzedModel.get(), bands, firstFrame, lastFrame, mutator);
        }
        if (!changedAny)
        {
            error = "No residual-band data in the current model.";
            return false;
        }

        const auto playbackKind = (AtsPreviewPlaybackKind) previewPlaybackKind.load(std::memory_order_acquire);
        if (activePlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex
            && playbackKind != AtsPreviewPlaybackKind::MorphTimeline)
        {
            stopPreviewPlayback();
            activePlaybackBufferIndex.store(-1, std::memory_order_release);
            while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == kMorphPlaybackBufferIndex)
                std::this_thread::yield();
            playbackBuffers[(size_t) kMorphPlaybackBufferIndex].setSize(0, 0);
        }

        modelRevision.fetch_add(1, std::memory_order_relaxed);
        synthParamsDirty.store(true);
        triggerAsyncUpdate();
        return true;
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    bool changedAny = false;
    ATS_SOUND* editedSound = nullptr;
    {
        const juce::ScopedLock ml(morphRecipeLock);
        editedSound = morphSourceSounds[(size_t) slotIndex].get();
        changedAny = mutateSoundResidualFrames(editedSound, bands, firstFrame, lastFrame, mutator);
        if (changedAny)
        {
            morphSourceDiagnostics[(size_t) slotIndex] = makeResidualDiagnostics(editedSound);
            morphSourceResidualBands[(size_t) slotIndex] = makeMorphResidualNoiseBands(editedSound);
            serializedMorphRecipe = ats::serializeMorphRecipe(morphRecipe);
        }
    }
    if (!changedAny)
    {
        error = source == AtsTraceSource::SourceA ? "No residual-band data in Source A."
                                                  : "No residual-band data in Source B.";
        return false;
    }

    const int bufferIndex = slotIndex == 0 ? kSourceAPlaybackBufferIndex : kSourceBPlaybackBufferIndex;
    if (activePlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        stopPreviewPlayback();
    while (readingPlaybackBufferIndex.load(std::memory_order_acquire) == bufferIndex)
        std::this_thread::yield();
    playbackBuffers[(size_t) bufferIndex].setSize(0, 0);

    markMorphSnapshotStale();
    modelRevision.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool AtsAudioProcessor::readResidualBandEnergyDb(AtsTraceSource source,
                                                 int bandIndex,
                                                 int frameIndex,
                                                 double& dbOut) const
{
    dbOut = kResidualDbFloor;
    if (bandIndex < 0 || bandIndex >= ATSA_CRITICAL_BANDS || frameIndex < 0)
        return false;

    auto readFromSound = [&](const ATS_SOUND* sound) -> bool {
        if (sound == nullptr || sound->band_energy == nullptr || sound->band_energy[bandIndex] == nullptr)
            return false;
        if (frameIndex >= sound->frames)
            return false;
        dbOut = residualEnergyToDb(sound->band_energy[bandIndex][frameIndex]);
        return true;
    };

    if (source == AtsTraceSource::Current)
    {
        const juce::ScopedLock ml(modelLock);
        return readFromSound(analyzedModel.get());
    }

    const int slotIndex = source == AtsTraceSource::SourceA ? 0 : 1;
    const juce::ScopedLock ml(morphRecipeLock);
    return readFromSound(morphSourceSounds[(size_t) slotIndex].get());
}

bool AtsAudioProcessor::writeResidualBandEnergyDb(AtsTraceSource source,
                                                  int bandIndex,
                                                  int frameIndex,
                                                  double db,
                                                  juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Edit Residual Band");
    if (!std::isfinite(db))
    {
        error = "dB value must be finite.";
        return false;
    }

    return mutateResidualBandFrames(source,
                                    { bandIndex },
                                    frameIndex,
                                    frameIndex,
                                    [db](int, int, double& energy) {
                                        energy = residualDbToEnergy(db);
                                    },
                                    error);
}

bool AtsAudioProcessor::paintResidualBandEnvelope(AtsTraceSource source,
                                                  int bandIndex,
                                                  int firstFrame,
                                                  int lastFrame,
                                                  const std::vector<std::pair<int, double>>& frameDbPairs,
                                                  juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Paint Residual Envelope");
    if (bandIndex < 0 || frameDbPairs.empty())
    {
        error = "Nothing to paint.";
        return false;
    }

    std::map<int, double> dbByFrame;
    for (const auto& pair : frameDbPairs)
    {
        if (std::isfinite(pair.second))
            dbByFrame[pair.first] = pair.second;
    }
    if (dbByFrame.empty())
    {
        error = "Nothing to paint.";
        return false;
    }

    return mutateResidualBandFrames(source,
                                    { bandIndex },
                                    firstFrame,
                                    lastFrame,
                                    [&dbByFrame](int, int frame, double& energy) {
                                        const auto it = dbByFrame.find(frame);
                                        if (it != dbByFrame.end())
                                            energy = residualDbToEnergy(it->second);
                                    },
                                    error);
}

bool AtsAudioProcessor::smoothResidualBands(AtsTraceSource source,
                                            const std::vector<int>& bandIndices,
                                            int firstFrame,
                                            int lastFrame,
                                            int windowRadiusFrames,
                                            juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Smooth Residual Bands");
    const int radius = juce::jmax(1, windowRadiusFrames);
    std::map<std::pair<int, int>, double> smoothed;

    auto readEnergy = [&](AtsTraceSource src, int band, int frame, double& energyOut) -> bool {
        double db = kResidualDbFloor;
        if (!readResidualBandEnergyDb(src, band, frame, db))
            return false;
        energyOut = residualDbToEnergy(db);
        return true;
    };

    const auto bands = uniqueSortedBandIndices(bandIndices);
    const int startFrame = std::min(firstFrame, lastFrame);
    const int endFrame = std::max(firstFrame, lastFrame);

    for (const int band : bands)
    {
        int maxFrame = 0;
        if (source == AtsTraceSource::Current)
        {
            int partials = 0, frames = 0;
            bool hr = false;
            if (getModelFootprint(partials, frames, hr))
                maxFrame = juce::jmax(0, frames - 1);
        }
        else
        {
            AtsMorphSourceSlotInfo info;
            const int slot = source == AtsTraceSource::SourceA ? 0 : 1;
            if (getMorphSourceSlotInfo(slot, info) && info.hasModel)
                maxFrame = juce::jmax(0, info.frameCount - 1);
        }

        for (int frame = startFrame; frame <= endFrame; ++frame)
        {
            double sum = 0.0;
            int count = 0;
            for (int offset = -radius; offset <= radius; ++offset)
            {
                const int sampleFrame = frame + offset;
                if (sampleFrame < 0 || sampleFrame > maxFrame)
                    continue;

                double energy = 0.0;
                if (readEnergy(source, band, sampleFrame, energy))
                {
                    sum += energy;
                    ++count;
                }
            }
            if (count > 0)
                smoothed[{ band, frame }] = sum / (double) count;
        }
    }

    if (smoothed.empty())
    {
        error = "No residual-band data to smooth.";
        return false;
    }

    return mutateResidualBandFrames(source,
                                    bands,
                                    firstFrame,
                                    lastFrame,
                                    [&smoothed](int band, int frame, double& energy) {
                                        const auto it = smoothed.find({ band, frame });
                                        if (it != smoothed.end())
                                            energy = it->second;
                                    },
                                    error);
}

bool AtsAudioProcessor::tiltResidualBands(AtsTraceSource source,
                                          const std::vector<int>& bandIndices,
                                          int firstFrame,
                                          int lastFrame,
                                          double tiltDbPerBandStep,
                                          juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Tilt Residual Bands");
    if (!std::isfinite(tiltDbPerBandStep) || std::abs(tiltDbPerBandStep) < 1.0e-9)
    {
        error = "Tilt amount must be non-zero.";
        return false;
    }

    auto bands = uniqueSortedBandIndices(bandIndices);
    if (bands.empty())
    {
        error = "No residual bands selected.";
        return false;
    }

    const int centerIndex = (int) bands.size() / 2;
    std::map<int, double> dbOffsetByBand;
    for (size_t i = 0; i < bands.size(); ++i)
        dbOffsetByBand[bands[i]] = tiltDbPerBandStep * (double) ((int) i - centerIndex);

    return mutateResidualBandFrames(source,
                                    bands,
                                    firstFrame,
                                    lastFrame,
                                    [&dbOffsetByBand](int band, int, double& energy) {
                                        const auto it = dbOffsetByBand.find(band);
                                        if (it == dbOffsetByBand.end())
                                            return;
                                        const double db = residualEnergyToDb(energy) + it->second;
                                        energy = residualDbToEnergy(db);
                                    },
                                    error);
}

bool AtsAudioProcessor::copyResidualBandToClipboard(AtsTraceSource source,
                                                    int bandIndex,
                                                    int firstFrame,
                                                    int lastFrame,
                                                    juce::String& error)
{
    error.clear();
    if (bandIndex < 0 || bandIndex >= ATSA_CRITICAL_BANDS)
    {
        error = "Invalid band index.";
        return false;
    }

    const int startFrame = std::min(firstFrame, lastFrame);
    const int endFrame = std::max(firstFrame, lastFrame);
    AtsResidualBandClipboard clip;
    clip.sourceBandIndex = bandIndex;
    clip.frameCount = endFrame - startFrame + 1;
    clip.energies.reserve((size_t) clip.frameCount);

    for (int frame = startFrame; frame <= endFrame; ++frame)
    {
        double db = kResidualDbFloor;
        if (!readResidualBandEnergyDb(source, bandIndex, frame, db))
        {
            error = "No residual-band data to copy.";
            residualBandClipboard = {};
            return false;
        }
        clip.energies.push_back(residualDbToEnergy(db));
    }

    residualBandClipboard = std::move(clip);
    return true;
}

bool AtsAudioProcessor::pasteResidualBandFromClipboard(AtsTraceSource source,
                                                     int bandIndex,
                                                     int firstFrame,
                                                     int lastFrame,
                                                     juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Paste Residual Band");
    if (!residualBandClipboard.valid())
    {
        error = "Clipboard is empty.";
        return false;
    }
    if (bandIndex < 0 || bandIndex >= ATSA_CRITICAL_BANDS)
    {
        error = "Invalid band index.";
        return false;
    }

    const int startFrame = std::min(firstFrame, lastFrame);
    const int endFrame = std::max(firstFrame, lastFrame);
    const int targetLength = endFrame - startFrame + 1;
    std::map<int, double> energyByFrame;
    for (int i = 0; i < targetLength; ++i)
    {
        const int frame = startFrame + i;
        const size_t clipIndex = (size_t) (i % residualBandClipboard.frameCount);
        energyByFrame[frame] = residualBandClipboard.energies[clipIndex];
    }

    return mutateResidualBandFrames(source,
                                    { bandIndex },
                                    startFrame,
                                    endFrame,
                                    [&energyByFrame](int, int frame, double& energy) {
                                        const auto it = energyByFrame.find(frame);
                                        if (it != energyByFrame.end())
                                            energy = it->second;
                                    },
                                    error);
}

bool AtsAudioProcessor::swapResidualBands(AtsTraceSource source,
                                          int bandIndexA,
                                          int bandIndexB,
                                          int firstFrame,
                                          int lastFrame,
                                          juce::String& error)
{
    ScopedModelEdit edit(*this, source, "Swap Residual Bands");
    if (bandIndexA < 0 || bandIndexB < 0 || bandIndexA >= ATSA_CRITICAL_BANDS || bandIndexB >= ATSA_CRITICAL_BANDS)
    {
        error = "Invalid band index.";
        return false;
    }
    if (bandIndexA == bandIndexB)
    {
        error = "Choose two different bands.";
        return false;
    }

    const int startFrame = std::min(firstFrame, lastFrame);
    const int endFrame = std::max(firstFrame, lastFrame);
    std::map<std::pair<int, int>, double> energyByBandFrame;

    for (const int band : { bandIndexA, bandIndexB })
    {
        for (int frame = startFrame; frame <= endFrame; ++frame)
        {
            double db = kResidualDbFloor;
            if (!readResidualBandEnergyDb(source, band, frame, db))
            {
                error = "No residual-band data to swap.";
                return false;
            }
            energyByBandFrame[{ band, frame }] = residualDbToEnergy(db);
        }
    }

    return mutateResidualBandFrames(source,
                                    { bandIndexA, bandIndexB },
                                    startFrame,
                                    endFrame,
                                    [&energyByBandFrame, bandIndexA, bandIndexB](int band, int frame, double& energy) {
                                        const int sourceBand = band == bandIndexA ? bandIndexB : bandIndexA;
                                        const auto it = energyByBandFrame.find({ sourceBand, frame });
                                        if (it != energyByBandFrame.end())
                                            energy = it->second;
                                    },
                                    error);
}

bool AtsAudioProcessor::hasResidualBandClipboard() const
{
    return residualBandClipboard.valid();
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

bool AtsAudioProcessor::exportMorphSourceSlotAts(int slotIndex, const juce::File& file, juce::String& error)
{
    error.clear();
    if (slotIndex < 0 || slotIndex > 1)
    {
        error = "Invalid morph source slot.";
        return false;
    }

    juce::File out = file;
    if (out.getFileExtension().isEmpty())
        out = out.withFileExtension("ats");

    const auto parent = out.getParentDirectory();
    if (!parent.exists())
    {
        if (!parent.createDirectory())
        {
            error = "Could not create export directory.";
            return false;
        }
    }

    const ats::AnalysisParams ap = readAnalysisParamsFromApvts();
    const juce::ScopedLock ml(morphRecipeLock);
    ATS_SOUND* sound = morphSourceSounds[(size_t) slotIndex].get();
    if (sound == nullptr)
    {
        error = slotIndex == 0 ? "Source A has no analyzed model to export."
                               : "Source B has no analyzed model to export.";
        return false;
    }

    if (ats_save(sound, out.getFullPathName().toUTF8(), ap.SMR_thres, ap.type) < 0)
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
        currentModelName = file.getFileNameWithoutExtension();
        activePlaybackBufferIndex.store(-1, std::memory_order_release);
        previewPlaying.store(false, std::memory_order_release);
    }

    modelRevision.fetch_add(1, std::memory_order_relaxed);
    clearSmearnovOnsetOverrides(AtsTraceSource::Current);

    setChoiceParameter(apvts, ats_param::kAnalysisType, juce::jlimit(1, 4, loadedType) - 1);

    {
        const juce::SpinLock::ScopedLockType sl(errorLock);
        lastError.clear();
    }

    setUiState(AtsUiState::Analyzed);
    synthParamsDirty.store(true);
    triggerAsyncUpdate();
    return true;
}

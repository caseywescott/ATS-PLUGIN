#include "PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "AtsMathExpr.h"
#include "PluginEditor.h"

#include "ResampleLinear.h"

#include <JuceHeader.h>

extern "C" {
#include "atsa.h"
}

namespace
{
constexpr int kStateVersion = 4;
constexpr int kMinReadableStateVersion = 2;

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

} // namespace

AtsAudioProcessor::AtsAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    apvts.addParameterListener("partGain", this);
    apvts.addParameterListener("resGain", this);
    apvts.addParameterListener("freq", this);
    apvts.addParameterListener("timeStretch", this);
    apvts.addParameterListener("synStart", this);
    apvts.addParameterListener("synDur", this);
    apvts.addParameterListener("usePhase", this);
    apvts.addParameterListener("useAllPartials", this);
    apvts.addParameterListener("residualOnly", this);
    apvts.addParameterListener("outputTrim", this);
}

AtsAudioProcessor::~AtsAudioProcessor()
{
    cancelJobs();
    joinWorkers();
    apvts.removeParameterListener("partGain", this);
    apvts.removeParameterListener("resGain", this);
    apvts.removeParameterListener("freq", this);
    apvts.removeParameterListener("timeStretch", this);
    apvts.removeParameterListener("synStart", this);
    apvts.removeParameterListener("synDur", this);
    apvts.removeParameterListener("usePhase", this);
    apvts.removeParameterListener("useAllPartials", this);
    apvts.removeParameterListener("residualOnly", this);
    apvts.removeParameterListener("outputTrim", this);
}

void AtsAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    outputTrimSmoothed.reset(sampleRate, 0.02);
    outputTrimSmoothed.setCurrentAndTargetValue(
        apvts.getRawParameterValue("outputTrim")->load());

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
        "analysisType",
        "Analysis type",
        juce::StringArray { "1 amp+freq", "2 +phase", "3 +noise", "4 full" },
        3));

    return layout;
}

void AtsAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);
    if (parameterID == "outputTrim")
        return;

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

ats::SynthParams AtsAudioProcessor::readSynthParamsFromApvts() const
{
    ats::SynthParams sp;
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
    sp.frequencyFactor = (double) apvts.getRawParameterValue("freq")->load();
    sp.frequencyFactor *= std::pow(2.0, (double) midiSemitoneOffset.load() / 12.0);
    sp.timeFactor = apvts.getRawParameterValue("timeStretch")->load();
    sp.startTime = apvts.getRawParameterValue("synStart")->load();
    sp.duration = apvts.getRawParameterValue("synDur")->load();
    sp.usePartialsPhase = apvts.getRawParameterValue("usePhase")->load() > 0.5f;
    sp.useAllPartials = apvts.getRawParameterValue("useAllPartials")->load() > 0.5f;
    sp.sampleRate = hostSampleRate;
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
    }

    {
        const juce::ScopedLock ml(modelLock);
        analyzedModel.reset(nullptr);
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

        auto* samples = const_cast<double*>(localMono.data());
        double** residual = nullptr;
        int resLen = 0;

        analysisProgress.store(0.15f);
        ATS_SOUND* raw = tracker(&an, samples, fileSr, (int) localMono.size(), &residual, &resLen);
        freeResidual(&residual);

        if (jobGeneration.load() != gen)
        {
            if (raw != nullptr)
            {
                free_sound(raw);
                std::free(raw);
            }
            analysisThreadRunning.store(false);
            return;
        }

        analysisProgress.store(0.85f);

        {
            const juce::ScopedLock ml(modelLock);
            analyzedModel.reset(raw);
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
        const ats::SynthParams sp = readSynthParamsFromApvts();

        juce::AudioBuffer<float> mono;
        int modelSr = 44100;
        {
            const juce::ScopedLock ml(modelLock);
            if (jobGeneration.load() != gen || analyzedModel.get() == nullptr)
            {
                resynthThreadRunning.store(false);
                setUiState(AtsUiState::Analyzed);
                return;
            }
            modelSr = analyzedModel.get()->srate;
            mono = runDoSynthesis(analyzedModel.get(), sp);
        }

        if (jobGeneration.load() != gen || mySerial != resynthJobSerial.load(std::memory_order_acquire))
        {
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        if (mono.getNumSamples() <= 0)
        {
            setError("Synthesis produced silence.");
            resynthThreadRunning.store(false);
            setUiState(AtsUiState::Analyzed);
            return;
        }

        std::vector<float> resampled;
        ats::resampleLinearMono(
            mono.getReadPointer(0),
            mono.getNumSamples(),
            (double) modelSr,
            resampled,
            hostSampleRate);

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
        stereo.setSize(2, (int) resampled.size());
        for (int i = 0; i < stereo.getNumSamples(); ++i)
        {
            const float x = resampled[static_cast<size_t>(i)];
            stereo.setSample(0, i, x);
            stereo.setSample(1, i, x);
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
        float v = 0.0f;
        if (shouldPreview && playback != nullptr && playbackPosition < playback->getNumSamples())
        {
            v = playback->getSample(0, playbackPosition) * trim;
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

#include "PluginProcessor.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "PluginEditor.h"

#include "ResampleLinear.h"

#include <JuceHeader.h>

extern "C" {
#include "atsa.h"
}

namespace
{
constexpr int kStateVersion = 2;

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
    juce::ignoreUnused(parameterID, newValue);
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
    sp.partialsGain = apvts.getRawParameterValue("partGain")->load();
    sp.residualGain = apvts.getRawParameterValue("resGain")->load();
    sp.frequencyFactor = apvts.getRawParameterValue("freq")->load();
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
    const int idx = juce::jlimit(0, 3, (int) std::floor((double) v * 4.0 - 1e-9));
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
        const juce::SpinLock::ScopedLockType sl(errorLock);
        lastError.clear();
    }
    setUiState(AtsUiState::ReadyToAnalyze);
}

void AtsAudioProcessor::joinWorkers()
{
    if (analysisThread.joinable())
        analysisThread.join();
    if (resynthThread.joinable())
        resynthThread.join();
    analysisThreadRunning.store(false);
    resynthThreadRunning.store(false);
}

void AtsAudioProcessor::cancelJobs()
{
    ++jobGeneration;
    joinWorkers();
}

void AtsAudioProcessor::startAnalysis()
{
    cancelJobs();

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
    if (resynthThread.joinable())
        resynthThread.join();

    {
        const juce::ScopedLock ml(modelLock);
        if (analyzedModel.get() == nullptr)
            return;
    }

    const uint64_t gen = jobGeneration.load();
    setUiState(AtsUiState::Resynthesizing);
    resynthThreadRunning.store(true);

    resynthThread = std::thread([this, gen]() {
        const ats::SynthParams sp = readSynthParamsFromApvts();

        juce::AudioBuffer<float> mono;
        int modelSr = 44100;
        {
            const juce::ScopedLock ml(modelLock);
            if (jobGeneration.load() != gen || analyzedModel.get() == nullptr)
            {
                resynthThreadRunning.store(false);
                return;
            }
            modelSr = analyzedModel.get()->srate;
            mono = runDoSynthesis(analyzedModel.get(), sp);
        }

        if (jobGeneration.load() != gen)
        {
            resynthThreadRunning.store(false);
            return;
        }

        if (mono.getNumSamples() <= 0)
        {
            setError("Synthesis produced silence.");
            resynthThreadRunning.store(false);
            return;
        }

        std::vector<float> resampled;
        ats::resampleLinearMono(
            mono.getReadPointer(0),
            mono.getNumSamples(),
            (double) modelSr,
            resampled,
            hostSampleRate);

        juce::AudioBuffer<float> stereo((int) resampled.size(), 2);
        for (int i = 0; i < stereo.getNumSamples(); ++i)
        {
            const float x = resampled[static_cast<size_t>(i)];
            stereo.setSample(0, i, x);
            stereo.setSample(1, i, x);
        }

        {
            const juce::ScopedLock pl(playbackLock);
            playbackBuffer.setSize(2, stereo.getNumSamples());
            playbackBuffer.makeCopyOf(stereo);
            playbackPosition = 0;
        }

        resynthThreadRunning.store(false);
        setUiState(AtsUiState::Analyzed);
    });
}

void AtsAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    outputTrimSmoothed.setTargetValue(apvts.getRawParameterValue("outputTrim")->load());

    const int n = buffer.getNumSamples();
    const juce::ScopedLock pl(playbackLock);

    for (int s = 0; s < n; ++s)
    {
        const float trim = outputTrimSmoothed.getNextValue();
        float v = 0.0f;
        if (playbackPosition < playbackBuffer.getNumSamples())
        {
            v = playbackBuffer.getSample(0, playbackPosition) * trim;
            ++playbackPosition;
        }

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            buffer.setSample(c, s, v);
    }
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
    writeSynth(mos, sp);

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
    if (ver != kStateVersion)
        return;

    ats::AnalysisParams ap;
    ats::SynthParams sp;
    if (!readAnalysis(mis, ap) || !readSynth(mis, sp))
        return;

    const juce::int64 atsSize = mis.readInt64();
    juce::MemoryBlock atsData;
    if (atsSize > 0 && mis.getNumBytesRemaining() >= atsSize)
    {
        atsData.setSize((size_t) atsSize);
        mis.read(atsData.getData(), (size_t) atsSize);
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
            setUiState(AtsUiState::Analyzed);
            synthParamsDirty.store(true);
            triggerAsyncUpdate();
        }
        else
        {
            std::free(raw);
        }
        tmp.deleteFile();
    }
}

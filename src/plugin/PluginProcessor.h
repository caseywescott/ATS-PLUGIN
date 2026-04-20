#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "AtsModel.h"
#include "AtsParams.h"

enum class AtsUiState
{
    NoSource,
    ReadyToAnalyze,
    Analyzing,
    Analyzed,
    Resynthesizing,
    Error
};

class AtsAudioProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater
{
public:
    AtsAudioProcessor();
    ~AtsAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ATS Resynth"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    void loadSourceFile(const juce::File& file);
    void startAnalysis();
    void cancelJobs();

    AtsUiState getUiState() const { return uiState.load(); }
    juce::String getLastError() const
    {
        const juce::SpinLock::ScopedLockType sl(errorLock);
        return lastError;
    }

    float getAnalysisProgress() const { return analysisProgress.load(); }

    static constexpr uint32_t stateMagic = 0x41545350u; // 'ATSP'

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void setUiState(AtsUiState s);
    void setError(const juce::String& e);

    void startResynthWorker();
    void joinWorkers();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ats::SynthParams readSynthParamsFromApvts() const;
    ats::AnalysisParams readAnalysisParamsFromApvts() const;

    std::atomic<AtsUiState> uiState { AtsUiState::NoSource };
    std::atomic<float> analysisProgress { 0.0f };
    std::atomic<bool> synthParamsDirty { false };

    juce::SpinLock errorLock;
    juce::String lastError;

    juce::CriticalSection sourceLock;
    std::vector<double> sourceMono;
    int sourceSampleRate = 0;

    juce::CriticalSection modelLock;
    ats::AtsSoundPtr analyzedModel;

    juce::CriticalSection playbackLock;
    juce::AudioBuffer<float> playbackBuffer;
    int playbackPosition = 0;

    juce::LinearSmoothedValue<float> outputTrimSmoothed;

    std::atomic<uint64_t> jobGeneration { 0 };
    std::thread analysisThread;
    std::thread resynthThread;
    std::atomic<bool> analysisThreadRunning { false };
    std::atomic<bool> resynthThreadRunning { false };

    double hostSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessor)
};

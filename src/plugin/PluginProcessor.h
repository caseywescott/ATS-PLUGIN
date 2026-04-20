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
    ApplyingMath,
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

    bool acceptsMidi() const override { return true; }
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

    int getModelRevision() const { return modelRevision.load(); }

    /** Fills `out` (ARGB) with a downsampled log-amplitude map; returns false if no model. */
    bool buildSpectrogramTexture(bool partialsMode, int width, int height, juce::Image& out);

    bool isWorkerBusy() const;

    /** Returns false if no model. */
    bool getModelFootprint(int& partials, int& frames, bool& hasResidualBands) const;

    bool saveFullStatePreset(const juce::File& file);
    bool loadFullStatePreset(const juce::File& file);

    /** Native ATS file (Qatsh/ATSA ecosystem). Uses current analysis SMR/type for save. */
    bool exportAtsFile(const juce::File& file, juce::String& error);
    bool importAtsFile(const juce::File& file, juce::String& error);

    /** Multi-line script; see docs/MATHEXPR.md. Runs on a worker thread. */
    void startMathExpr(const juce::String& script);

    /** Read one sinusoidal cell; returns false if out of range or no model. */
    bool readPartialCell(int partialIdx, int frameIdx, double& amp, double& freq, double& phase, bool& hasPhase);

    /** Write amplitude and frequency for one cell; validates invariants. */
    bool writePartialCell(int partialIdx, int frameIdx, double amp, double freq, juce::String& error);

    juce::String getMathStatus() const;

    static constexpr uint32_t stateMagic = 0x41545350u; // 'ATSP'

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void setUiState(AtsUiState s);
    void setError(const juce::String& e);
    void setMathStatus(const juce::String& s);

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
    std::thread transformThread;
    std::atomic<bool> analysisThreadRunning { false };
    std::atomic<bool> resynthThreadRunning { false };
    std::atomic<bool> transformThreadRunning { false };

    mutable juce::SpinLock mathStatusLock;
    juce::String mathStatus;

    double hostSampleRate = 44100.0;

    std::atomic<int> modelRevision { 0 };

    /** Semitones relative to middle C (note 60) applied on top of the Freq APVTS factor. */
    std::atomic<float> midiSemitoneOffset { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessor)
};

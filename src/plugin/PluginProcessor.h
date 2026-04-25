#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "AtsModel.h"
#include "AtsParams.h"
#include "EnvelopeOverlay.h"
#include "SpectralEnvelopeHybrid.h"

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

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

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
    bool buildEnvelopeOverlayData(int gridSize, ats::EnvelopeOverlayData& out, juce::String& status);

    bool isWorkerBusy() const;

    /** Returns false if no model. */
    bool getModelFootprint(int& partials, int& frames, bool& hasResidualBands) const;

    bool saveFullStatePreset(const juce::File& file);
    bool loadFullStatePreset(const juce::File& file);

    /** Build a transfer recipe from the current APVTS envelope transfer params. */
    ats::SpectralEnvelopeTransferRecipe readEnvelopeTransferRecipeFromApvts() const;

    /** Source and target model IDs for the envelope transfer workspace (UI-set, non-automatable). */
    void setEnvelopeTransferSourceId(const juce::String& id);
    void setEnvelopeTransferTargetId(const juce::String& id);
    juce::String getEnvelopeTransferSourceId() const;
    juce::String getEnvelopeTransferTargetId() const;

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
    void startPreviewPlayback();
    void stopPreviewPlayback();
    bool isPreviewPlaying() const { return previewPlaying.load(); }
    bool hasPreviewAudio() const;

    static constexpr uint32_t stateMagic = 0x41545350u; // 'ATSP'

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void setUiState(AtsUiState s);
    void setError(const juce::String& e);
    void setMathStatus(const juce::String& s);

    void startResynthWorker();
    void joinWorkers();
    void clearPreviewLayerCache();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ats::SynthParams readSynthParamsFromApvts() const;
    /** Freq/time/phase flags only; caller sets partialsGain and residualGain for layer renders. */
    ats::SynthParams readCoreSynthParamsFromApvts() const;
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

    /** Host-rate partial / residual layers; skip re-synthesis when core + model revision unchanged. */
    bool previewLayerCacheValid = false;
    int previewLayerCacheRevision = -1;
    double previewLayerCacheHostSr = 0.0;
    ats::SynthParams previewLayerCacheCore {};
    std::vector<float> previewLayerCachePartials;
    std::vector<float> previewLayerCacheResidual;

    /** Per slot: ch0 = partials-only render, ch1 = residual-only render; gains applied in processBlock. */
    std::array<juce::AudioBuffer<float>, 3> playbackBuffers;
    std::atomic<int> activePlaybackBufferIndex { -1 };
    std::atomic<int> readingPlaybackBufferIndex { -1 };
    int currentPlaybackBufferIndex = -1;
    int nextPlaybackBufferIndex = 0;
    int playbackPosition = 0;
    std::atomic<bool> previewPlaying { false };

    juce::LinearSmoothedValue<float> outputTrimSmoothed;
    juce::LinearSmoothedValue<float> partGainSmoothed;
    juce::LinearSmoothedValue<float> resGainSmoothed;

    std::mutex resynthMutex;
    std::atomic<uint64_t> resynthJobSerial { 0 };
    std::atomic<int> resynthDetachedWorkers { 0 };

    std::atomic<uint64_t> jobGeneration { 0 };
    std::thread analysisThread;
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

    /** Mirrors factory preset list for host program / preset menus. */
    int currentFactoryProgramIndex = 0;

    mutable juce::SpinLock envelopeTransferIdLock;
    juce::String envelopeTransferSourceId;
    juce::String envelopeTransferTargetId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessor)
};

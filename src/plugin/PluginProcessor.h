#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "AtsModel.h"
#include "AtsMorphRecipe.h"
#include "AtsParams.h"
#include "AtsTransientSmear.h"
#include "TimbreMotionPlan.h"

namespace ats_param
{
inline constexpr size_t kMorphLaneCount = static_cast<size_t>(ats::MorphLane::Count);
inline constexpr int kMorphQuickSnapshotCount = 5;
inline constexpr int kMorphVariationSlotCount = 3;
inline constexpr const char* kMorphAmount = "morphAmount";
inline constexpr const char* kMorphPerformanceMacro = "morphPerformanceMacro";
inline constexpr const char* kMorphSmoothingMs = "morphSmoothingMs";
inline constexpr const char* kMorphGridType = "morphGridType";
inline constexpr const char* kMorphCorrespondenceStrategy = "morphCorrespondenceStrategy";
inline constexpr const char* kMorphQuality = "morphQuality";
inline constexpr const char* kMorphUnmatchedSourcePolicy = "morphUnmatchedSourcePolicy";
inline constexpr const char* kMorphUnmatchedTargetPolicy = "morphUnmatchedTargetPolicy";
inline constexpr const char* kMorphDiscardOptIn = "morphDiscardOptIn";
inline constexpr const char* kMorphFrequencyInterpolationDomain = "morphFrequencyInterpolationDomain";
inline constexpr const char* kMorphAmplitudeInterpolationDomain = "morphAmplitudeInterpolationDomain";
inline constexpr const char* kMorphPhaseStrategy = "morphPhaseStrategy";
inline constexpr const char* kMorphTransientPolicy = "morphTransientPolicy";
inline constexpr const char* kAnalysisType = "analysisType";
inline constexpr const char* kAnalysisStart = "analysisStart";
inline constexpr const char* kAnalysisDuration = "analysisDuration";
inline constexpr const char* kAnalysisLowestFreq = "analysisLowestFreq";
inline constexpr const char* kAnalysisHighestFreq = "analysisHighestFreq";
inline constexpr const char* kAnalysisFreqDev = "analysisFreqDev";
inline constexpr const char* kAnalysisWinCycles = "analysisWinCycles";
inline constexpr const char* kAnalysisWinType = "analysisWinType";
inline constexpr const char* kAnalysisHopSize = "analysisHopSize";
inline constexpr const char* kAnalysisLowestMag = "analysisLowestMag";
inline constexpr const char* kAnalysisTrackLen = "analysisTrackLen";
inline constexpr const char* kAnalysisMinSegLen = "analysisMinSegLen";
inline constexpr const char* kAnalysisMinGapLen = "analysisMinGapLen";
inline constexpr const char* kAnalysisSmrThreshold = "analysisSmrThreshold";
inline constexpr const char* kAnalysisMinSegSmr = "analysisMinSegSmr";
inline constexpr const char* kAnalysisLastPeakContribution = "analysisLastPeakContribution";
inline constexpr const char* kAnalysisSmrContribution = "analysisSmrContribution";
inline constexpr const char* kClassicPreviewMode = "classicPreviewMode";
inline constexpr const char* kFreqShapeMode = "freqShapeMode";
inline constexpr const char* kFreqShapeAmount = "freqShapeAmount";
inline constexpr const char* kFreqShapePivotHz = "freqShapePivotHz";
inline constexpr const char* kSmearnovBlurMs = "smearnovBlurMs";
inline constexpr const char* kSmearnovSharpenAmount = "smearnovSharpenAmount";
inline constexpr const char* kSmearnovExaggerateDb = "smearnovExaggerateDb";
inline constexpr const char* kSmearnovMix = "smearnovMix";
inline constexpr const char* kSmearnovMaxDb = "smearnovMaxDb";
inline constexpr const char* kSmearnovTargets = "smearnovTargets";
inline constexpr const char* kSmearnovScope = "smearnovScope";
inline constexpr const char* kSmearnovWeightDetFlux = "smearnovWeightDetFlux";
inline constexpr const char* kSmearnovWeightResFlux = "smearnovWeightResFlux";
inline constexpr const char* kSmearnovWeightHfcFlux = "smearnovWeightHfcFlux";
inline constexpr const char* kSmearnovWeightBirth = "smearnovWeightBirth";
inline constexpr const char* kSmearnovWeightPhaseDev = "smearnovWeightPhaseDev";
inline constexpr const char* kSmearnovThresholdLambda = "smearnovThresholdLambda";
inline constexpr const char* kSmearnovThresholdAutoScale = "smearnovThresholdAutoScale";
inline constexpr const char* kSmearnovPeakWindowFrames = "smearnovPeakWindowFrames";
inline constexpr const char* kSmearnovMinGapFrames = "smearnovMinGapFrames";
inline constexpr const char* kSmearnovPreMarginMs = "smearnovPreMarginMs";
inline constexpr const char* kSmearnovMaxAttackFrames = "smearnovMaxAttackFrames";
inline constexpr const char* kSmearnovUsePhaseDeviation = "smearnovUsePhaseDeviation";
inline constexpr const char* kSmearnovUseRpcaFallback = "smearnovUseRpcaFallback";
inline constexpr const char* kTimbreBrightnessDb  = "timbreBrightnessDb";
inline constexpr const char* kTimbreTiltDbPerOct  = "timbreTiltDbPerOct";
inline constexpr const char* kTimbreNoiseBalance  = "timbreNoiseBalance";
inline constexpr std::array<const char*, kMorphLaneCount> kMorphLaneAmountIds = {
    "morphLaneFrequencyAmount",
    "morphLaneAmplitudeAmount",
    "morphLaneResidualAmount",
    "morphLaneTimeAmount",
    "morphLaneTransientAmount",
    "morphLanePhaseAmount",
};
inline constexpr std::array<const char*, kMorphLaneCount> kMorphLaneLinkedIds = {
    "morphLaneFrequencyLinked",
    "morphLaneAmplitudeLinked",
    "morphLaneResidualLinked",
    "morphLaneTimeLinked",
    "morphLaneTransientLinked",
    "morphLanePhaseLinked",
};
inline constexpr std::array<const char*, kMorphLaneCount> kMorphLaneDisplayNames = {
    "Freq",
    "Amp",
    "Noise",
    "Time",
    "Transient",
    "Phase",
};
inline constexpr std::array<float, kMorphQuickSnapshotCount> kMorphQuickSnapshotAmounts = {
    0.0f,
    0.25f,
    0.5f,
    0.75f,
    1.0f,
};
} // namespace ats_param

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

struct AtsResidualDiagnostics
{
    bool hasResidual = false;
    int activeBandCount = 0;
    int activeBandFrameCount = 0;
    double totalEnergy = 0.0;
    int peakBandIndex = -1;
    double peakBandEnergy = 0.0;
};

struct AtsMorphSourceSlotInfo
{
    juce::String slotName;
    juce::String sourceName;
    juce::String sourceHash;
    double durationSeconds = 0.0;
    double sampleRate = 0.0;
    double sourceGain = 1.0;
    double timeOffsetSeconds = 0.0;
    int frameCount = 0;
    int partialCount = 0;
    bool hasModel = false;
    bool hasPhase = false;
    bool hasResidual = false;
    AtsResidualDiagnostics residualDiagnostics;
    bool hasTransientMarkers = false;
};

struct AtsMorphBandDiagnostic
{
    int bandIndex = -1;
    double lowHz = 0.0;
    double highHz = 0.0;
    double sourcePower = 0.0;
    double targetPower = 0.0;
    double outputPower = 0.0;
    double expectedPower = 0.0;
    double energyDelta = 0.0;
    int matchedCount = 0;
    int unmatchedSourceCount = 0;
    int unmatchedTargetCount = 0;
    bool warning = false;
    juce::String inspectorText;
};

enum class AtsMorphSnapshotUiState
{
    Ready,
    Stale,
    Rebuilding,
    Warning
};

enum class AtsSourceAuditionMode
{
    Both = 0,
    PartialsOnly,
    ResidualOnly,
    SmearOnly // isolated smear contribution: (smeared render) - (dry render)
};

enum class AtsPreviewPlaybackKind
{
    None = 0,
    MorphPreview,
    MorphTimeline,
    SourceA,
    SourceB
};

struct AtsMorphSnapshotSlotInfo
{
    juce::String name;
    bool hasSnapshot = false;
    float morphAmount = 0.0f;
    float performanceMacro = 0.5f;
    float smoothingMs = 40.0f;
    std::array<float, ats_param::kMorphLaneCount> laneAmounts {};
    std::array<bool, ats_param::kMorphLaneCount> laneLinked {};
};

enum class MorphTimelinePointKind
{
    Morph  = 0, // standard A→B partial blend
    Hybrid = 1, // apply Source B's Bark envelope to Source A amplitudes
    EnvelopeMorph = 2 // blend A/B Bark envelopes and resample partials onto the result
};

struct AtsMorphTimelinePoint
{
    double timeNorm = 0.0;
    double morphAmount = 0.0;
    MorphTimelinePointKind kind = MorphTimelinePointKind::Morph;
    ats::MorphCurveShape curve = ats::MorphCurveShape::Linear;
};

enum class AtsTraceSource
{
    Current = 0,
    SourceA,
    SourceB
};

struct AtsPartialTracePoint
{
    float timeNorm = 0.0f;
    float freqNorm = 0.0f;
    float ampNorm = 0.0f;
    int frameIndex = 0;
    double frequencyHz = 0.0;
    double sourceFrequencyHz = 0.0;
    double amplitude = 0.0;
    bool active = false;
};

struct AtsPartialTrace
{
    int partialIndex = -1;
    float peakAmpNorm = 0.0f;
    double meanFrequencyHz = 0.0;
    double sourceMeanFrequencyHz = 0.0;
    int frequencyShapeRank = 0;
    std::vector<AtsPartialTracePoint> points;
};

struct AtsResidualBandTrace
{
    int bandIndex = -1;
    double lowHz = 0.0;
    double highHz = 0.0;
    std::vector<float> energyNorm;
};

/** Copied Bark-band residual energy (one band, contiguous frames). */
struct AtsResidualBandClipboard
{
    int sourceBandIndex = -1;
    int frameCount = 0;
    std::vector<double> energies;
    bool valid() const { return frameCount > 0 && (int) energies.size() == frameCount; }
};

struct AtsSpectralTraceSnapshot
{
    int partialCount = 0;
    int visiblePartialCount = 0;
    int frameCount = 0;
    double durationSeconds = 0.0;
    double minFrequencyHz = 20.0;
    double maxFrequencyHz = 20000.0;
    bool hasResidual = false;
    double ampDisplayReferencePeak = 0.0;
    double residualDisplayReferencePeak = 0.0;
    ats::FrequencyShapeMode frequencyShapeMode = ats::FrequencyShapeMode::Linear;
    double frequencyShapeAmount = 1.0;
    double frequencyShapePivotHz = 0.0;
    double resolvedFrequencyShapePivotHz = 0.0;
    uint64_t displayShapeRevision = 0;
    std::vector<AtsPartialTrace> partials;
    std::vector<AtsResidualBandTrace> residualBands;
    std::vector<ats::AtsOnset> onsets;
    std::vector<double> smearnovDetectionCurve;
    std::vector<double> smearnovThresholdCurve;
};

struct AtsSpectralTraceDisplayReference
{
    double ampPeak = 0.0;
    double residualPeak = 0.0;
};

// Defined in PluginProcessor.cpp: cross-render cache of per-frame Bark (True-Envelope) builds for
// the Hybrid envelope-transfer / EnvelopeMorph lanes (held by pointer so the header stays light).
struct MorphEnvelopeCache;

class AtsAudioProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AsyncUpdater,
                          private juce::Timer
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

    // Declared before apvts so it is fully constructed when passed to the
    // AudioProcessorValueTreeState constructor (member init order = declaration order).
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // --- Undo / redo --------------------------------------------------------
    /** Heap ATS_SOUND snapshot (deep copy) with matching free; reusable across redo. */
    using AtsSoundSnapshot = std::shared_ptr<ATS_SOUND>;
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    juce::String getUndoDescription() const;
    juce::String getRedoDescription() const;
    /** Deep snapshot of the live model for `source` (null when there is no model). */
    AtsSoundSnapshot captureModelSnapshot(AtsTraceSource source) const;
    /** Replaces the live model for `source` with `snapshot` and refreshes derived state. */
    void installModelSnapshot(AtsTraceSource source, const AtsSoundSnapshot& snapshot);
    /** Sets morph timeline points and records an undo transaction labelled `label`. */
    void setMorphTimelinePointsUndoable(const std::vector<AtsMorphTimelinePoint>& points,
                                        const juce::String& label);

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
    int getMorphRecipeRevision() const { return morphRecipeRevision.load(); }
    uint64_t getDisplayShapeRevision() const { return displayShapeRevision.load(std::memory_order_acquire); }
    ats::FrequencyShapeSettings getCurrentFrequencyShapeSettings() const;

    /** Fills `out` (ARGB) with a downsampled log-amplitude map; returns false if no model. */
    bool buildSpectrogramTexture(bool partialsMode, int width, int height, juce::Image& out);

    /** Builds a downsampled partial-trajectory snapshot for UI drawing/edit hit-testing. */
    bool buildSpectralTraceSnapshot(AtsSpectralTraceSnapshot& out,
                                    int maxPartials,
                                    int maxPointsPerPartial,
                                    int residualColumns,
                                    const AtsSpectralTraceDisplayReference& displayReference = {}) const;
    bool buildSpectralTraceSnapshot(AtsTraceSource source,
                                    AtsSpectralTraceSnapshot& out,
                                    int maxPartials,
                                    int maxPointsPerPartial,
                                    int residualColumns,
                                    const AtsSpectralTraceDisplayReference& displayReference = {}) const;

    /** Non-destructively deletes from the user's perspective by zeroing selected partial amplitudes. */
    bool mutePartials(const std::vector<int>& partialIndices, juce::String& error);
    bool mutePartials(AtsTraceSource source, const std::vector<int>& partialIndices, juce::String& error);
    bool scalePartials(AtsTraceSource source,
                       const std::vector<int>& partialIndices,
                       double amplitudeScale,
                       juce::String& error);

    /** Flattens partial amplitudes onto a fitted Bark-RDC spectral envelope over an inclusive
        frame range. smoothness in [0,1] controls the polynomial order/regularization. */
    bool smoothPartialEnvelope(AtsTraceSource source,
                               int firstFrame,
                               int lastFrame,
                               double smoothness,
                               juce::String& error);

    /** Scales partial amplitudes by a gain factor inside a time x frequency box: frames in
        [firstFrame,lastFrame] and partials whose per-frame frequency lies in [freqLoHz,freqHiHz].
        Pass freqLoHz <= 0 and freqHiHz <= 0 to apply across the full frequency range. */
    bool gainPartialEnvelope(AtsTraceSource source,
                             int firstFrame,
                             int lastFrame,
                             double freqLoHz,
                             double freqHiHz,
                             double gainFactor,
                             juce::String& error);

    /** Deletes residual energy in selected critical bands over an inclusive frame range. */
    bool muteResidualBands(const std::vector<int>& bandIndices, int firstFrame, int lastFrame, juce::String& error);
    bool muteResidualBands(AtsTraceSource source,
                           const std::vector<int>& bandIndices,
                           int firstFrame,
                           int lastFrame,
                           juce::String& error);
    bool scaleResidualBands(AtsTraceSource source,
                            const std::vector<int>& bandIndices,
                            int firstFrame,
                            int lastFrame,
                            double energyScale,
                            juce::String& error);

    bool readResidualBandEnergyDb(AtsTraceSource source, int bandIndex, int frameIndex, double& dbOut) const;
    bool writeResidualBandEnergyDb(AtsTraceSource source,
                                   int bandIndex,
                                   int frameIndex,
                                   double db,
                                   juce::String& error);
    bool paintResidualBandEnvelope(AtsTraceSource source,
                                   int bandIndex,
                                   int firstFrame,
                                   int lastFrame,
                                   const std::vector<std::pair<int, double>>& frameDbPairs,
                                   juce::String& error);
    /** Write a drawn frequency contour onto one partial across a frame range.
        Only frames where the partial is active (amplitude > 0) are touched. */
    bool paintPartialFrequency(AtsTraceSource source,
                               int partialIdx,
                               int firstFrame,
                               int lastFrame,
                               const std::vector<std::pair<int, double>>& frameHzPairs,
                               juce::String& error);
    bool smoothResidualBands(AtsTraceSource source,
                             const std::vector<int>& bandIndices,
                             int firstFrame,
                             int lastFrame,
                             int windowRadiusFrames,
                             juce::String& error);
    bool tiltResidualBands(AtsTraceSource source,
                           const std::vector<int>& bandIndices,
                           int firstFrame,
                           int lastFrame,
                           double tiltDbPerBandStep,
                           juce::String& error);
    /** Apply a spectral tilt (dB/octave slope) to partial amplitudes in a time × frequency box.
        The pivot frequency is the geometric mean of [freqLoHz, freqHiHz], or of the model's
        full range when freqLoHz <= 0 or freqHiHz <= 0. */
    bool tiltPartialEnvelope(AtsTraceSource source,
                             int firstFrame,
                             int lastFrame,
                             double freqLoHz,
                             double freqHiHz,
                             double tiltDbPerOctave,
                             juce::String& error);
    bool copyResidualBandToClipboard(AtsTraceSource source,
                                     int bandIndex,
                                     int firstFrame,
                                     int lastFrame,
                                     juce::String& error);
    bool pasteResidualBandFromClipboard(AtsTraceSource source,
                                        int bandIndex,
                                        int firstFrame,
                                        int lastFrame,
                                        juce::String& error);
    bool swapResidualBands(AtsTraceSource source,
                           int bandIndexA,
                           int bandIndexB,
                           int firstFrame,
                           int lastFrame,
                           juce::String& error);
    bool autoRedistributeResidualBands(AtsTraceSource source,
                                       const std::vector<int>& bandIndices,
                                       int firstFrame,
                                       int lastFrame,
                                       juce::String& error);
    bool hasResidualBandClipboard() const;
    const AtsResidualBandClipboard& getResidualBandClipboard() const { return residualBandClipboard; }

    using ResidualBandFrameMutator = std::function<void(int bandIndex, int frame, double& energy)>;
    bool mutateResidualBandFrames(AtsTraceSource source,
                                  const std::vector<int>& bandIndices,
                                  int firstFrame,
                                  int lastFrame,
                                  const ResidualBandFrameMutator& mutator,
                                  juce::String& error);

    bool detectOnsets(AtsTraceSource source,
                      const ats::SmearnovDetectParams& params,
                      std::vector<ats::AtsOnset>& out,
                      juce::String& error) const;
    bool detectOnsets(AtsTraceSource source, std::vector<ats::AtsOnset>& out, juce::String& error) const;
    bool smearAttacks(AtsTraceSource source,
                      const std::vector<ats::AtsOnset>& onsets,
                      const ats::SmearnovParams& params,
                      juce::String& error);
    bool smearAttacks(AtsTraceSource source, const std::vector<ats::AtsOnset>& onsets, juce::String& error);
    // selectionFirstFrame/LastFrame are only consulted when the Scope parameter
    // is "Selection"; pass -1/-1 when there is no display selection.
    void startSmearnovApply(AtsTraceSource source,
                            int selectionFirstFrame = -1,
                            int selectionLastFrame = -1);

    /** Onset overrides drive the Smearnov overlay edits (drag/disable). When an
     *  override is active for a source it replaces auto-detection in the trace
     *  snapshot and is what `startSmearnovApply`/`smearAttacks` operate on. */
    std::vector<ats::AtsOnset> getSmearnovOnsets(AtsTraceSource source) const;
    bool hasSmearnovOnsetOverrides(AtsTraceSource source) const;
    /** `userEdited` = true for manual overlay edits (kept across detection-param
     *  changes); false for a pinned auto-detection (dropped when detection params
     *  change). Defaults to true since the overlay is the manual-edit caller. */
    void setSmearnovOnsetOverrides(AtsTraceSource source,
                                   std::vector<ats::AtsOnset> onsets,
                                   bool userEdited = true);
    void clearSmearnovOnsetOverrides(AtsTraceSource source);
    /** Bumps whenever the editable onset overlay changes, so the UI can refresh
     *  markers without a full model-revision (audio cache) invalidation. */
    uint64_t getSmearnovOverlayRevision() const
    {
        return smearnovOverlayRevision.load(std::memory_order_acquire);
    }

    /** Active control-cluster tab (0 = Morph, 1 = Smearnov). UI-only state that
     *  persists with the session so the editor reopens on the same tab. */
    int getActiveControlTab() const { return activeControlTab.load(std::memory_order_acquire); }
    void setActiveControlTab(int tab)
    {
        activeControlTab.store(juce::jlimit(0, 1, tab), std::memory_order_release);
    }

    bool isWorkerBusy() const;

    /** Returns false if no model. */
    bool getModelFootprint(int& partials, int& frames, bool& hasResidualBands) const;

    bool saveFullStatePreset(const juce::File& file);
    bool loadFullStatePreset(const juce::File& file);

    /** Native ATS file (Qatsh/ATSA ecosystem). Uses current analysis SMR/type for save. */
    bool exportAtsFile(const juce::File& file, juce::String& error);
    bool exportMorphSourceSlotAts(int slotIndex, const juce::File& file, juce::String& error);
    bool importAtsFile(const juce::File& file, juce::String& error);

    /** Multi-line script; see docs/MATHEXPR.md. Runs on a worker thread. */
    void startMathExpr(const juce::String& script);

    /** Read one sinusoidal cell; returns false if out of range or no model. */
    bool readPartialCell(int partialIdx, int frameIdx, double& amp, double& freq, double& phase, bool& hasPhase);

    /** Write amplitude and frequency for one cell; validates invariants. */
    bool writePartialCell(int partialIdx, int frameIdx, double amp, double freq, juce::String& error);

    juce::String getMathStatus() const;
    bool playCurrentModelPreview(juce::String& error);
    bool playCurrentModelPreview(juce::String& error, bool liveSmear);
    bool playCurrentModelPreview(AtsSourceAuditionMode mode, bool liveSmear, juce::String& error);
    /** Audition only the specified partials from the given source by silencing all others. */
    bool playSelectedPartialsPreview(AtsTraceSource source,
                                     const std::vector<int>& partialIndices,
                                     juce::String& error);
    /** Freeze all partials (and residual bands) at the given frame — copies that frame's data
        to every other frame in the model, creating a sustained static spectrum. */
    bool freezePartialAtFrame(AtsTraceSource source, int frame, juce::String& error);
    /** Seek preview playback to a normalized position [0,1] in the active buffer. No-op if
        nothing is playing. Thread-safe (posts via atomic; applied in processBlock). */
    void seekPreviewPlaybackNorm(float norm);
    void startPreviewPlayback();
    void stopPreviewPlayback();
    bool isPreviewPlaying() const { return previewPlaying.load(); }
    AtsPreviewPlaybackKind getPreviewPlaybackKind() const
    {
        return (AtsPreviewPlaybackKind) previewPlaybackKind.load(std::memory_order_acquire);
    }
    /** [0,1] progress for preview playback matching the requested trace source, or -1 when inactive. */
    float getPreviewPlayheadNormForTraceSource(AtsTraceSource source) const;
    bool hasPreviewAudio() const;
    bool hasMorphSourcePreviewAudio(int slotIndex) const;
    bool hasMorphPreviewAudio() const;
    bool canPlayMorphPreview() const;
    bool playMorphPreview(juce::String& error);
    bool playMorphTimelinePreview(juce::String& error);
    bool playMorphTimelinePreview(AtsSourceAuditionMode mode, juce::String& error);
    bool playMorphTimelinePreview(AtsSourceAuditionMode mode, bool liveSmear, juce::String& error);
    bool bounceMorphTimelineToFile(const juce::File& file, AtsSourceAuditionMode mode, juce::String& error);
    bool installPreparedMorphSnapshotForPlayback(const ats::MorphRenderSnapshot& snapshot,
                                                 double durationSeconds,
                                                 juce::String& error);
    std::vector<AtsMorphTimelinePoint> getMorphTimelinePoints() const;
    void setMorphTimelinePoints(const std::vector<AtsMorphTimelinePoint>& points);
    // Timbre Motion Plan persistence (stored in the morph recipe's timbre_motion_plan field).
    ats::TimbreMotionPlan getTimbreMotionPlan() const;
    void setTimbreMotionPlan(const ats::TimbreMotionPlan& plan);
    void setTimbreMotionPlanUndoable(const ats::TimbreMotionPlan& plan, const juce::String& label);
    bool loadAndAnalyzeMorphSourceSlot(int slotIndex, const juce::File& file, juce::String& error);
    bool reanalyzeMorphSourceSlot(int slotIndex, juce::String& error);
    bool getMorphSourceFile(int slotIndex, juce::File& out) const;
    int getPendingMorphSourceSlot() const { return pendingMorphSourceSlotAfterAnalysis.load(std::memory_order_acquire); }
    bool getMorphSourceSlotInfo(int slotIndex, AtsMorphSourceSlotInfo& out) const;
    bool setCurrentModelAsMorphSourceSlot(int slotIndex, juce::String& error);
    bool listenMorphSourceSlot(int slotIndex, AtsSourceAuditionMode mode, juce::String& error);
    bool listenMorphSourceSlot(int slotIndex, AtsSourceAuditionMode mode, bool liveSmear, juce::String& error);
    bool listenMorphSourceSlot(int slotIndex, juce::String& error);
    std::vector<AtsMorphBandDiagnostic> buildMorphBandDiagnostics() const;
    void applyMorphQuickSnapshot(float amount);
    bool saveCurrentMorphVariation(int slotIndex, juce::String& error);
    bool recallMorphVariation(int slotIndex, juce::String& error);
    bool getMorphVariationSlotInfo(int slotIndex, AtsMorphSnapshotSlotInfo& out) const;
    AtsMorphSnapshotUiState getMorphSnapshotUiState() const;
    juce::String getMorphSnapshotStatusText() const;
    void setMorphPresetMetadata(const juce::String& presetName, const juce::String& description);

    static constexpr uint32_t stateMagic = 0x41545350u; // 'ATSP'

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    void setUiState(AtsUiState s);
    void setError(const juce::String& e);
    void setMathStatus(const juce::String& s);

    void startResynthWorker();
    void joinWorkers();
    void clearPreviewLayerCache();
    void markMorphSnapshotStale();

    /** Captures the post-edit model and pushes an undo transaction (message thread only). */
    void recordModelEdit(AtsTraceSource source, AtsSoundSnapshot before, const juce::String& label);

    // RAII guard placed at the top of each synchronous model-edit method. On
    // construction it snapshots the model; on destruction, if the edit bumped
    // modelRevision (i.e. actually changed something), it records an undo entry.
    // A recursion counter dedups nested edits (e.g. scalePartials -> mutePartials)
    // so one user action yields one undo entry with the outermost label.
    struct ScopedModelEdit
    {
        ScopedModelEdit(AtsAudioProcessor& p, AtsTraceSource s, juce::String l);
        ~ScopedModelEdit();
        AtsAudioProcessor& proc;
        AtsTraceSource source;
        juce::String label;
        AtsSoundSnapshot before;
        int revAtStart = 0;
        bool active = false;
    };
    int modelEditRecursion = 0; // message-thread only

    // Worker-thread edits (math script, smear apply) can't touch the UndoManager.
    // The before-snapshot is captured at kickoff on the message thread; the worker
    // sets asyncUndoCommit on success and the entry is recorded in handleAsyncUpdate.
    AtsSoundSnapshot asyncUndoBefore;
    AtsTraceSource asyncUndoSource = AtsTraceSource::Current;
    juce::String asyncUndoLabel;
    std::atomic<bool> asyncUndoCommit { false };
    int undoTransactionTick = 0;
    std::int64_t lastTimelineEditMs = 0; // coalesces a continuous timeline drag
    std::int64_t lastTimbreMotionEditMs = 0; // coalesces a continuous timbre-motion drag
    bool renderCurrentModelToPlaybackBuffer(int bufferIndex, juce::String& error, bool liveSmear = false,
                                            bool smearIsolate = false, bool renderCombinedPass = false);
    bool renderMorphSourceSlotToPlaybackBuffer(int slotIndex, juce::String& error, bool liveSmear = false,
                                               bool smearIsolate = false, bool renderCombinedPass = false);
    void rebuildMorphCorrespondenceIfReady();

    // --- Live (non-destructive) Smearnov preview (transport "Smear" toggle) ---
    /** Hash of the current detect+smear params + effective scope + overlay revision; the
     *  smeared-clone caches rebuild when it changes. */
    [[nodiscard]] size_t computeSmearLiveHash() const;
    /** Detect (or reuse override) + scope-filter + smearAttacks on `clone` in place.
     *  Returns true if any attack window changed. Operates on the caller's private copy. */
    bool applyLiveSmearToClone(AtsTraceSource source, ATS_SOUND* clone) const;
    /** Borrowed pointer to the cached smeared clone for `source`, or nullptr when there is no
     *  model or smearing changed nothing (caller then renders the unmodified source). Rebuilds
     *  the cache lazily on source-revision / param-hash change. Renders are serialized
     *  (isWorkerBusy / joinWorkers), so the borrowed pointer is valid for the caller's render. */
    [[nodiscard]] const ATS_SOUND* getSmearedSound(AtsTraceSource source, juce::String& error);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    ats::SynthParams readSynthParamsFromApvts() const;
    /** Freq/time/phase flags only; caller sets partialsGain and residualGain for layer renders. */
    ats::SynthParams readCoreSynthParamsFromApvts() const;
    ats::FrequencyShapeSettings readFrequencyShapeFromApvts() const;
    ats::AnalysisParams readAnalysisParamsFromApvts() const;
    ats::SmearnovDetectParams readSmearnovDetectParamsFromApvts() const;
    ats::SmearnovParams readSmearnovParamsFromApvts() const;
    void getUiOnsetDetection(AtsTraceSource source,
                             const ATS_SOUND* sound,
                             uint64_t sourceRevision,
                             const ats::SmearnovDetectParams& params,
                             std::vector<ats::AtsOnset>& onsets,
                             ats::SmearnovDetectionFunctions& functions) const;
    /** Copies the active onset override for `source` into `out`; false if none. */
    bool copySmearnovOnsetOverride(AtsTraceSource source, std::vector<ats::AtsOnset>& out) const;
    /** Drops pinned auto-detected overrides (keeps manual overlay edits). Called
     *  when a Smearnov detection parameter changes so the next Apply re-detects. */
    void invalidateAutoDetectedOnsetOverrides();
    static bool isSmearnovDetectionParameter(const juce::String& parameterID);

    std::atomic<AtsUiState> uiState { AtsUiState::NoSource };
    std::atomic<float> analysisProgress { 0.0f };
    std::atomic<bool> synthParamsDirty { false };

    juce::SpinLock errorLock;
    juce::String lastError;

    juce::CriticalSection sourceLock;
    std::vector<double> sourceMono;
    int sourceSampleRate = 0;
    juce::File currentSourceFile;
    std::array<juce::File, 2> morphSourceFiles;

    juce::CriticalSection modelLock;
    ats::AtsSoundPtr analyzedModel;
    juce::String currentModelName;

    juce::CriticalSection morphRecipeLock;
    AtsResidualBandClipboard residualBandClipboard;
    ats::MorphRecipe morphRecipe;
    /** Raw text is kept so unknown future recipe fields survive plugin-state reloads. */
    std::string serializedMorphRecipe;
    std::array<std::vector<ats::MorphBandAssignment>, 2> morphSourceAssignments;
    std::array<AtsResidualDiagnostics, 2> morphSourceDiagnostics;
    std::array<std::vector<ats::MorphResidualNoiseBand>, 2> morphSourceResidualBands;
    std::array<ats::AtsSoundPtr, 2> morphSourceSounds; // full per-frame data; protected by morphRecipeLock

    juce::CriticalSection morphSnapshotLock;
    std::array<AtsMorphSnapshotSlotInfo, ats_param::kMorphVariationSlotCount> morphVariationSlots;
    std::atomic<int> morphSnapshotState { (int) AtsMorphSnapshotUiState::Ready };
    std::atomic<int> pendingMorphSourceSlotAfterAnalysis { -1 };
    std::atomic<int> activeControlTab { 0 }; // 0 = Morph, 1 = Smearnov (persisted UI state)

    /** Host-rate partial / residual / classic-combined layers; skip re-synthesis when core + model revision unchanged. */
    bool previewLayerCacheValid = false;
    int previewLayerCacheRevision = -1;
    double previewLayerCacheHostSr = 0.0;
    ats::SynthParams previewLayerCacheCore {};
    juce::AudioBuffer<float> previewLayerCachePartials;
    juce::AudioBuffer<float> previewLayerCacheResidual;
    juce::AudioBuffer<float> previewLayerCacheCombined;

    /** Caches the assembled transport-preview render per playback buffer so repeated Play of an
        unchanged source/timeline reuses the audio instead of re-running full additive synthesis.
        Stored independently of playbackBuffers so other (non-cached) writers can't yield a false hit. */
    struct TransportRenderCacheEntry
    {
        bool valid = false;
        int sourceTag = -1;            // 0 = Source A, 1 = Source B, 100 = current analyzed model
        int modelRevisionSnap = -1;
        uint64_t morphSourcesRevisionSnap = 0;
        uint64_t displayShapeRevisionSnap = 0;
        double hostSr = 0.0;
        ats::SynthParams core {};
        bool liveSmear = false;
        bool smearIsolate = false;
        bool renderCombinedPass = false;
        size_t smearHash = 0;
        uint64_t smearOverlayRevisionSnap = 0;
        juce::AudioBuffer<float> buffer;
    };
    std::array<TransportRenderCacheEntry, 3> transportRenderCache;
    bool transportCacheTryReuse(int bufferIndex, int sourceTag, const ats::SynthParams& core,
                                bool liveSmear, bool smearIsolate, bool renderCombinedPass);
    void transportCacheStore(int bufferIndex, int sourceTag, const ats::SynthParams& core,
                             bool liveSmear, bool smearIsolate, bool renderCombinedPass,
                             const juce::AudioBuffer<float>& buffer);

    /** Per slot: ch0 = partials-only, ch1 = residual-only, optional ch2 = classic combined. */
    std::array<juce::AudioBuffer<float>, 3> playbackBuffers;
    std::atomic<int> activePlaybackBufferIndex { -1 };
    std::atomic<int> readingPlaybackBufferIndex { -1 };
    int currentPlaybackBufferIndex = -1;
    int nextPlaybackBufferIndex = 0;
    int playbackPosition = 0;
    std::atomic<int> seekToSample { -1 }; // -1 = no pending seek; set by seekPreviewPlaybackNorm
    /** Sample position at the start of the current audio block; UI interpolates from anchor time. */
    std::atomic<double> previewPlaybackAnchorSamples { 0.0 };
    std::atomic<double> previewPlaybackAnchorTimeMs { 0.0 };
    std::atomic<double> previewPlaybackHostSampleRate { 44100.0 };
    std::atomic<bool> previewPlaying { false };
    std::atomic<int> previewAuditionMode { (int) AtsSourceAuditionMode::Both };
    std::atomic<int> previewPlaybackKind { (int) AtsPreviewPlaybackKind::None };

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
    std::atomic<int> morphRecipeRevision { 0 };
    std::atomic<uint64_t> displayShapeRevision { 0 };

    // Onset overrides per trace source (Current, SourceA, SourceB). `userEdited`
    // distinguishes a manual overlay edit (drag/disable, survives detection-param
    // changes) from a pinned auto-detection (cleared when detection params change).
    mutable juce::CriticalSection smearnovOnsetLock;
    std::array<std::vector<ats::AtsOnset>, 3> smearnovOnsetOverrides;
    std::array<bool, 3> smearnovOnsetOverrideActive { { false, false, false } };
    std::array<bool, 3> smearnovOnsetOverrideUserEdited { { false, false, false } };
    std::atomic<uint64_t> smearnovOverlayRevision { 0 };

    struct UiOnsetDetectionCache
    {
        std::vector<ats::AtsOnset> onsets;
        ats::SmearnovDetectionFunctions functions;
        ats::SmearnovDetectParams params {};
        uint64_t sourceRevision = 0;
        bool valid = false;
    };
    mutable juce::CriticalSection uiOnsetDetectionCacheLock;
    mutable std::array<UiOnsetDetectionCache, 3> uiOnsetDetectionCache;

    // Live (non-destructive) smear caches. Guarded by smearedCacheLock. `sound` is nullptr
    // when smearing changed nothing (so the caller falls back to the unmodified source).
    struct SmearedSourceCache
    {
        ats::AtsSoundPtr sound;
        uint64_t sourceRevision = 0;
        size_t paramHash = 0;
        bool valid = false;
    };
    // Cached correspondence for the live-smeared morph timeline (the heavy part flagged in the
    // plan). Records the smeared-source geometry so repeated Timeline plays skip the rebuild.
    struct SmearedTimelineCache
    {
        std::vector<ats::MorphCorrespondenceRecord> records;
        uint64_t revA = 0;
        uint64_t revB = 0;
        size_t paramHash = 0;
        int strategy = -1;
        int quality = -1;
        bool valid = false;
    };
    mutable juce::CriticalSection smearedCacheLock;
    SmearedSourceCache smearedCurrent;
    SmearedSourceCache smearedA;
    SmearedSourceCache smearedB;
    SmearedTimelineCache smearedTimeline;
    /** Bumped by markMorphSnapshotStale whenever Source A/B state changes; keys the A/B and
     *  timeline live-smear caches (coarse: any source change invalidates both slots). */
    std::atomic<uint64_t> morphSourcesRevision { 0 };

    /** Cross-render memo of the per-frame Bark envelope builds in renderMorphTrajectoryBuffer, so a
     *  morph replay after a non-source change (morph amount, timeline edit) skips the iterative
     *  True-Envelope rebuilds. Keyed on source pointer + morphSourcesRevision; results are
     *  bit-identical to a fresh build. */
    std::unique_ptr<MorphEnvelopeCache> morphEnvelopeCache;

    // Debounce for freq-shape resynth: avoid spawning a thread on every drag tick.
    std::atomic<bool> freqShapePendingResynth { false };
    std::atomic<int64_t> freqShapeLastChangeMs { 0 };

    // Auto-preview: when a morph parameter changes after the user has played a morph
    // preview, re-render and restart playback automatically after the debounce period.
    // 0 = never played, 1 = last was playMorphPreview, 2 = last was playMorphTimelinePreview.
    std::atomic<int> lastMorphPlayKind { 0 };
    std::atomic<bool> morphAutoRerenderPending { false };
    std::atomic<int64_t> morphAutoRerenderLastChangeMs { 0 };
    static constexpr int64_t kMorphAutoRerenderDebounceMs = 350;

    /** Semitones relative to middle C (note 60) applied on top of the Freq APVTS factor. */
    std::atomic<float> midiSemitoneOffset { 0.0f };

    /** Mirrors factory preset list for host program / preset menus. */
    int currentFactoryProgramIndex = 0;

    /** Cached raw parameter pointers for lock-free access on the audio thread. */
    std::atomic<float>* outputTrimRaw = nullptr;
    std::atomic<float>* partGainRaw = nullptr;
    std::atomic<float>* resGainRaw = nullptr;
    std::atomic<float>* classicPreviewModeRaw = nullptr;
    std::atomic<float>* timbreBrightnessDbRaw = nullptr;
    std::atomic<float>* timbreTiltDbPerOctRaw = nullptr;
    std::atomic<float>* timbreNoiseBalanceRaw = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtsAudioProcessor)
};

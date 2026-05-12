#pragma once

#include "AtsModel.h"
#include "AtsModernModel.h"
#include "AtsParams.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ats
{

struct AnalysisModeDescriptor
{
    ModernAtsAnalysisMode analysisMode = ModernAtsAnalysisMode::ClassicAts;
    ModernAtsTrackerMode defaultTrackerMode = ModernAtsTrackerMode::ClassicRecursiveFrequencySmr;
    const char* id = "classic_ats";
    const char* label = "Classic ATS";
    bool compatibilityDefault = false;
    bool enabled = true;
};

const std::vector<AnalysisModeDescriptor>& analysisModeRegistry();
const AnalysisModeDescriptor* findAnalysisModeDescriptor(const std::string& id);
const AnalysisModeDescriptor& defaultAnalysisModeDescriptor();

struct SpectrumBin
{
    double frequencyHz = 0.0;
    double magnitudeDb = 0.0;
    double phaseRadians = 0.0;
};

struct SpectralPeak
{
    int binIndex = 0;
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    double phaseRadians = 0.0;
    double smrDb = 0.0;
    double magnitudeDb = 0.0;
};

struct PeakEstimationConfig
{
    double lowestFrequencyHz = 20.0;
    double highestFrequencyHz = 20000.0;
    double magnitudeThresholdDb = -60.0;
    bool interpolateParabolically = true;
};

struct PeakEstimationDiagnostics
{
    int rawPeaks = 0;
    int acceptedPeaks = 0;
    int rejectedPeaks = 0;
    double lowestFrequencyHz = 0.0;
    double highestFrequencyHz = 0.0;
    double magnitudeThresholdDb = 0.0;
    std::vector<std::string> rejectedPeakReasons;
};

class IPeakEstimator
{
public:
    virtual ~IPeakEstimator() = default;
    virtual const char* id() const = 0;
    virtual std::vector<SpectralPeak> estimatePeaks(const std::vector<SpectrumBin>& bins,
                                                    const PeakEstimationConfig& config,
                                                    PeakEstimationDiagnostics& diagnostics) const = 0;
};

class ClassicStftPeakEstimator final : public IPeakEstimator
{
public:
    const char* id() const override { return "classic_stft_peak_estimator"; }
    std::vector<SpectralPeak> estimatePeaks(const std::vector<SpectrumBin>& bins,
                                            const PeakEstimationConfig& config,
                                            PeakEstimationDiagnostics& diagnostics) const override;
};

class ExperimentalReassignedPeakEstimator final : public IPeakEstimator
{
public:
    const char* id() const override { return "experimental_reassigned_peak_estimator"; }
    std::vector<SpectralPeak> estimatePeaks(const std::vector<SpectrumBin>& bins,
                                            const PeakEstimationConfig& config,
                                            PeakEstimationDiagnostics& diagnostics) const override;
};

struct PeakEstimatorComparison
{
    std::string baselineId;
    std::string candidateId;
    int baselineAcceptedPeaks = 0;
    int candidateAcceptedPeaks = 0;
    double matchedPeakRatio = 0.0;
    double meanAbsFrequencyDeltaHz = 0.0;
    double baselineCpuMillis = 0.0;
    double candidateCpuMillis = 0.0;
    std::string recommendation;
};

PeakEstimatorComparison comparePeakEstimators(const IPeakEstimator& baseline,
                                              const IPeakEstimator& candidate,
                                              const std::vector<SpectrumBin>& bins,
                                              const PeakEstimationConfig& config,
                                              double peakMatchToleranceHz = 25.0);

struct ClassicAtsAnalysisResult
{
    AtsSoundPtr sound;
    std::vector<ModernAtsTrackLifecycleEvent> trackLifecycle;
    std::string errorMessage;
};

ClassicAtsAnalysisResult runClassicAtsAnalysis(const AnalysisParams& params,
                                               const std::vector<double>& monoSamples,
                                               int sampleRate);

struct ModernAtsAnalysisConfig
{
    ModernAtsAnalysisMode analysisMode = ModernAtsAnalysisMode::ModernAts;
    ModernAtsTrackerMode trackerMode = ModernAtsTrackerMode::ModernAssignment;
    bool useExperimentalPeakEstimator = false;
    bool captureTransientBuffers = true;
    int inputChannelCount = 1;
    std::size_t maxPeaksPerFrame = 96;
    std::size_t maxCandidatesPerTrack = 8;
    double maxTransientBufferSeconds = 0.10;
};

struct ModernAtsAnalysisResult
{
    ModernAtsModel model;
    std::vector<PeakEstimationDiagnostics> peakDiagnostics;
    std::string errorMessage;
};

ModernAtsAnalysisResult runModernAtsAnalysis(const AnalysisParams& params,
                                             const std::vector<double>& monoSamples,
                                             int sampleRate,
                                             const ModernAtsAnalysisConfig& config);

std::vector<ModernAtsTrackLifecycleEvent> normalizeTrackLifecycleFromActivity(int partialId,
                                                                              const std::vector<bool>& activity,
                                                                              int shortGapFrames,
                                                                              int minSegmentFrames);
std::vector<ModernAtsTrackLifecycleEvent> normalizeTrackLifecycleFromActivity(
    int partialId,
    const std::vector<bool>& activity,
    int shortGapFrames,
    int minSegmentFrames,
    const std::vector<int>& protectedFrameIndices,
    bool allowGapFillAcrossProtectedFrames);

struct TrackingPeak
{
    int sourceIndex = 0;
    double frequencyHz = 0.0;
    double amplitude = 0.0;
    double smrDb = 0.0;
    double phaseRadians = 0.0;
};

using TrackingPeakFrame = std::vector<TrackingPeak>;

struct AssignmentTrackerConfig
{
    ModernAtsAnalysisMode analysisMode = ModernAtsAnalysisMode::ModernAts;
    std::size_t maxCandidatesPerTrack = 8;
    double maxMatchCost = 0.35;
    double frequencyWeight = 1.0;
    double amplitudeWeight = 0.2;
    double smrWeight = 0.05;
    double frequencySlopeWeight = 0.6;
    double birthPenalty = 0.25;
    double deathPenalty = 0.25;
    double spuriousPeakPenalty = 0.25;
    bool useFrequencySlopeContinuity = true;
    bool allowInterpolationAcrossTransients = false;
    int maxGapFillFrames = 0;
    std::vector<int> transientBreakFrameIndices;
};

struct AssignmentTrackerResult
{
    std::vector<ModernAtsPartial> partials;
    std::vector<ModernAtsTrackLifecycleEvent> trackLifecycle;
    std::vector<ModernAtsAssignmentCost> assignmentCosts;
    int rejectedPeakCount = 0;
};

AssignmentTrackerResult runAssignmentTrackerPrototype(const std::vector<TrackingPeakFrame>& frames,
                                                      const AssignmentTrackerConfig& config);

std::vector<double> computeOrphanResidualBandEnergy(const std::vector<double>& totalBandEnergy,
                                                    const std::vector<double>& assignedBandEnergy);

std::vector<float> renderOrphanResidualBandNoise(const std::vector<std::vector<double>>& orphanBandEnergyByFrame,
                                                 int hopSizeSamples,
                                                 float gain,
                                                 std::uint32_t seed,
                                                 int sampleRate = 48000);

struct TransientFeatureFrame
{
    double timeSeconds = 0.0;
    double energy = 0.0;
    double spectralFlux = 0.0;
    double modelDiscontinuity = 0.0;
};

struct TransientDetectionConfig
{
    double spectralFluxWeight = 1.0;
    double energyRiseWeight = 0.5;
    double modelDiscontinuityWeight = 0.5;
    double thresholdStdDevMultiplier = 1.0;
    double minimumScore = 0.1;
    double preWindowSeconds = 0.01;
    double postWindowSeconds = 0.03;
    double minimumGapSeconds = 0.04;
    double markerGain = 1.0;
};

struct TransientDetectionDiagnostics
{
    double threshold = 0.0;
    std::vector<double> scores;
    std::vector<int> rejectedFrameIndices;
};

std::vector<ModernAtsTransientMarker> detectTransientMarkers(
    const std::vector<TransientFeatureFrame>& frames,
    const TransientDetectionConfig& config,
    TransientDetectionDiagnostics& diagnostics);

struct ExperimentalTransientAtom
{
    double centerSeconds = 0.0;
    double durationSeconds = 0.0;
    double gain = 1.0;
    std::vector<float> dctCoefficients;
};

struct ExperimentalTransientLane
{
    int sampleRate = 0;
    std::vector<ExperimentalTransientAtom> atoms;
};

ExperimentalTransientLane buildExperimentalTransientLane(const std::vector<ModernAtsTransientMarker>& markers,
                                                         int sampleRate,
                                                         int coeffsPerAtom = 16);
std::vector<float> synthesizeExperimentalTransientLane(const ExperimentalTransientLane& lane);

struct TransientLaneComparison
{
    int bufferTransientCount = 0;
    int laneTransientCount = 0;
    double bufferEnergy = 0.0;
    double laneEnergy = 0.0;
    double normalizedEnergyDelta = 0.0;
    std::string graduationDecision;
};

TransientLaneComparison compareTransientLanes(const std::vector<ModernAtsTransientBuffer>& preservationBuffers,
                                              const ExperimentalTransientLane& lane);

struct DdspHelperAssessment
{
    std::string helperKind;
    bool keepsClassicModelAsSourceOfTruth = true;
    bool introducesRuntimeDependency = false;
    std::vector<std::string> approvedUseCases;
    std::vector<std::string> blockedUseCases;
    std::string recommendation;
};

DdspHelperAssessment evaluateDdspHelperPolicy(const std::string& helperKind,
                                              const std::vector<std::string>& requestedUseCases,
                                              bool wouldRequireRuntimeModel);

} // namespace ats

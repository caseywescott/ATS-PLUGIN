#include "AtsAnalysisPipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>

extern "C" {
#include "atsa.h"
}

namespace ats
{

namespace
{

double clamp(double value, double lo, double hi)
{
    return std::max(lo, std::min(hi, value));
}

bool finite(double value) { return std::isfinite(value); }

double ampFromDb(double db) { return std::pow(10.0, db / 20.0); }

bool spectrumBinFinite(const SpectrumBin& bin)
{
    return finite(bin.frequencyHz) && finite(bin.magnitudeDb) && finite(bin.phaseRadians);
}

void freeResidual(double*** res)
{
    if (res == nullptr || *res == nullptr)
        return;
    std::free((*res)[0]);
    std::free((*res)[1]);
    std::free(*res);
    *res = nullptr;
}

ModernAtsPartialFrame sleptFrame()
{
    ModernAtsPartialFrame frame;
    frame.confidence = 0.0;
    frame.reason = ModernAtsFrameReason::Slept;
    return frame;
}

ModernAtsPartialFrame frameFromPeak(const TrackingPeak& peak, ModernAtsFrameReason reason, double confidence)
{
    ModernAtsPartialFrame frame;
    frame.amplitude = peak.amplitude;
    frame.frequencyHz = peak.frequencyHz;
    frame.phaseRadians = peak.phaseRadians;
    frame.smrDb = peak.smrDb;
    frame.confidence = clamp(confidence, 0.0, 1.0);
    frame.reason = reason;
    return frame;
}

bool containsFrame(const std::vector<int>& frames, int frameIndex)
{
    return std::find(frames.begin(), frames.end(), frameIndex) != frames.end();
}

struct Candidate
{
    int partialId = 0;
    int peakIndex = 0;
    ModernAtsAssignmentCost cost;
};

ModernAtsAssignmentCost assignmentCostFor(int frameIndex,
                                          int partialId,
                                          int peakIndex,
                                          const ModernAtsPartial& partial,
                                          const TrackingPeak& peak,
                                          const AssignmentTrackerConfig& config)
{
    ModernAtsAssignmentCost cost;
    cost.frameIndex = frameIndex;
    cost.partialId = partialId;
    cost.peakIndex = peak.sourceIndex;

    const auto& last = partial.frames.back();
    const bool hasPrevious = partial.frames.size() >= 2;
    const auto& previous = hasPrevious ? partial.frames[partial.frames.size() - 2] : last;
    const bool useSlope = config.analysisMode == ModernAtsAnalysisMode::ModernAts
                          && config.useFrequencySlopeContinuity && hasPrevious
                          && previous.reason != ModernAtsFrameReason::Slept;

    const double previousSlope = useSlope ? last.frequencyHz - previous.frequencyHz : 0.0;
    const double predictedFrequency = useSlope ? last.frequencyHz + previousSlope : last.frequencyHz;
    const double frequencyDenom = std::max(20.0, std::abs(predictedFrequency));
    cost.frequencyCost = config.frequencyWeight * std::abs(peak.frequencyHz - predictedFrequency) / frequencyDenom;

    const double ampDenom = std::max(1.0e-6, last.amplitude);
    cost.amplitudeCost = config.amplitudeWeight * std::abs(peak.amplitude - last.amplitude) / ampDenom;
    cost.smrCost = config.smrWeight * std::abs(peak.smrDb - last.smrDb) / 100.0;
    cost.birthDeathCost = config.birthPenalty + config.deathPenalty;
    cost.spuriousPeakCost = config.spuriousPeakPenalty;

    if (useSlope)
    {
        const double observedSlope = peak.frequencyHz - last.frequencyHz;
        cost.slopeCost = config.frequencySlopeWeight * std::abs(observedSlope - previousSlope)
                         / std::max(100.0, std::abs(previousSlope));
    }

    cost.totalCost = cost.frequencyCost + cost.amplitudeCost + cost.smrCost + cost.slopeCost;
    return cost;
}

} // namespace

const std::vector<AnalysisModeDescriptor>& analysisModeRegistry()
{
    static const std::vector<AnalysisModeDescriptor> modes = {
        { ModernAtsAnalysisMode::ClassicAts,
          ModernAtsTrackerMode::ClassicRecursiveFrequencySmr,
          "classic_ats",
          "Classic ATS",
          true,
          true },
        { ModernAtsAnalysisMode::ModernAts,
          ModernAtsTrackerMode::ModernAssignment,
          "modern_ats",
          "Modern ATS",
          false,
          true },
        { ModernAtsAnalysisMode::Experimental,
          ModernAtsTrackerMode::Experimental,
          "experimental",
          "Experimental",
          false,
          false },
    };
    return modes;
}

const AnalysisModeDescriptor* findAnalysisModeDescriptor(const std::string& id)
{
    const auto& modes = analysisModeRegistry();
    const auto it = std::find_if(modes.begin(), modes.end(), [&id](const AnalysisModeDescriptor& mode) {
        return id == mode.id;
    });
    return it == modes.end() ? nullptr : &*it;
}

const AnalysisModeDescriptor& defaultAnalysisModeDescriptor()
{
    const auto& modes = analysisModeRegistry();
    const auto it = std::find_if(modes.begin(), modes.end(), [](const AnalysisModeDescriptor& mode) {
        return mode.compatibilityDefault;
    });
    return it == modes.end() ? modes.front() : *it;
}

std::vector<SpectralPeak> ClassicStftPeakEstimator::estimatePeaks(
    const std::vector<SpectrumBin>& bins,
    const PeakEstimationConfig& config,
    PeakEstimationDiagnostics& diagnostics) const
{
    diagnostics = {};
    diagnostics.lowestFrequencyHz = config.lowestFrequencyHz;
    diagnostics.highestFrequencyHz = config.highestFrequencyHz;
    diagnostics.magnitudeThresholdDb = config.magnitudeThresholdDb;

    std::vector<SpectralPeak> peaks;
    if (bins.size() < 3)
        return peaks;

    for (std::size_t i = 1; i + 1 < bins.size(); ++i)
    {
        const auto& left = bins[i - 1];
        const auto& center = bins[i];
        const auto& right = bins[i + 1];

        if (!spectrumBinFinite(left) || !spectrumBinFinite(center) || !spectrumBinFinite(right))
            continue;

        if (center.magnitudeDb < left.magnitudeDb || center.magnitudeDb < right.magnitudeDb)
            continue;

        ++diagnostics.rawPeaks;

        if (center.frequencyHz < config.lowestFrequencyHz || center.frequencyHz > config.highestFrequencyHz)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("frequency_range");
            continue;
        }
        if (center.magnitudeDb < config.magnitudeThresholdDb)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("magnitude_threshold");
            continue;
        }

        double interpolatedFrequency = center.frequencyHz;
        double interpolatedMagnitude = center.magnitudeDb;
        if (config.interpolateParabolically)
        {
            const double denom = left.magnitudeDb - 2.0 * center.magnitudeDb + right.magnitudeDb;
            if (std::abs(denom) > 1.0e-12)
            {
                const double offset = clamp(0.5 * (left.magnitudeDb - right.magnitudeDb) / denom, -0.5, 0.5);
                const double binSpacing = 0.5 * (right.frequencyHz - left.frequencyHz);
                interpolatedFrequency = center.frequencyHz + offset * binSpacing;
                interpolatedMagnitude = center.magnitudeDb - 0.25 * (left.magnitudeDb - right.magnitudeDb) * offset;
            }
        }

        SpectralPeak peak;
        peak.binIndex = static_cast<int>(i);
        peak.frequencyHz = interpolatedFrequency;
        peak.amplitude = ampFromDb(interpolatedMagnitude);
        peak.phaseRadians = center.phaseRadians;
        peak.smrDb = interpolatedMagnitude;
        peak.magnitudeDb = interpolatedMagnitude;
        peaks.push_back(peak);
        ++diagnostics.acceptedPeaks;
    }

    return peaks;
}

std::vector<SpectralPeak> ExperimentalReassignedPeakEstimator::estimatePeaks(
    const std::vector<SpectrumBin>& bins,
    const PeakEstimationConfig& config,
    PeakEstimationDiagnostics& diagnostics) const
{
    diagnostics = {};
    diagnostics.lowestFrequencyHz = config.lowestFrequencyHz;
    diagnostics.highestFrequencyHz = config.highestFrequencyHz;
    diagnostics.magnitudeThresholdDb = config.magnitudeThresholdDb;

    std::vector<SpectralPeak> peaks;
    if (bins.size() < 5)
        return peaks;

    for (std::size_t i = 2; i + 2 < bins.size(); ++i)
    {
        const auto& c = bins[i];
        if (!spectrumBinFinite(bins[i - 1]) || !spectrumBinFinite(c) || !spectrumBinFinite(bins[i + 1]))
            continue;

        if (c.magnitudeDb < bins[i - 1].magnitudeDb || c.magnitudeDb < bins[i + 1].magnitudeDb)
            continue;
        ++diagnostics.rawPeaks;

        if (c.frequencyHz < config.lowestFrequencyHz || c.frequencyHz > config.highestFrequencyHz)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("frequency_range");
            continue;
        }
        if (c.magnitudeDb < config.magnitudeThresholdDb)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("magnitude_threshold");
            continue;
        }

        double reassignedOffset = 0.0;
        const double denom = bins[i - 1].magnitudeDb - 2.0 * c.magnitudeDb + bins[i + 1].magnitudeDb;
        if (std::abs(denom) > 1.0e-12)
        {
            reassignedOffset =
                clamp(0.5 * (bins[i - 1].magnitudeDb - bins[i + 1].magnitudeDb) / denom, -0.75, 0.75);
        }
        const double localSpacing = 0.5 * (bins[i + 1].frequencyHz - bins[i - 1].frequencyHz);

        SpectralPeak peak;
        peak.binIndex = static_cast<int>(i);
        peak.frequencyHz = c.frequencyHz + reassignedOffset * localSpacing;
        peak.magnitudeDb =
            c.magnitudeDb - 0.25 * (bins[i - 1].magnitudeDb - bins[i + 1].magnitudeDb) * reassignedOffset;
        peak.amplitude = ampFromDb(peak.magnitudeDb);
        peak.phaseRadians = c.phaseRadians;
        peak.smrDb = peak.magnitudeDb;
        peaks.push_back(peak);
        ++diagnostics.acceptedPeaks;
    }

    return peaks;
}

PeakEstimatorComparison comparePeakEstimators(const IPeakEstimator& baseline,
                                              const IPeakEstimator& candidate,
                                              const std::vector<SpectrumBin>& bins,
                                              const PeakEstimationConfig& config,
                                              double peakMatchToleranceHz)
{
    PeakEstimatorComparison comparison;
    comparison.baselineId = baseline.id();
    comparison.candidateId = candidate.id();

    PeakEstimationDiagnostics baselineDiag;
    PeakEstimationDiagnostics candidateDiag;
    const auto t0 = std::chrono::high_resolution_clock::now();
    const auto baselinePeaks = baseline.estimatePeaks(bins, config, baselineDiag);
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto candidatePeaks = candidate.estimatePeaks(bins, config, candidateDiag);
    const auto t2 = std::chrono::high_resolution_clock::now();

    comparison.baselineCpuMillis =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    comparison.candidateCpuMillis =
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    comparison.baselineAcceptedPeaks = static_cast<int>(baselinePeaks.size());
    comparison.candidateAcceptedPeaks = static_cast<int>(candidatePeaks.size());

    int matched = 0;
    double freqDeltaAcc = 0.0;
    std::vector<bool> candidateUsed(candidatePeaks.size(), false);
    for (const auto& basePeak : baselinePeaks)
    {
        double bestDelta = std::numeric_limits<double>::infinity();
        int bestIdx = -1;
        for (std::size_t i = 0; i < candidatePeaks.size(); ++i)
        {
            if (candidateUsed[i])
                continue;
            const double delta = std::abs(candidatePeaks[i].frequencyHz - basePeak.frequencyHz);
            if (delta < bestDelta)
            {
                bestDelta = delta;
                bestIdx = static_cast<int>(i);
            }
        }
        if (bestIdx >= 0 && bestDelta <= peakMatchToleranceHz)
        {
            candidateUsed[static_cast<std::size_t>(bestIdx)] = true;
            ++matched;
            freqDeltaAcc += bestDelta;
        }
    }

    const int denom = std::max(1, static_cast<int>(std::max(baselinePeaks.size(), candidatePeaks.size())));
    comparison.matchedPeakRatio = static_cast<double>(matched) / static_cast<double>(denom);
    comparison.meanAbsFrequencyDeltaHz = matched > 0 ? freqDeltaAcc / static_cast<double>(matched) : 0.0;
    comparison.recommendation = (comparison.matchedPeakRatio >= 0.80
                                 && comparison.candidateCpuMillis <= comparison.baselineCpuMillis * 1.5)
                                    ? "experimental_candidate_viable"
                                    : "keep_experimental_only";
    return comparison;
}

ClassicAtsAnalysisResult runClassicAtsAnalysis(const AnalysisParams& params,
                                               const std::vector<double>& monoSamples,
                                               int sampleRate)
{
    ClassicAtsAnalysisResult result;
    if (monoSamples.empty())
    {
        result.errorMessage = "No samples to analyze.";
        return result;
    }
    if (sampleRate <= 0)
    {
        result.errorMessage = "Invalid sample rate.";
        return result;
    }

    ANARGS an {};
    params.toAnargs(an);

    double** residual = nullptr;
    int residualLength = 0;
    auto* samples = const_cast<double*>(monoSamples.data());
    ATS_SOUND* raw = tracker(&an, samples, sampleRate, static_cast<int>(monoSamples.size()), &residual, &residualLength);
    freeResidual(&residual);

    if (raw == nullptr)
    {
        result.errorMessage = "Classic ATS tracker failed.";
        return result;
    }

    for (int partial = 0; partial < raw->partials; ++partial)
    {
        std::vector<bool> activity;
        activity.reserve(static_cast<std::size_t>(raw->frames));
        for (int frame = 0; frame < raw->frames; ++frame)
        {
            const bool active = raw->amp != nullptr && raw->frq != nullptr && raw->amp[partial] != nullptr
                                && raw->frq[partial] != nullptr && raw->amp[partial][frame] > 0.0
                                && raw->frq[partial][frame] > 0.0;
            activity.push_back(active);
        }
        auto lifecycle =
            normalizeTrackLifecycleFromActivity(partial, activity, params.min_gap_len, params.min_seg_len);
        result.trackLifecycle.insert(result.trackLifecycle.end(), lifecycle.begin(), lifecycle.end());
    }

    result.sound.reset(raw);
    return result;
}

std::vector<ModernAtsTrackLifecycleEvent> normalizeTrackLifecycleFromActivity(int partialId,
                                                                              const std::vector<bool>& activity,
                                                                              int shortGapFrames,
                                                                              int minSegmentFrames)
{
    return normalizeTrackLifecycleFromActivity(partialId, activity, shortGapFrames, minSegmentFrames, {}, true);
}

std::vector<ModernAtsTrackLifecycleEvent> normalizeTrackLifecycleFromActivity(
    int partialId,
    const std::vector<bool>& activity,
    int shortGapFrames,
    int minSegmentFrames,
    const std::vector<int>& protectedFrameIndices,
    bool allowGapFillAcrossProtectedFrames)
{
    std::vector<ModernAtsTrackLifecycleEvent> events;
    const int n = static_cast<int>(activity.size());
    int cursor = 0;

    while (cursor < n)
    {
        while (cursor < n && !activity[static_cast<std::size_t>(cursor)])
            ++cursor;
        if (cursor >= n)
            break;

        const int segmentStart = cursor;
        int segmentEnd = cursor;
        std::vector<int> gapFillFrames;

        while (segmentEnd + 1 < n)
        {
            int gapStart = segmentEnd + 1;
            while (gapStart < n && activity[static_cast<std::size_t>(gapStart)])
            {
                segmentEnd = gapStart;
                ++gapStart;
            }
            if (gapStart >= n)
                break;

            int gapEnd = gapStart;
            while (gapEnd < n && !activity[static_cast<std::size_t>(gapEnd)])
                ++gapEnd;
            if (gapEnd >= n)
                break;

            const int gapLength = gapEnd - gapStart;
            if (gapLength > shortGapFrames)
                break;
            bool crossesProtectedFrame = false;
            for (int frame = gapStart; frame < gapEnd; ++frame)
                crossesProtectedFrame = crossesProtectedFrame || containsFrame(protectedFrameIndices, frame);
            if (crossesProtectedFrame && !allowGapFillAcrossProtectedFrames)
                break;

            for (int frame = gapStart; frame < gapEnd; ++frame)
                gapFillFrames.push_back(frame);
            segmentEnd = gapEnd;
        }

        const int segmentLength = segmentEnd - segmentStart + 1;
        if (segmentLength < minSegmentFrames)
        {
            events.push_back({ partialId, segmentStart, ModernAtsLifecycleKind::Pruned, "short_segment" });
        }
        else
        {
            events.push_back({ partialId, segmentStart, ModernAtsLifecycleKind::Birth, "active_segment" });
            for (const int frame : gapFillFrames)
                events.push_back({ partialId, frame, ModernAtsLifecycleKind::GapFill, "short_gap" });
            events.push_back({ partialId, segmentEnd, ModernAtsLifecycleKind::Death, "segment_end" });
        }

        cursor = segmentEnd + 1;
    }

    return events;
}

AssignmentTrackerResult runAssignmentTrackerPrototype(const std::vector<TrackingPeakFrame>& frames,
                                                      const AssignmentTrackerConfig& config)
{
    AssignmentTrackerResult result;
    std::vector<int> activePartialIds;
    int nextPartialId = 0;

    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex)
    {
        const auto& framePeaks = frames[frameIndex];
        for (auto& partial : result.partials)
        {
            while (partial.frames.size() < frameIndex)
                partial.frames.push_back(sleptFrame());
        }

        const bool transientBreak = !config.allowInterpolationAcrossTransients
                                    && containsFrame(config.transientBreakFrameIndices,
                                                     static_cast<int>(frameIndex));
        if (transientBreak)
        {
            for (const int partialId : activePartialIds)
            {
                if (partialId < 0 || partialId >= static_cast<int>(result.partials.size()))
                    continue;
                auto& partial = result.partials[static_cast<std::size_t>(partialId)];
                if (partial.frames.size() == frameIndex)
                    partial.frames.push_back(sleptFrame());
                result.trackLifecycle.push_back(
                    { partialId, static_cast<int>(frameIndex), ModernAtsLifecycleKind::Death, "transient_break" });
            }
            activePartialIds.clear();
        }

        std::vector<bool> peakAssigned(framePeaks.size(), false);
        std::vector<bool> partialAssigned(result.partials.size(), false);
        std::vector<Candidate> candidates;

        for (const int partialId : activePartialIds)
        {
            if (partialId < 0 || partialId >= static_cast<int>(result.partials.size()))
                continue;

            std::vector<Candidate> perTrack;
            for (std::size_t peakIndex = 0; peakIndex < framePeaks.size(); ++peakIndex)
            {
                Candidate candidate;
                candidate.partialId = partialId;
                candidate.peakIndex = static_cast<int>(peakIndex);
                candidate.cost = assignmentCostFor(static_cast<int>(frameIndex),
                                                   partialId,
                                                   static_cast<int>(peakIndex),
                                                   result.partials[static_cast<std::size_t>(partialId)],
                                                   framePeaks[peakIndex],
                                                   config);
                perTrack.push_back(candidate);
            }

            std::sort(perTrack.begin(), perTrack.end(), [](const Candidate& a, const Candidate& b) {
                return a.cost.totalCost < b.cost.totalCost;
            });
            if (perTrack.size() > config.maxCandidatesPerTrack)
                perTrack.resize(config.maxCandidatesPerTrack);
            candidates.insert(candidates.end(), perTrack.begin(), perTrack.end());
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.cost.totalCost == b.cost.totalCost)
                return a.partialId < b.partialId;
            return a.cost.totalCost < b.cost.totalCost;
        });

        std::vector<int> nextActivePartialIds;
        for (const auto& candidate : candidates)
        {
            const double alternativeCost = std::max(0.0, candidate.cost.birthDeathCost)
                                           + std::max(0.0, candidate.cost.spuriousPeakCost);
            const double maxAllowedCost = std::min(config.maxMatchCost, alternativeCost);
            if (candidate.cost.totalCost > maxAllowedCost)
                continue;
            if (partialAssigned[static_cast<std::size_t>(candidate.partialId)]
                || peakAssigned[static_cast<std::size_t>(candidate.peakIndex)])
                continue;

            auto& partial = result.partials[static_cast<std::size_t>(candidate.partialId)];
            partial.frames.push_back(frameFromPeak(framePeaks[static_cast<std::size_t>(candidate.peakIndex)],
                                                   ModernAtsFrameReason::Matched,
                                                   1.0 - clamp(candidate.cost.totalCost, 0.0, 1.0)));
            partialAssigned[static_cast<std::size_t>(candidate.partialId)] = true;
            peakAssigned[static_cast<std::size_t>(candidate.peakIndex)] = true;
            nextActivePartialIds.push_back(candidate.partialId);
            result.assignmentCosts.push_back(candidate.cost);
        }

        for (const int partialId : activePartialIds)
        {
            if (partialId < 0 || partialId >= static_cast<int>(result.partials.size()))
                continue;
            if (partialAssigned[static_cast<std::size_t>(partialId)])
                continue;
            result.partials[static_cast<std::size_t>(partialId)].frames.push_back(sleptFrame());
            result.trackLifecycle.push_back(
                { partialId, static_cast<int>(frameIndex), ModernAtsLifecycleKind::Death, "unmatched_track" });
        }

        for (std::size_t peakIndex = 0; peakIndex < framePeaks.size(); ++peakIndex)
        {
            if (peakAssigned[peakIndex])
                continue;

            ModernAtsPartial partial;
            partial.id = nextPartialId++;
            partial.label = "track_" + std::to_string(partial.id);
            partial.frames.resize(frameIndex, sleptFrame());
            partial.frames.push_back(frameFromPeak(framePeaks[peakIndex], ModernAtsFrameReason::Born, 1.0));
            result.partials.push_back(partial);
            nextActivePartialIds.push_back(partial.id);
            result.trackLifecycle.push_back(
                { partial.id, static_cast<int>(frameIndex), ModernAtsLifecycleKind::Birth, "unmatched_peak" });
        }

        for (auto& partial : result.partials)
        {
            if (partial.frames.size() == frameIndex)
                partial.frames.push_back(sleptFrame());
        }

        activePartialIds = nextActivePartialIds;
    }

    for (auto& partial : result.partials)
    {
        while (partial.frames.size() < frames.size())
            partial.frames.push_back(sleptFrame());
    }

    return result;
}

std::vector<double> computeOrphanResidualBandEnergy(const std::vector<double>& totalBandEnergy,
                                                    const std::vector<double>& assignedBandEnergy)
{
    const std::size_t n = std::max(totalBandEnergy.size(), assignedBandEnergy.size());
    std::vector<double> orphan(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double total = i < totalBandEnergy.size() ? totalBandEnergy[i] : 0.0;
        const double assigned = i < assignedBandEnergy.size() ? assignedBandEnergy[i] : 0.0;
        orphan[i] = std::max(0.0, total - assigned);
    }
    return orphan;
}

std::vector<float> renderOrphanResidualBandNoise(const std::vector<std::vector<double>>& orphanBandEnergyByFrame,
                                                 int hopSizeSamples,
                                                 float gain,
                                                 std::uint32_t seed)
{
    if (hopSizeSamples <= 0 || orphanBandEnergyByFrame.empty() || gain <= 0.0f)
        return {};
    if (!std::isfinite(gain))
        return {};

    std::vector<float> output(orphanBandEnergyByFrame.size() * static_cast<std::size_t>(hopSizeSamples), 0.0f);
    std::uint32_t state = seed == 0 ? 1u : seed;

    for (std::size_t frameIndex = 0; frameIndex < orphanBandEnergyByFrame.size(); ++frameIndex)
    {
        double energy = 0.0;
        for (const double bandEnergy : orphanBandEnergyByFrame[frameIndex])
        {
            if (std::isfinite(bandEnergy))
                energy += std::max(0.0, bandEnergy);
        }
        const float amplitude = gain * static_cast<float>(std::sqrt(std::max(0.0, energy)));
        for (int i = 0; i < hopSizeSamples; ++i)
        {
            state = state * 1664525u + 1013904223u;
            const float noise = (static_cast<float>((state >> 8) & 0x00ffffffu) / 8388607.5f) - 1.0f;
            output[frameIndex * static_cast<std::size_t>(hopSizeSamples) + static_cast<std::size_t>(i)] =
                amplitude * noise;
        }
    }

    return output;
}

std::vector<ModernAtsTransientMarker> detectTransientMarkers(
    const std::vector<TransientFeatureFrame>& frames,
    const TransientDetectionConfig& config,
    TransientDetectionDiagnostics& diagnostics)
{
    diagnostics = {};
    std::vector<ModernAtsTransientMarker> markers;
    if (frames.empty())
        return markers;

    diagnostics.scores.resize(frames.size(), 0.0);
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const double previousEnergy = i == 0 ? frames[i].energy : frames[i - 1].energy;
        const double energyRise = std::max(0.0, frames[i].energy - previousEnergy)
                                  / std::max(1.0e-9, previousEnergy);
        diagnostics.scores[i] = config.spectralFluxWeight * std::max(0.0, frames[i].spectralFlux)
                                + config.energyRiseWeight * energyRise
                                + config.modelDiscontinuityWeight
                                      * std::max(0.0, frames[i].modelDiscontinuity);
    }

    const double mean = std::accumulate(diagnostics.scores.begin(), diagnostics.scores.end(), 0.0)
                        / static_cast<double>(diagnostics.scores.size());
    double variance = 0.0;
    for (const double score : diagnostics.scores)
        variance += (score - mean) * (score - mean);
    variance /= static_cast<double>(diagnostics.scores.size());
    diagnostics.threshold = std::max(config.minimumScore,
                                     mean + std::sqrt(variance) * config.thresholdStdDevMultiplier);

    double lastMarkerTime = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const double score = diagnostics.scores[i];
        const double left = i > 0 ? diagnostics.scores[i - 1] : -std::numeric_limits<double>::infinity();
        const double right =
            i + 1 < diagnostics.scores.size() ? diagnostics.scores[i + 1] : -std::numeric_limits<double>::infinity();
        const bool localPeak = score >= left && score >= right;
        const bool farEnough = frames[i].timeSeconds - lastMarkerTime >= config.minimumGapSeconds;

        if (score >= diagnostics.threshold && localPeak && farEnough)
        {
            ModernAtsTransientMarker marker;
            marker.frameIndex = static_cast<int>(i);
            marker.onsetSeconds = frames[i].timeSeconds;
            marker.windowStartSeconds = std::max(0.0, marker.onsetSeconds - config.preWindowSeconds);
            marker.windowEndSeconds = marker.onsetSeconds + config.postWindowSeconds;
            marker.gain = config.markerGain;
            markers.push_back(marker);
            lastMarkerTime = marker.onsetSeconds;
        }
        else if (score > 0.0)
        {
            diagnostics.rejectedFrameIndices.push_back(static_cast<int>(i));
        }
    }

    return markers;
}

ExperimentalTransientLane buildExperimentalTransientLane(const std::vector<ModernAtsTransientMarker>& markers,
                                                         int sampleRate,
                                                         int coeffsPerAtom)
{
    ExperimentalTransientLane lane;
    lane.sampleRate = sampleRate;
    if (sampleRate <= 0 || coeffsPerAtom <= 0)
        return lane;

    for (const auto& marker : markers)
    {
        ExperimentalTransientAtom atom;
        atom.centerSeconds = marker.onsetSeconds;
        atom.durationSeconds = std::max(0.001, marker.windowEndSeconds - marker.windowStartSeconds);
        atom.gain = marker.gain;
        atom.dctCoefficients.resize(static_cast<std::size_t>(coeffsPerAtom), 0.0f);
        for (int i = 0; i < coeffsPerAtom; ++i)
        {
            const double falloff = std::exp(-0.20 * static_cast<double>(i));
            atom.dctCoefficients[static_cast<std::size_t>(i)] =
                static_cast<float>(atom.gain * falloff * std::cos(0.35 * static_cast<double>(i)));
        }
        lane.atoms.push_back(std::move(atom));
    }
    return lane;
}

std::vector<float> synthesizeExperimentalTransientLane(const ExperimentalTransientLane& lane)
{
    if (lane.sampleRate <= 0 || lane.atoms.empty())
        return {};

    std::size_t totalSamples = 0;
    for (const auto& atom : lane.atoms)
    {
        const std::size_t atomEnd = static_cast<std::size_t>(
            std::max(0.0, atom.centerSeconds + atom.durationSeconds) * static_cast<double>(lane.sampleRate));
        totalSamples = std::max(totalSamples, atomEnd + 1);
    }
    if (totalSamples == 0)
        return {};

    std::vector<float> output(totalSamples, 0.0f);
    const double pi = std::acos(-1.0);
    for (const auto& atom : lane.atoms)
    {
        const std::size_t start = static_cast<std::size_t>(
            std::max(0.0, atom.centerSeconds - 0.5 * atom.durationSeconds) * static_cast<double>(lane.sampleRate));
        const std::size_t len = std::max<std::size_t>(1, static_cast<std::size_t>(
            atom.durationSeconds * static_cast<double>(lane.sampleRate)));
        for (std::size_t n = 0; n < len && (start + n) < output.size(); ++n)
        {
            const double x = static_cast<double>(n) / static_cast<double>(len);
            double v = 0.0;
            for (std::size_t k = 0; k < atom.dctCoefficients.size(); ++k)
            {
                v += static_cast<double>(atom.dctCoefficients[k]) * std::cos(pi * (k + 0.5) * x);
            }
            const double env = std::sin(pi * x);
            output[start + n] += static_cast<float>(v * env);
        }
    }
    return output;
}

TransientLaneComparison compareTransientLanes(const std::vector<ModernAtsTransientBuffer>& preservationBuffers,
                                              const ExperimentalTransientLane& lane)
{
    TransientLaneComparison comparison;
    comparison.bufferTransientCount = static_cast<int>(preservationBuffers.size());
    comparison.laneTransientCount = static_cast<int>(lane.atoms.size());

    for (const auto& buf : preservationBuffers)
    {
        for (const float s : buf.samples)
            comparison.bufferEnergy += static_cast<double>(s) * static_cast<double>(s);
    }
    const auto laneRender = synthesizeExperimentalTransientLane(lane);
    for (const float s : laneRender)
        comparison.laneEnergy += static_cast<double>(s) * static_cast<double>(s);

    const double norm = std::max(1.0e-9, comparison.bufferEnergy);
    comparison.normalizedEnergyDelta = std::abs(comparison.laneEnergy - comparison.bufferEnergy) / norm;
    comparison.graduationDecision = comparison.normalizedEnergyDelta <= 0.25
                                        ? "candidate_for_modern_mode"
                                        : "keep_as_experimental";
    return comparison;
}

DdspHelperAssessment evaluateDdspHelperPolicy(const std::string& helperKind,
                                              const std::vector<std::string>& requestedUseCases,
                                              bool wouldRequireRuntimeModel)
{
    DdspHelperAssessment assessment;
    assessment.helperKind = helperKind;
    assessment.introducesRuntimeDependency = wouldRequireRuntimeModel;
    assessment.approvedUseCases = { "parameter_estimation", "analysis_denoising", "preset_suggestion" };
    assessment.blockedUseCases = { "runtime_core_representation", "replacing_classical_model" };

    for (const auto& useCase : requestedUseCases)
    {
        const bool approved = std::find(assessment.approvedUseCases.begin(),
                                        assessment.approvedUseCases.end(),
                                        useCase) != assessment.approvedUseCases.end();
        if (!approved)
            assessment.keepsClassicModelAsSourceOfTruth = false;
    }

    assessment.recommendation =
        (!assessment.keepsClassicModelAsSourceOfTruth || wouldRequireRuntimeModel)
            ? "defer_ddsp_helper_until_licensing_size_determinism_are_resolved"
            : "allow_offline_assistive_ddsp_helper_only";
    return assessment;
}

} // namespace ats

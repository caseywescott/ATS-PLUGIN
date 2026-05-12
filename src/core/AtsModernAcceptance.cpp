#include "AtsModernAcceptance.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace ats
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

bool isActiveFrame(const ModernAtsPartialFrame& frame)
{
    return frame.amplitude > 0.0 && frame.frequencyHz > 0.0
           && frame.reason != ModernAtsFrameReason::Slept
           && frame.reason != ModernAtsFrameReason::Pruned
           && frame.reason != ModernAtsFrameReason::Rejected;
}

double finitePositive(double value)
{
    return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}

double rmsOf(const std::vector<float>& samples)
{
    if (samples.empty())
        return 0.0;
    double sum = 0.0;
    for (const float sample : samples)
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

double dftMagnitudeAt(const std::vector<float>& samples, int bin, int bins)
{
    if (samples.empty() || bins <= 0)
        return 0.0;

    double re = 0.0;
    double im = 0.0;
    const double scale = -2.0 * kPi * static_cast<double>(bin) / static_cast<double>(bins);
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const double phase = scale * static_cast<double>(i);
        re += static_cast<double>(samples[i]) * std::cos(phase);
        im += static_cast<double>(samples[i]) * std::sin(phase);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(samples.size());
}

const ModernAtsPartial* findPartial(const ModernAtsModel& model, int partialId)
{
    for (const auto& partial : model.partials)
    {
        if (partial.id == partialId)
            return &partial;
    }
    return nullptr;
}

void addCheck(ModernAtsAcceptanceReport& report,
              const std::string& id,
              bool passed,
              double observed,
              double threshold,
              const std::string& detail)
{
    report.checks.push_back({ id, passed, observed, threshold, detail });
}

bool renderSafetyPassed(const ModernAtsRealtimeSafetyReport& safety)
{
    return safety.renderIntoCallerOwnedBuffer && !safety.allocatesDuringRender
           && !safety.locksDuringRender && !safety.performsFileIoDuringRender
           && !safety.performsAnalysisDuringRender;
}

bool hasGapFillForSleep(const std::vector<ModernAtsTrackLifecycleEvent>& events,
                        const ModernAtsTrackLifecycleEvent& sleepEvent)
{
    for (const auto& event : events)
    {
        if (event.partialId == sleepEvent.partialId && event.frameIndex == sleepEvent.frameIndex
            && event.kind == ModernAtsLifecycleKind::GapFill)
        {
            return true;
        }
    }
    return false;
}

} // namespace

bool ModernAtsAcceptanceReport::passed() const
{
    // An empty report (no gates evaluated) is not a failure: all evaluated checks passed.
    for (const auto& check : checks)
    {
        if (!check.passed)
            return false;
    }
    return true;
}

std::vector<std::string> ModernAtsAcceptanceReport::failureMessages() const
{
    std::vector<std::string> failures;
    for (const auto& check : checks)
    {
        if (check.passed)
            continue;
        std::ostringstream out;
        out << check.id << " observed=" << check.observed << " threshold=" << check.threshold;
        if (!check.detail.empty())
            out << " " << check.detail;
        failures.push_back(out.str());
    }
    return failures;
}

ModernAtsAudioMetrics computeModernAtsAudioMetrics(const std::vector<float>& reference,
                                                   const std::vector<float>& candidate)
{
    ModernAtsAudioMetrics metrics;
    metrics.referenceRms = rmsOf(reference);
    metrics.candidateRms = rmsOf(candidate);

    const std::size_t n = std::max(reference.size(), candidate.size());
    if (n == 0)
        return metrics;

    double errorSum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double a = i < reference.size() ? reference[i] : 0.0;
        const double b = i < candidate.size() ? candidate[i] : 0.0;
        const double error = b - a;
        errorSum += error * error;
        metrics.peakError = std::max(metrics.peakError, std::abs(error));
    }
    metrics.rmsError = std::sqrt(errorSum / static_cast<double>(n));
    metrics.normalizedRmsError = metrics.rmsError / std::max(1.0e-12, metrics.referenceRms);

    const int spectralBins = 16;
    const std::size_t spectralSamples = std::min<std::size_t>(n, 2048);
    std::vector<float> referenceWindow(spectralSamples, 0.0f);
    std::vector<float> candidateWindow(spectralSamples, 0.0f);
    for (std::size_t i = 0; i < spectralSamples; ++i)
    {
        referenceWindow[i] = i < reference.size() ? reference[i] : 0.0f;
        candidateWindow[i] = i < candidate.size() ? candidate[i] : 0.0f;
    }

    double spectralDistance = 0.0;
    for (int bin = 1; bin <= spectralBins; ++bin)
    {
        const double refMag = dftMagnitudeAt(referenceWindow, bin, spectralSamples);
        const double candMag = dftMagnitudeAt(candidateWindow, bin, spectralSamples);
        const double refDb = 20.0 * std::log10(std::max(1.0e-12, refMag));
        const double candDb = 20.0 * std::log10(std::max(1.0e-12, candMag));
        spectralDistance += std::abs(candDb - refDb);
    }
    metrics.spectralDistanceDb = spectralDistance / static_cast<double>(spectralBins);
    return metrics;
}

ModernAtsContinuityMetrics computeModernAtsContinuityMetrics(const ModernAtsModel& model)
{
    ModernAtsContinuityMetrics metrics;
    metrics.partialCount = static_cast<int>(model.partials.size());

    double confidenceSum = 0.0;
    for (const auto& partial : model.partials)
    {
        metrics.expectedFrameCount += static_cast<int>(partial.frames.size());
        for (const auto& frame : partial.frames)
        {
            if (isActiveFrame(frame))
            {
                ++metrics.activeFrameCount;
                confidenceSum += std::max(0.0, std::min(1.0, frame.confidence));
            }
            if (frame.reason == ModernAtsFrameReason::Interpolated)
                ++metrics.interpolatedFrameCount;
            if (frame.reason == ModernAtsFrameReason::Rejected || frame.reason == ModernAtsFrameReason::Pruned)
                ++metrics.rejectedFrameCount;
        }
    }

    for (const auto& event : model.trackLifecycle)
    {
        if (event.kind == ModernAtsLifecycleKind::Sleep && !hasGapFillForSleep(model.trackLifecycle, event))
            ++metrics.lifecycleBreakCount;
        if (event.kind == ModernAtsLifecycleKind::Death || event.kind == ModernAtsLifecycleKind::Pruned)
            ++metrics.lifecycleBreakCount;
    }

    const int denominator = std::max(1, metrics.expectedFrameCount + metrics.lifecycleBreakCount);
    metrics.continuityScore = static_cast<double>(metrics.activeFrameCount)
                              / static_cast<double>(denominator);
    metrics.meanConfidence = metrics.activeFrameCount > 0
                                 ? confidenceSum / static_cast<double>(metrics.activeFrameCount)
                                 : 0.0;
    return metrics;
}

ModernAtsResidualMetrics computeModernAtsResidualMetrics(const ModernAtsModel& model)
{
    ModernAtsResidualMetrics metrics;
    for (const auto& partial : model.partials)
    {
        for (const auto& frame : partial.frames)
            metrics.partialNoiseEnergy += finitePositive(frame.noiseEnergy);
    }
    for (const auto& frame : model.frames)
    {
        for (const double energy : frame.orphanResidualBandEnergy)
            metrics.orphanResidualEnergy += finitePositive(energy);
    }
    metrics.totalResidualEnergy = metrics.partialNoiseEnergy + metrics.orphanResidualEnergy;
    metrics.orphanResidualRatio =
        metrics.totalResidualEnergy > 0.0 ? metrics.orphanResidualEnergy / metrics.totalResidualEnergy : 0.0;
    return metrics;
}

ModernAtsTransientMetrics computeModernAtsTransientMetrics(const ModernAtsModel& model)
{
    ModernAtsTransientMetrics metrics;
    metrics.markerCount = static_cast<int>(model.transients.size());
    metrics.bufferCount = static_cast<int>(model.transientBuffers.size());
    for (const auto& marker : model.transients)
        metrics.totalWindowSeconds += std::max(0.0, marker.windowEndSeconds - marker.windowStartSeconds);

    double peakAbs = 0.0;
    int sampleCount = 0;
    for (const auto& buffer : model.transientBuffers)
    {
        for (std::size_t i = 0; i < buffer.samples.size(); ++i)
        {
            const double sample = buffer.samples[i];
            metrics.bufferEnergy += sample * sample;
            ++sampleCount;
            peakAbs = std::max(peakAbs, std::abs(sample));
            if (i > 0)
                metrics.maxBufferStep =
                    std::max(metrics.maxBufferStep,
                             std::abs(static_cast<double>(buffer.samples[i]) - static_cast<double>(buffer.samples[i - 1])));
        }
    }
    const double rms = metrics.bufferEnergy > 0.0
                           ? std::sqrt(metrics.bufferEnergy
                                       / static_cast<double>(std::max(1, sampleCount)))
                           : 0.0;
    metrics.attackSharpness = rms > 0.0 ? peakAbs / rms : 0.0;
    return metrics;
}

ModernAtsTrackerFixtureMetrics computeModernAtsTrackerFixtureMetrics(
    const ModernAtsModel& model,
    const std::string& fixtureId,
    const std::vector<ModernAtsTrackerReference>& references)
{
    ModernAtsTrackerFixtureMetrics metrics;
    metrics.fixtureId = fixtureId;
    metrics.continuity = computeModernAtsContinuityMetrics(model);

    double frequencyError = 0.0;
    int comparedFrames = 0;
    for (const auto& reference : references)
    {
        const auto* partial = findPartial(model, reference.partialId);
        if (partial == nullptr)
        {
            metrics.identityErrors += static_cast<int>(reference.expectedFrequencyHz.size());
            continue;
        }

        for (std::size_t frameIndex = 0; frameIndex < reference.expectedFrequencyHz.size(); ++frameIndex)
        {
            if (frameIndex >= partial->frames.size())
            {
                ++metrics.identityErrors;
                continue;
            }
            const auto& frame = partial->frames[frameIndex];
            if (!isActiveFrame(frame))
            {
                ++metrics.identityErrors;
                continue;
            }
            const double error = std::abs(frame.frequencyHz - reference.expectedFrequencyHz[frameIndex]);
            frequencyError += error;
            ++comparedFrames;
            if (error > reference.toleranceHz)
                ++metrics.identityErrors;
        }
    }

    metrics.meanAbsFrequencyErrorHz =
        comparedFrames > 0 ? frequencyError / static_cast<double>(comparedFrames) : 0.0;
    return metrics;
}

ModernAtsAcceptanceReport evaluateModernAtsAcceptanceGates(
    const ModernAtsAcceptanceInputs& inputs,
    const ModernAtsAcceptanceThresholds& thresholds)
{
    ModernAtsAcceptanceReport report;

    addCheck(report,
             "classic.normalized_rms_error",
             inputs.classicParityAudio.normalizedRmsError <= thresholds.maxClassicNormalizedRmsError,
             inputs.classicParityAudio.normalizedRmsError,
             thresholds.maxClassicNormalizedRmsError,
             "classic render should remain inside parity tolerance");
    addCheck(report,
             "classic.spectral_distance_db",
             inputs.classicParityAudio.spectralDistanceDb <= thresholds.maxClassicSpectralDistanceDb,
             inputs.classicParityAudio.spectralDistanceDb,
             thresholds.maxClassicSpectralDistanceDb,
             "classic render spectral distance should remain inside parity tolerance");

    const int crossingLimit = inputs.classicCrossingTracker.identityErrors
                              + thresholds.maxCrossingIdentityErrorsOverClassic;
    addCheck(report,
             "modern.crossing_identity_errors",
             inputs.modernCrossingTracker.identityErrors <= crossingLimit,
             static_cast<double>(inputs.modernCrossingTracker.identityErrors),
             static_cast<double>(crossingLimit),
             "modern tracker should improve or match crossing/close-partial identity");
    addCheck(report,
             "modern.crossing_continuity",
             inputs.modernCrossingTracker.continuity.continuityScore
                 >= inputs.classicCrossingTracker.continuity.continuityScore
                        - thresholds.maxSustainedContinuityRegression,
             inputs.modernCrossingTracker.continuity.continuityScore,
             inputs.classicCrossingTracker.continuity.continuityScore,
             "modern crossing continuity should not materially regress");

    const int sustainedLimit = inputs.classicSustainedTracker.identityErrors
                               + thresholds.maxSustainedIdentityErrorsOverClassic;
    addCheck(report,
             "modern.sustained_identity_errors",
             inputs.modernSustainedTracker.identityErrors <= sustainedLimit,
             static_cast<double>(inputs.modernSustainedTracker.identityErrors),
             static_cast<double>(sustainedLimit),
             "modern tracker should not worsen sustained fixture identity");
    addCheck(report,
             "modern.sustained_continuity",
             inputs.modernSustainedTracker.continuity.continuityScore
                 >= inputs.classicSustainedTracker.continuity.continuityScore
                        - thresholds.maxSustainedContinuityRegression,
             inputs.modernSustainedTracker.continuity.continuityScore,
             inputs.classicSustainedTracker.continuity.continuityScore,
             "modern sustained continuity should not materially regress");

    addCheck(report,
             "transient.marker_count",
             inputs.protectedTransient.markerCount > inputs.baselineTransient.markerCount,
             static_cast<double>(inputs.protectedTransient.markerCount),
             static_cast<double>(inputs.baselineTransient.markerCount + 1),
             "protected transient path should add onset markers");
    addCheck(report,
             "transient.attack_sharpness",
             inputs.protectedTransient.attackSharpness
                 >= inputs.baselineTransient.attackSharpness
                        + thresholds.minTransientSharpnessImprovement,
             inputs.protectedTransient.attackSharpness,
             inputs.baselineTransient.attackSharpness + thresholds.minTransientSharpnessImprovement,
             "protected transient path should preserve sharper attacks");
    addCheck(report,
             "transient.click_step",
             inputs.protectedTransient.maxBufferStep <= thresholds.maxTransientClickStep,
             inputs.protectedTransient.maxBufferStep,
             thresholds.maxTransientClickStep,
             "transient buffers should stay inside click guardrail");

    const double residualRatio = inputs.expectedResidualEnergy > 0.0
                                     ? inputs.residualMetrics.totalResidualEnergy
                                           / inputs.expectedResidualEnergy
                                     : 1.0;
    addCheck(report,
             "residual.energy_preservation",
             residualRatio >= thresholds.minResidualEnergyRatio,
             residualRatio,
             thresholds.minResidualEnergyRatio,
             "partial plus orphan residual energy should be preserved");

    addCheck(report,
             "morph.recipe_recall",
             inputs.morphRecipeRecallDeterministic,
             inputs.morphRecipeRecallDeterministic ? 1.0 : 0.0,
             1.0,
             "morph recipe serialization should recall deterministically");
    addCheck(report,
             "morph.render_determinism",
             inputs.morphRenderDeterministic,
             inputs.morphRenderDeterministic ? 1.0 : 0.0,
             1.0,
             "morph renders should be deterministic under fixed seeds");

    addCheck(report,
             "synth.realtime_safety",
             renderSafetyPassed(inputs.realtimeSafety),
             renderSafetyPassed(inputs.realtimeSafety) ? 1.0 : 0.0,
             1.0,
             inputs.realtimeSafety.summary);
    addCheck(report,
             "synth.finite_samples",
             inputs.renderSamplesFinite,
             inputs.renderSamplesFinite ? 1.0 : 0.0,
             1.0,
             "synthesis should not emit denormal or non-finite samples");
    addCheck(report,
             "synth.cpu_budget",
             inputs.renderStats.cpuRatioAtRealtime <= thresholds.maxRenderCpuRatio,
             inputs.renderStats.cpuRatioAtRealtime,
             thresholds.maxRenderCpuRatio,
             "modern synthesis should stay inside the configured CPU budget");

    return report;
}

// ---------------------------------------------------------------------------
// External fixture support (Phase 7)
// ---------------------------------------------------------------------------

namespace
{

// Minimal flat-JSON value extractor for our known summary schema.
// Handles: "key": number and "key": "string" patterns (one key per line,
// no nested objects).  We control the format via generate_atsa_references.py.

static std::string jsonStringValue(const std::string& json, const std::string& key)
{
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return {};
    auto afterColon = json.find_first_not_of(" \t", pos + search.size());
    if (afterColon == std::string::npos || json[afterColon] != '"')
        return {};
    const auto start = afterColon + 1;
    const auto end = json.find('"', start);
    if (end == std::string::npos)
        return {};
    return json.substr(start, end - start);
}

static double jsonDoubleValue(const std::string& json, const std::string& key, double defaultVal)
{
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return defaultVal;
    auto afterColon = json.find_first_not_of(" \t", pos + search.size());
    if (afterColon == std::string::npos)
        return defaultVal;
    try
    {
        return std::stod(json.substr(afterColon));
    }
    catch (...)
    {
        return defaultVal;
    }
}

static int jsonIntValue(const std::string& json, const std::string& key, int defaultVal)
{
    const double d = jsonDoubleValue(json, key, static_cast<double>(defaultVal));
    return static_cast<int>(d);
}

} // namespace (internal)

bool parseExternalFixtureGate(const std::string& name, ExternalFixtureGate& out)
{
    if (name == "classic_parity")
    {
        out = ExternalFixtureGate::ClassicParity;
        return true;
    }
    if (name == "tracker_continuity")
    {
        out = ExternalFixtureGate::TrackerContinuity;
        return true;
    }
    if (name == "residual_energy_preservation")
    {
        out = ExternalFixtureGate::ResidualEnergyPreservation;
        return true;
    }
    if (name == "transient_preservation")
    {
        out = ExternalFixtureGate::TransientPreservation;
        return true;
    }
    return false;
}

bool loadExternalFixtureReferenceMetrics(const std::string& path,
                                         ExternalFixtureReferenceMetrics& out,
                                         std::string& errorOut)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        errorOut = "cannot open: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string json = ss.str();

    if (json.find("\"schema_version\"") == std::string::npos)
    {
        errorOut = "missing schema_version field in: " + path;
        return false;
    }

    out.fixtureId = jsonStringValue(json, "fixture_id");
    out.normalizedRmsError = jsonDoubleValue(json, "normalized_rms_error", 0.0);
    out.spectralDistanceDb = jsonDoubleValue(json, "spectral_distance_db", 0.0);
    out.continuityScore = jsonDoubleValue(json, "continuity_score", -1.0);
    out.identityErrors = jsonIntValue(json, "identity_errors", -1);
    out.orphanResidualRatio = jsonDoubleValue(json, "orphan_residual_ratio", -1.0);
    out.attackSharpness = jsonDoubleValue(json, "attack_sharpness", -1.0);
    out.maxBufferStep = jsonDoubleValue(json, "max_buffer_step", -1.0);
    return true;
}

ModernAtsAcceptanceReport evaluateExternalFixtureGates(
    const ExternalFixtureReferenceMetrics& our,
    const ExternalFixtureReferenceMetrics& reference,
    const std::vector<ExternalFixtureGate>& enabledGates,
    const ExternalFixtureThresholds& thresholds)
{
    ModernAtsAcceptanceReport report;
    const std::string& id = our.fixtureId.empty() ? reference.fixtureId : our.fixtureId;

    for (const auto gate : enabledGates)
    {
        switch (gate)
        {
        case ExternalFixtureGate::ClassicParity:
        {
            const double nrmsDelta = std::abs(our.normalizedRmsError - reference.normalizedRmsError);
            addCheck(report,
                     id + ".classic_parity.normalized_rms_error",
                     nrmsDelta <= thresholds.maxNormalizedRmsErrorDelta,
                     nrmsDelta,
                     thresholds.maxNormalizedRmsErrorDelta,
                     "normalized RMS error delta vs ATSA reference (fixture=" + id + ")");

            const double specDelta = std::abs(our.spectralDistanceDb - reference.spectralDistanceDb);
            addCheck(report,
                     id + ".classic_parity.spectral_distance_db",
                     specDelta <= thresholds.maxSpectralDistanceDbDelta,
                     specDelta,
                     thresholds.maxSpectralDistanceDbDelta,
                     "spectral distance delta vs ATSA reference (fixture=" + id + ")");
            break;
        }

        case ExternalFixtureGate::TrackerContinuity:
        {
            if (our.continuityScore >= 0.0 && reference.continuityScore >= 0.0)
            {
                const double regression = reference.continuityScore - our.continuityScore;
                addCheck(report,
                         id + ".tracker_continuity.continuity_score",
                         regression <= thresholds.maxContinuityRegression,
                         our.continuityScore,
                         reference.continuityScore - thresholds.maxContinuityRegression,
                         "tracker continuity regression vs ATSA reference (fixture=" + id + ")");
            }
            if (our.identityErrors >= 0 && reference.identityErrors >= 0)
            {
                const int increase = our.identityErrors - reference.identityErrors;
                addCheck(report,
                         id + ".tracker_continuity.identity_errors",
                         increase <= thresholds.maxIdentityErrorIncrease,
                         static_cast<double>(our.identityErrors),
                         static_cast<double>(reference.identityErrors
                                             + thresholds.maxIdentityErrorIncrease),
                         "tracker identity error increase vs ATSA reference (fixture=" + id + ")");
            }
            break;
        }

        case ExternalFixtureGate::ResidualEnergyPreservation:
        {
            if (our.orphanResidualRatio >= 0.0 && reference.orphanResidualRatio >= 0.0)
            {
                const double delta =
                    std::abs(our.orphanResidualRatio - reference.orphanResidualRatio);
                addCheck(report,
                         id + ".residual_energy_preservation.orphan_residual_ratio",
                         delta <= thresholds.maxOrphanResidualRatioDelta,
                         delta,
                         thresholds.maxOrphanResidualRatioDelta,
                         "orphan residual ratio delta vs ATSA reference (fixture=" + id + ")");
            }
            break;
        }

        case ExternalFixtureGate::TransientPreservation:
        {
            if (our.attackSharpness >= 0.0 && reference.attackSharpness > 0.0)
            {
                const double ratio = our.attackSharpness / reference.attackSharpness;
                addCheck(report,
                         id + ".transient_preservation.attack_sharpness",
                         ratio >= thresholds.minTransientSharpnessRatio,
                         ratio,
                         thresholds.minTransientSharpnessRatio,
                         "attack sharpness ratio vs ATSA reference (fixture=" + id + ")");
            }
            break;
        }
        }
    }

    return report;
}

} // namespace ats

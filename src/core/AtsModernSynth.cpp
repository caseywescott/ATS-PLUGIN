#include "AtsModernSynth.h"

#include "AtsOutputLimiter.h"
#include "AtsResidualBandNoise.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace ats
{

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;

double clamp01(double value) { return std::max(0.0, std::min(1.0, value)); }

int frameStartSample(const ModernAtsRenderPlan& plan, int frameIndex)
{
    return std::max(0, frameIndex) * plan.hopSizeSamples;
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

double wrapPhase(double phase)
{
    if (!std::isfinite(phase))
        return 0.0;
    return std::remainder(phase, kTwoPi);
}

double renderTimeFactor(const ModernAtsSynthConfig& config)
{
    return std::max(0.01, config.timeFactor);
}

double renderTimeFactor(const ModernAtsRenderPlan& plan)
{
    return renderTimeFactor(plan.config);
}

double renderSampleRateFactor(const ModernAtsModel& model, const ModernAtsSynthConfig& config)
{
    if (model.header.sampleRate <= 0 || config.outputSampleRate <= 0)
        return 1.0;
    return static_cast<double>(config.outputSampleRate) / static_cast<double>(model.header.sampleRate);
}

bool usesPitchNeutralStoredPhaseCorrection(const ModernAtsSynthConfig& config)
{
    return config.useStoredPhase && std::abs(config.frequencyFactor - 1.0) <= 1.0e-9;
}

float transientFade(int localIndex, int length, int fadeSamples)
{
    if (fadeSamples <= 0 || length <= 1)
        return 1.0f;

    const int edge = std::min(fadeSamples, std::max(1, length / 2));
    if (localIndex < edge)
        return static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * 0.5 * (double)localIndex / (double)edge));
    const int fromEnd = length - 1 - localIndex;
    if (fromEnd < edge)
        return static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * 0.5 * (double)fromEnd / (double)edge));
    return 1.0f;
}

const ModernAtsTransientMarker* markerForBuffer(const ModernAtsModel& model, const ModernAtsTransientBuffer& buffer)
{
    if (buffer.markerIndex < 0 || buffer.markerIndex >= static_cast<int>(model.transients.size()))
        return nullptr;
    return &model.transients[static_cast<std::size_t>(buffer.markerIndex)];
}

int transientStartSample(const ModernAtsRenderPlan& plan,
                         const ModernAtsTransientMarker& marker,
                         const ModernAtsTransientBuffer& buffer)
{
    (void)buffer;
    return std::max(0, static_cast<int>(std::llround(marker.windowStartSeconds
                                                     * static_cast<double>(plan.outputSampleRate)
                                                     * renderTimeFactor(plan))));
}

int transientLengthSamples(double sourceSampleCount, int sourceSampleRate, int outputSampleRate, double timeFactor)
{
    if (sourceSampleCount <= 0.0 || sourceSampleRate <= 0 || outputSampleRate <= 0)
        return 0;

    const double resampledLength = sourceSampleCount
                                   * static_cast<double>(outputSampleRate)
                                   / static_cast<double>(sourceSampleRate)
                                   * timeFactor;
    return std::max(1, static_cast<int>(std::ceil(resampledLength)));
}

double orphanResidualBandEnergyAt(const ModernAtsModel& model, int frameIndex, int bandIndex)
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(model.frames.size()))
        return 0.0;

    const auto& frame = model.frames[static_cast<std::size_t>(frameIndex)];
    if (bandIndex < 0 || bandIndex >= static_cast<int>(frame.orphanResidualBandEnergy.size()))
        return 0.0;

    const double energy = frame.orphanResidualBandEnergy[static_cast<std::size_t>(bandIndex)];
    return std::isfinite(energy) ? std::max(0.0, energy) : 0.0;
}

} // namespace

bool prepareModernAtsRenderPlan(const ModernAtsModel& model,
                                const ModernAtsSynthConfig& config,
                                ModernAtsRenderPlan& plan,
                                std::string& errorMessage)
{
    errorMessage.clear();
    std::string validationError;
    if (!validateModernAtsModel(model, validationError))
    {
        errorMessage = validationError;
        return false;
    }
    if (model.header.schemaVersion != kModernAtsSchemaVersion)
    {
        errorMessage = "Unsupported modern ATS synthesis schema.";
        return false;
    }
    if (config.outputSampleRate <= 0)
    {
        errorMessage = "Output sample rate must be positive.";
        return false;
    }

    const int baseHop = config.hopSizeSamples > 0 ? config.hopSizeSamples : model.header.hopSizeSamples;
    if (baseHop <= 0)
    {
        errorMessage = "Render hop size must be positive.";
        return false;
    }
    const double clampedTimeFactor = renderTimeFactor(config);
    const double sampleRateFactor = renderSampleRateFactor(model, config);
    const int hop = std::max(1,
                             static_cast<int>(std::round(static_cast<double>(baseHop)
                                                         * sampleRateFactor
                                                         * clampedTimeFactor)));

    int totalSamples = static_cast<int>(model.frames.size()) * hop;
    for (const auto& buffer : model.transientBuffers)
    {
        const auto* marker = markerForBuffer(model, buffer);
        if (marker == nullptr || buffer.samples.empty())
            continue;
        const int start = std::max(0, static_cast<int>(std::llround(marker->windowStartSeconds
                                                                    * static_cast<double>(config.outputSampleRate)
                                                                    * clampedTimeFactor)));
        const int length = transientLengthSamples(static_cast<double>(buffer.samples.size()),
                                                  buffer.sampleRate,
                                                  config.outputSampleRate,
                                                  clampedTimeFactor);
        totalSamples = std::max(totalSamples, start + std::max(0, length));
    }

    plan.model = &model;
    plan.config = config;
    plan.outputSampleRate = config.outputSampleRate;
    plan.hopSizeSamples = hop;
    plan.totalSamples = totalSamples;
    return true;
}

bool renderModernAtsPlanIntoBuffer(const ModernAtsRenderPlan& plan, float* output, int numSamples)
{
    if (plan.model == nullptr || output == nullptr || numSamples < plan.totalSamples || plan.totalSamples < 0)
        return false;

    const auto& model = *plan.model;
    std::memset(output, 0, static_cast<std::size_t>(numSamples) * sizeof(float));
    const bool correctStoredPhaseTrajectory = usesPitchNeutralStoredPhaseCorrection(plan.config);

    // Partial sinusoidal synthesis.
    // Outer loop is per-partial so each track carries a continuous phase accumulator
    // across hops, eliminating phase discontinuities at frame boundaries.
    for (const auto& partial : model.partials)
    {
        double phase = 0.0;
        bool phaseSeeded = false;

        for (int sample = 0; sample < plan.totalSamples; ++sample)
        {
            const int frameIndex = plan.hopSizeSamples > 0 ? sample / plan.hopSizeSamples : 0;
            if (frameIndex >= static_cast<int>(partial.frames.size()))
                break;

            const auto& a = partial.frames[static_cast<std::size_t>(frameIndex)];
            if (a.reason == ModernAtsFrameReason::Slept || a.reason == ModernAtsFrameReason::Pruned
                || a.reason == ModernAtsFrameReason::Rejected)
            {
                phaseSeeded = false; // next active segment re-seeds from stored phase
                continue;
            }

            const int nextFrameIndex =
                std::min(frameIndex + 1, static_cast<int>(partial.frames.size()) - 1);
            const auto& b = partial.frames[static_cast<std::size_t>(nextFrameIndex)];
            const double localT = plan.hopSizeSamples > 0
                                      ? static_cast<double>(sample - frameIndex * plan.hopSizeSamples)
                                            / static_cast<double>(plan.hopSizeSamples)
                                      : 0.0;
            // When the next frame is inactive the partial is ending: fade amplitude to
            // zero but hold frequency constant.  Interpolating frequency toward 0 Hz
            // would produce an audible downward chirp artifact at every partial death.
            const bool bActive = b.reason != ModernAtsFrameReason::Slept
                                 && b.reason != ModernAtsFrameReason::Pruned
                                 && b.reason != ModernAtsFrameReason::Rejected;
            const double amp = lerp(a.amplitude, bActive ? b.amplitude : 0.0, localT) * plan.config.partialGain;
            const double freq = lerp(a.frequencyHz, bActive ? b.frequencyHz : a.frequencyHz, localT)
                                * plan.config.frequencyFactor;
            double phaseCorrectionPerSample = 0.0;
            if (correctStoredPhaseTrajectory && bActive && nextFrameIndex != frameIndex
                && plan.hopSizeSamples > 0)
            {
                const double nextFreq = b.frequencyHz * plan.config.frequencyFactor;
                const double averageFreq = 0.5 * (a.frequencyHz * plan.config.frequencyFactor + nextFreq);
                const double predictedNextPhase =
                    a.phaseRadians
                    + kTwoPi * averageFreq * static_cast<double>(plan.hopSizeSamples)
                          / static_cast<double>(plan.outputSampleRate);
                phaseCorrectionPerSample =
                    wrapPhase(b.phaseRadians - predictedNextPhase) / static_cast<double>(plan.hopSizeSamples);
            }

            if (!phaseSeeded)
            {
                phase = plan.config.useStoredPhase ? wrapPhase(a.phaseRadians) : 0.0;
                phaseSeeded = true;
            }

            output[sample] += static_cast<float>(amp * std::sin(phase));
            phase = wrapPhase(phase
                              + kTwoPi * freq / static_cast<double>(plan.outputSampleRate)
                              + phaseCorrectionPerSample);
        }
    }

    // Orphan residual band noise.
    if (plan.config.orphanResidualGain > 0.0)
    {
        std::array<ResidualBandNoiseState, kModernAtsDefaultResidualBands> residualBandStates {};
        for (int sample = 0; sample < plan.totalSamples; ++sample)
        {
            const int frameIndex = plan.hopSizeSamples > 0 ? sample / plan.hopSizeSamples : 0;
            if (frameIndex < 0 || frameIndex >= static_cast<int>(model.frames.size()))
                continue;

            const int nextFrameIndex =
                std::min(frameIndex + 1, static_cast<int>(model.frames.size()) - 1);
            const double localT = plan.hopSizeSamples > 0
                                      ? static_cast<double>(sample - frameIndex * plan.hopSizeSamples)
                                            / static_cast<double>(plan.hopSizeSamples)
                                      : 0.0;

            for (int band = 0; band < kModernAtsDefaultResidualBands; ++band)
            {
                const double energy = lerp(orphanResidualBandEnergyAt(model, frameIndex, band),
                                           orphanResidualBandEnergyAt(model, nextFrameIndex, band),
                                           localT);
                output[sample] += renderResidualBandNoiseSample(
                    energy,
                    band,
                    sample,
                    plan.config.deterministicNoiseSeed,
                    plan.outputSampleRate,
                    plan.config.orphanResidualGain,
                    residualBandStates[static_cast<std::size_t>(band)]);
            }
        }
    }

    // Transient buffer playback with linear resampling and click-free crossfades.
    if (plan.config.transientGain > 0.0)
    {
        for (std::size_t bufferIndex = 0; bufferIndex < model.transientBuffers.size(); ++bufferIndex)
        {
            const auto& buffer = model.transientBuffers[bufferIndex];
            const auto* marker = markerForBuffer(model, buffer);
            if (marker == nullptr || buffer.samples.empty() || buffer.sampleRate <= 0)
                continue;

            const int start = transientStartSample(plan, *marker, buffer);
            const double timeFactor = renderTimeFactor(plan);
            const int length = transientLengthSamples(static_cast<double>(buffer.samples.size()),
                                                      buffer.sampleRate,
                                                      plan.outputSampleRate,
                                                      timeFactor);
            for (int i = 0; i < length; ++i)
            {
                const int outIndex = start + i;
                if (outIndex < 0 || outIndex >= numSamples)
                    continue;

                // Linear interpolation between adjacent source samples.
                const double srcPos = static_cast<double>(i) * static_cast<double>(buffer.sampleRate)
                                      / (static_cast<double>(plan.outputSampleRate) * timeFactor);
                const int srcFloor = std::max(
                    0, std::min(static_cast<int>(buffer.samples.size()) - 1,
                                static_cast<int>(std::floor(srcPos))));
                const int srcCeil =
                    std::min(static_cast<int>(buffer.samples.size()) - 1, srcFloor + 1);
                const double frac = srcPos - std::floor(srcPos);
                const double srcSample =
                    static_cast<double>(buffer.samples[static_cast<std::size_t>(srcFloor)]) * (1.0 - frac)
                    + static_cast<double>(buffer.samples[static_cast<std::size_t>(srcCeil)]) * frac;

                const double fade = transientFade(i, length, plan.config.transientCrossfadeSamples);
                output[outIndex] += static_cast<float>(
                    srcSample * plan.config.transientGain * marker->gain * buffer.gain * fade);
            }
        }
    }

    AtsSoftLimitConfig limitConfig;
    limitConfig.ceiling = plan.config.outputCeiling;
    limitConfig.kneeStart = plan.config.outputSoftKneeStart;
    limitConfig.denormalClamp = plan.config.denormalClamp;

    // Final safety pass over the mixed output.
    for (int i = 0; i < plan.totalSamples; ++i)
        output[i] = applyAtsSoftLimit(output[i], limitConfig);

    return true;
}

std::vector<float> renderModernAtsModel(const ModernAtsModel& model,
                                        const ModernAtsSynthConfig& config,
                                        ModernAtsRenderStats* stats)
{
    ModernAtsRenderPlan plan;
    std::string error;
    if (!prepareModernAtsRenderPlan(model, config, plan, error))
        return {};

    std::vector<float> output(static_cast<std::size_t>(plan.totalSamples), 0.0f);

    const auto start = std::chrono::steady_clock::now();
    if (!renderModernAtsPlanIntoBuffer(plan, output.data(), static_cast<int>(output.size())))
        return {};
    const auto end = std::chrono::steady_clock::now();

    if (stats != nullptr)
    {
        stats->totalSamples = plan.totalSamples;
        stats->modelStateBytes = estimateModernAtsModelStateBytes(model);
        stats->outputBytes = output.size() * sizeof(float);
        stats->renderSeconds = std::chrono::duration<double>(end - start).count();
        const double audioSeconds = plan.outputSampleRate > 0
                                        ? (double)plan.totalSamples / (double)plan.outputSampleRate
                                        : 0.0;
        stats->cpuRatioAtRealtime = audioSeconds > 0.0 ? stats->renderSeconds / audioSeconds : 0.0;
        stats->peakAbs = 0.0f;
        for (const float sample : output)
            stats->peakAbs = std::max(stats->peakAbs, std::abs(sample));
    }

    return output;
}

ModernAtsRealtimeSafetyReport checkModernAtsRealtimeSafety(const ModernAtsRenderPlan& plan)
{
    ModernAtsRealtimeSafetyReport report;
    report.renderIntoCallerOwnedBuffer = plan.model != nullptr && plan.totalSamples >= 0;
    report.allocatesDuringRender = false;
    report.locksDuringRender = false;
    report.performsFileIoDuringRender = false;
    report.performsAnalysisDuringRender = false;
    report.summary =
        "renderModernAtsPlanIntoBuffer writes into caller-owned memory and performs synthesis/soft limiting only.";
    return report;
}

bool hasDenormalOrNonFiniteSamples(const float* samples, int numSamples, float denormalThreshold)
{
    if (samples == nullptr || numSamples < 0)
        return true;
    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = samples[i];
        if (!std::isfinite(sample))
            return true;
        if (sample != 0.0f && std::abs(sample) < denormalThreshold)
            return true;
    }
    return false;
}

} // namespace ats

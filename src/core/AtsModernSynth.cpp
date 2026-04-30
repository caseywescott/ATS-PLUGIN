#include "AtsModernSynth.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace ats
{

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;

double clamp01(double value) { return std::max(0.0, std::min(1.0, value)); }

float sanitize(double value, double denormalClamp)
{
    if (!std::isfinite(value) || std::abs(value) < denormalClamp)
        return 0.0f;
    return static_cast<float>(value);
}

std::uint32_t mix32(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float deterministicNoise(std::uint32_t seed, int sampleIndex, int streamIndex)
{
    const auto x = mix32(seed ^ (0x9e3779b9u * static_cast<std::uint32_t>(streamIndex + 1))
                         ^ static_cast<std::uint32_t>(sampleIndex + 0x85ebca6bu));
    return (static_cast<float>(x & 0x00ffffffu) / 8388607.5f) - 1.0f;
}

int frameStartSample(const ModernAtsRenderPlan& plan, int frameIndex)
{
    return std::max(0, frameIndex) * plan.hopSizeSamples;
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

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
    return std::max(0, static_cast<int>(std::llround(marker.windowStartSeconds * plan.outputSampleRate)));
}

double frameEnergy(const ModernAtsFrame& frame)
{
    double total = 0.0;
    for (const double energy : frame.orphanResidualBandEnergy)
        total += std::max(0.0, energy);
    return total;
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
    const double clampedTimeFactor = std::max(0.01, config.timeFactor);
    const int hop = std::max(1, static_cast<int>(std::round(static_cast<double>(baseHop) * clampedTimeFactor)));

    int totalSamples = static_cast<int>(model.frames.size()) * hop;
    for (const auto& buffer : model.transientBuffers)
    {
        const auto* marker = markerForBuffer(model, buffer);
        if (marker == nullptr || buffer.samples.empty())
            continue;
        const int start = std::max(0, static_cast<int>(std::llround(marker->windowStartSeconds * config.outputSampleRate)));
        const int length = static_cast<int>(std::ceil((double)buffer.samples.size()
                                                     * (double)config.outputSampleRate
                                                     / (double)buffer.sampleRate));
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
            const double amp = lerp(a.amplitude, b.amplitude, localT) * plan.config.partialGain;
            const double freq = lerp(a.frequencyHz, b.frequencyHz, localT) * plan.config.frequencyFactor;

            if (!phaseSeeded)
            {
                phase = plan.config.useStoredPhase ? a.phaseRadians : 0.0;
                phaseSeeded = true;
            }

            output[sample] += static_cast<float>(amp * std::sin(phase));
            phase += kTwoPi * freq / static_cast<double>(plan.outputSampleRate);
            if (phase >= kTwoPi)
                phase -= kTwoPi;
            else if (phase < 0.0)
                phase += kTwoPi;
        }
    }

    // Orphan residual band noise.
    if (plan.config.orphanResidualGain > 0.0)
    {
        for (int sample = 0; sample < plan.totalSamples; ++sample)
        {
            const int frameIndex = plan.hopSizeSamples > 0 ? sample / plan.hopSizeSamples : 0;
            if (frameIndex < 0 || frameIndex >= static_cast<int>(model.frames.size()))
                continue;
            const double orphanEnergy = frameEnergy(model.frames[static_cast<std::size_t>(frameIndex)]);
            if (orphanEnergy > 0.0)
            {
                const double noise =
                    deterministicNoise(plan.config.deterministicNoiseSeed, sample, frameIndex);
                output[sample] +=
                    static_cast<float>(std::sqrt(orphanEnergy) * plan.config.orphanResidualGain * noise);
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
            const int length = static_cast<int>(std::ceil(static_cast<double>(buffer.samples.size())
                                                          * static_cast<double>(plan.outputSampleRate)
                                                          / static_cast<double>(buffer.sampleRate)));
            for (int i = 0; i < length; ++i)
            {
                const int outIndex = start + i;
                if (outIndex < 0 || outIndex >= numSamples)
                    continue;

                // Linear interpolation between adjacent source samples.
                const double srcPos = static_cast<double>(i) * static_cast<double>(buffer.sampleRate)
                                      / static_cast<double>(plan.outputSampleRate);
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

    // Final denormal clamp pass over the mixed output.
    for (int i = 0; i < plan.totalSamples; ++i)
        output[i] = sanitize(output[i], plan.config.denormalClamp);

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
    report.summary = "renderModernAtsPlanIntoBuffer writes into caller-owned memory and performs synthesis only.";
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

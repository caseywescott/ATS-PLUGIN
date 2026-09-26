#include "AtsEngine.h"

#include "../core/AtsFrequencyShape.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>

namespace ats
{
namespace
{
// The patched ATSA tracker reports progress from every frame_worker thread
// concurrently. Progress callers expect serial, monotonic calls (the host
// stores an atomic, tests append to a vector), so serialize here and drop
// values that arrive behind one already reported.
struct ProgressContext
{
    const CancellationToken* cancellation = nullptr;
    AtsEngine::Progress* progress = nullptr;
    std::mutex mutex;
    double last = 0.0;
};

void progressCallback(void* user, double fraction)
{
    auto* context = static_cast<ProgressContext*>(user);
    if (context == nullptr || context->progress == nullptr || !*context->progress) return;
    if (context->cancellation != nullptr && context->cancellation->isCancelled()) return;
    const auto value = std::clamp(fraction, 0.0, 1.0);
    std::lock_guard<std::mutex> lock(context->mutex);
    if (value < context->last) return;
    context->last = value;
    (*context->progress)(value);
}

JobResult<AnalysisOutput> failedAnalysis(JobGeneration generation, const char* code, const char* text)
{
    JobResult<AnalysisOutput> result;
    result.generation = generation;
    result.status = JobResult<AnalysisOutput>::Status::failed;
    result.error = { code, text };
    return result;
}
}

JobResult<AnalysisOutput> AtsEngine::analyze(JobGeneration generation,
                                             const CancellationToken& cancellation,
                                             const AnalysisRequest& request,
                                             Progress progress)
{
    if (cancellation.isCancelled())
    {
        auto result = failedAnalysis(generation, "cancelled", "analysis cancelled before start");
        result.status = JobResult<AnalysisOutput>::Status::cancelled;
        return result;
    }
    if (request.samples == nullptr || request.sampleCount == 0 || request.sampleRate <= 0)
        return failedAnalysis(generation, "invalid_request", "analysis requires non-empty samples and a positive sample rate");

    ANARGS arguments {};
    request.parameters.toAnargs(arguments);
    ProgressContext context;
    context.cancellation = &cancellation;
    context.progress = &progress;
    arguments.progress_cb = progress ? progressCallback : nullptr;
    arguments.progress_user = progress ? &context : nullptr;

    double** residual = nullptr;
    int residualLength = 0;
    auto* rawSound = tracker(&arguments,
                             const_cast<double*>(request.samples),
                             request.sampleRate,
                             static_cast<int>(request.sampleCount),
                             &residual,
                             &residualLength);

    AnalysisOutput output;
    if (residual != nullptr)
    {
        if (residual[0] != nullptr && residualLength > 0)
            output.residual.assign(residual[0], residual[0] + residualLength);
        if (residual[0] != nullptr) std::free(residual[0]);
        if (residual[1] != nullptr) std::free(residual[1]);
        std::free(residual);
    }
    if (cancellation.isCancelled())
    {
        if (rawSound != nullptr) { free_sound(rawSound); std::free(rawSound); }
        JobResult<AnalysisOutput> result;
        result.generation = generation;
        result.status = JobResult<AnalysisOutput>::Status::cancelled;
        result.error = { "cancelled", "analysis cancelled" };
        return result;
    }
    if (rawSound == nullptr)
        return failedAnalysis(generation, "analysis_failed", "ATSA tracker returned no model");

    // Some legal-but-extreme host automation combinations can leave the
    // legacy tracker time row non-finite or non-increasing. The former JUCE
    // host effectively avoided those combinations by not forwarding all
    // analysis controls. Keep the controls wired, but repair only an invalid
    // time row from ATSA's authoritative frame-size/sample-rate metadata.
    if (rawSound->time != nullptr && rawSound->time[0] != nullptr &&
        rawSound->frames > 0 && rawSound->srate > 0 && rawSound->frame_size > 0)
    {
        bool validTimes = true;
        for (int frame = 0; frame < rawSound->frames; ++frame)
            if (!std::isfinite(rawSound->time[0][frame]) ||
                (frame > 0 && rawSound->time[0][frame] <= rawSound->time[0][frame - 1]))
            {
                validTimes = false;
                break;
            }
        if (!validTimes)
            for (int frame = 0; frame < rawSound->frames; ++frame)
                rawSound->time[0][frame] = static_cast<double>(frame * rawSound->frame_size) /
                                           static_cast<double>(rawSound->srate);
    }

    output.sound.reset(rawSound);
    JobResult<AnalysisOutput> result;
    result.generation = generation;
    result.status = JobResult<AnalysisOutput>::Status::success;
    result.value = std::move(output);
    return result;
}

JobResult<RenderStats> AtsEngine::render(JobGeneration generation,
                                         const CancellationToken& cancellation,
                                         const ATS_SOUND* sound,
                                         const SynthParams& parameters,
                                         AudioBufferView destination)
{
    JobResult<RenderStats> result;
    result.generation = generation;
    if (cancellation.isCancelled())
    {
        result.status = JobResult<RenderStats>::Status::cancelled;
        result.error = { "cancelled", "render cancelled before start" };
        return result;
    }
    if (sound == nullptr || !destination.valid() || destination.channelCount == 0)
    {
        result.status = JobResult<RenderStats>::Status::failed;
        result.error = { "invalid_request", "render requires a model and destination" };
        return result;
    }

    destination.clear();
    AtsSoundPtr shapedSound;
    const ATS_SOUND* renderSound = sound;
    if (isFrequencyShapeActive(resolveFrequencyShapeSettingsForSound(sound,
                                                                     parameters.frequencyShape)))
    {
        if (auto* copy = copy_sound(const_cast<ATS_SOUND*>(sound)))
        {
            shapedSound.reset(copy);
            applyFrequencyShapeToSound(shapedSound.get(), parameters.frequencyShape);
            renderSound = shapedSound.get();
        }
    }

    SPARAMS synthesis {};
    parameters.toSparams(synthesis, renderSound->dur);
    CURVE timeEnvelope {};
    curve_init(&timeEnvelope);
    double* raw = nullptr;
    int rawFrames = 0;
    do_synthesis(const_cast<ATS_SOUND*>(renderSound), &synthesis, &timeEnvelope, nullptr, &raw, &rawFrames);
    curve_free(&timeEnvelope);

    if (raw == nullptr || rawFrames <= 0)
    {
        if (raw != nullptr) std::free(raw);
        result.status = JobResult<RenderStats>::Status::failed;
        result.error = { "render_failed", "ATSA synthesis returned no samples" };
        return result;
    }
    const auto frames = std::min<std::size_t>(destination.frameCount, static_cast<std::size_t>(rawFrames));
    double sumSquares = 0.0;
    float peak = 0.0f;
    bool finite = true;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const float sample = static_cast<float>(raw[i]);
        finite = finite && std::isfinite(sample);
        const float safe = std::isfinite(sample) ? sample : 0.0f;
        peak = std::max(peak, std::abs(safe));
        sumSquares += static_cast<double>(safe) * safe;
        for (std::uint32_t channel = 0; channel < destination.channelCount; ++channel)
            if (destination.channels[channel] != nullptr) destination.channels[channel][i] = safe;
    }
    std::free(raw);
    if (cancellation.isCancelled())
    {
        result.status = JobResult<RenderStats>::Status::cancelled;
        result.error = { "cancelled", "render cancelled" };
        return result;
    }
    result.status = JobResult<RenderStats>::Status::success;
    result.value.framesWritten = frames;
    result.value.peak = peak;
    result.value.rms = frames == 0 ? 0.0f : static_cast<float>(std::sqrt(sumSquares / frames));
    result.value.finite = finite;
    return result;
}

} // namespace ats

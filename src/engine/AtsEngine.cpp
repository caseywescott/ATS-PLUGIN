#include "AtsEngine.h"

#include "../core/AtsFrequencyShape.h"
#include "../core/AtsMicroModulation.h"
#include "../core/AtsResidualFixes.h"
#include "../core/AtsResidualSynth.h"
#include "../core/AtsTransposeCharacter.h"
#include "../core/AtsVibrato.h"

#include <algorithm>
#include <vector>
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

    // Serra 1997: tracking backwards in time lets the guides lock onto the
    // partials before they reach the attack. The tracker sees the reversed
    // signal and the model is flipped back below. The analysis start/duration
    // window is kept in forward time by mirroring it onto the reversed axis.
    std::vector<double> reversed;
    const double* trackerSamples = request.samples;
    if (request.parameters.reverseTracking)
    {
        reversed = reversedSamples(request.samples, request.sampleCount);
        trackerSamples = reversed.data();
        const double total = static_cast<double>(request.sampleCount) / static_cast<double>(request.sampleRate);
        const double start = std::clamp(arguments.start, 0.0, total);
        const double duration = arguments.duration > 0.0 ? std::min(arguments.duration, total - start) : total - start;
        arguments.start = std::max(0.0, total - start - duration);
        arguments.duration = duration;
    }

    double** residual = nullptr;
    int residualLength = 0;
    auto* rawSound = tracker(&arguments,
                             const_cast<double*>(trackerSamples),
                             request.sampleRate,
                             static_cast<int>(request.sampleCount),
                             &residual,
                             &residualLength);
    if (rawSound != nullptr && request.parameters.reverseTracking)
    {
        auto flipped = reverseFrames(rawSound);
        free_sound(rawSound);
        std::free(rawSound);
        rawSound = flipped ? flipped.release() : nullptr;
        if (residual != nullptr && residual[0] != nullptr && residualLength > 0)
            std::reverse(residual[0], residual[0] + residualLength);
    }
    // Serra 1997: the residual must never carry more energy than the source in
    // any band and frame (mis-tracked attacks inflate it).
    if (rawSound != nullptr && request.parameters.clampResidual && rawSound->band_energy != nullptr)
        (void) clampResidualToSource(rawSound, request.samples, request.sampleCount, request.sampleRate);

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

    // Automation curves resample / re-pitch a copy after the static shape.
    AtsSoundPtr curvedSound;
    if (parameters.curves.active())
    {
        curvedSound = AtsAutomationCurves::apply(renderSound, parameters.curves);
        if (curvedSound)
            renderSound = curvedSound.get();
    }

    // Research-derived model-domain transforms (2026-11-24), each on a copy and
    // each dropping the stored phases. Order: transposition character (so the
    // envelope is read from the un-modulated partials), vibrato, animation.
    bool phasesInvalid = static_cast<bool>(curvedSound);
    double frequencyFactor = parameters.frequencyFactor;
    AtsSoundPtr characterSound;
    if (parameters.transposeCharacter && std::abs(frequencyFactor - 1.0) > 1.0e-9)
    {
        TransposeCharacterParams character;
        character.frequencyFactor = frequencyFactor;
        character.preserveEnvelope = true;
        character.tiltDbPerOctavePerOctave = parameters.transposeTiltDbPerOct;
        character.ceilingHz = 0.95 * 0.5 * parameters.sampleRate;
        characterSound = applyTransposeCharacter(renderSound, character);
        if (characterSound)
        {
            renderSound = characterSound.get();
            frequencyFactor = 1.0; // realised in the model domain
            phasesInvalid = true;
        }
    }
    AtsSoundPtr vibratoSound;
    {
        VibratoParams vibrato;
        vibrato.depth = std::clamp(parameters.vibratoDepth, 0.0, 2.0);
        vibrato.rateScale = std::clamp(parameters.vibratoRateScale, 0.25, 4.0);
        if (vibrato.active())
        {
            vibratoSound = applyVibrato(renderSound, vibrato);
            if (vibratoSound)
            {
                renderSound = vibratoSound.get();
                phasesInvalid = true;
            }
        }
    }
    AtsSoundPtr animatedSound;
    {
        MicroModulationParams animation;
        animation.shimmerDb = std::max(0.0, parameters.animationShimmerDb);
        animation.jitterCents = std::max(0.0, parameters.animationJitterCents);
        animation.rateHz = std::clamp(parameters.animationRateHz, 0.5, 15.0);
        if (animation.active())
        {
            animatedSound = applyMicroModulation(renderSound, animation);
            if (animatedSound)
            {
                renderSound = animatedSound.get();
                phasesInvalid = true;
            }
        }
    }

    SPARAMS synthesis {};
    parameters.toSparams(synthesis, renderSound->dur);
    synthesis.frec = frequencyFactor;
    if (phasesInvalid)
        synthesis.upha = 0; // stored phases do not survive the model-domain transforms
    // Hanna's noise density: with more than one carrier per band the plugin
    // renders the whole residual as band noise and ATSA renders partials only.
    const bool pluginResidual = parameters.residualDensity > 1 && parameters.residualGain > 0.0
        && renderSound->band_energy != nullptr;
    if (pluginResidual)
        synthesis.ramp = 0.0;
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
    if (pluginResidual)
    {
        ResidualSynthParams residual;
        residual.sampleRate = parameters.sampleRate;
        residual.carriersPerBand = std::clamp(parameters.residualDensity, 1, 64);
        residual.gain = parameters.residualGain;
        residual.startTime = synthesis.beg;
        residual.duration = std::max(0.0, synthesis.end - synthesis.beg);
        residual.timeFactor = parameters.timeFactor;
        std::vector<double> noise;
        (void) renderResidualBands(renderSound, residual, noise);
        const auto mixed = std::min<std::size_t>(noise.size(), static_cast<std::size_t>(rawFrames));
        for (std::size_t i = 0; i < mixed; ++i)
            raw[i] += noise[i];
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

#pragma once

#include "../core/AtsStateCodec.h"
#include "../core/AtsTransientSmear.h"
#include "../core/MidiNoteRouter.h"
#include "../core/ParameterRegistry.h"
#include "../core/PlaybackBuffers.h"
#include "../engine/SourceImportService.h"
#include "../engine/AtsEngine.h"
#include "../ui/Editor.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstddef>
#include <chrono>
#include <future>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ats::host
{

enum class HostFormat : std::uint8_t { vst3, au, standalone };

struct HostIdentity
{
    std::string_view productName = "ATS Resynth";
    std::string_view company = "ATSPlugin";
    std::string_view bundleId = "com.atsplugin.resynth";
    std::string_view vst3ManufacturerCode = "AtPl";
    std::string_view vst3ProductCode = "Atr1";
    std::string_view category = "Audio effect";
    double latencySeconds = 0.0;
    double tailSeconds = 0.0;
    bool midiInput = true;
    bool midiOutput = false;

    [[nodiscard]] static constexpr HostIdentity current() noexcept { return {}; }
};

struct HostBusLayout
{
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 2;
};

enum class HostState : std::uint8_t { cold, prepared, released };

struct HostPrepareSpec
{
    double sampleRate = 0.0;
    std::uint32_t maximumBlockSize = 0;
    std::uint32_t outputChannels = 0;
    std::uint32_t inputChannels = 0;
};

struct HostParameterInfo
{
    std::string_view id;
    std::string_view name;
    std::string_view group;
    ParameterType type = ParameterType::floatValue;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    float interval = 0.0001f;
    float skew = 1.0f;
    std::uint32_t hostNumericId = 0;
    bool automatable = true;
    bool readOnly = false;
    const std::string_view* choices = nullptr;
    std::uint16_t choiceCount = 0;
};

// The Session page has state that is intentionally independent of any native
// editor instance.  Native wrappers forward typed editor commands here, so a
// controller recreation or a view close cannot leave preview / Smearnov work
// owned by the UI object.
struct SessionRuntimeSnapshot
{
    int previewSource = 0;
    int previewLayer = 0;
    bool previewPlaying = false;
    float normalizedPlayhead = 0.0f;
    bool liveSmearEnabled = false;
    std::array<bool, 3> variationStored {};
    bool smearnovRunning = false;
    bool smearedSourceAvailable = false;
    std::uint64_t timelineRevision = 0;
    std::uint64_t timbreRevision = 0;
    std::uint64_t onsetRevision = 0;
    std::vector<float> timeline { 0.0f, 1.0f };
    std::vector<float> timbrePlan;
    std::vector<float> onsetOverlay;
};

using HostStateChunk = std::vector<std::byte>;

class HostAdapter
{
public:
    static constexpr std::size_t kMaximumStateChunkBytes = 64u * 1024u * 1024u;

    HostAdapter() = default;
    ~HostAdapter()
    {
        cancelAnalysis();
        if (analysisFuture_.valid())
            analysisFuture_.wait();
        cancelSmearnov();
        if (smearnovFuture_.valid())
            smearnovFuture_.wait();
    }
    HostAdapter(const HostAdapter&) = delete;
    HostAdapter& operator=(const HostAdapter&) = delete;

    [[nodiscard]] HostState state() const noexcept { return state_; }
    [[nodiscard]] HostFormat format() const noexcept { return format_; }
    [[nodiscard]] const HostIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] const HostPrepareSpec& prepareSpec() const noexcept { return spec_; }
    [[nodiscard]] HostBusLayout busLayout() const noexcept
    { return { spec_.inputChannels, spec_.outputChannels }; }
    [[nodiscard]] bool editorAttached() const noexcept { return editorAttached_; }
    [[nodiscard]] bool editorVisible() const noexcept { return editorVisible_; }
    [[nodiscard]] float editorScale() const noexcept { return editorScale_; }
    [[nodiscard]] std::array<float, 2> editorLogicalSize() const noexcept
    { return { editorWidth_, editorHeight_ }; }
    [[nodiscard]] const ParameterStore& parameters() const noexcept { return parameters_; }
    [[nodiscard]] ParameterStore& parameters() noexcept { return parameters_; }
    [[nodiscard]] const HostStateChunk& stateChunk() const noexcept { return stateChunk_; }
    [[nodiscard]] SessionRuntimeSnapshot sessionSnapshot() const
    {
        SessionRuntimeSnapshot snapshot;
        snapshot.previewSource = previewSource_.load(std::memory_order_acquire);
        snapshot.previewLayer = previewLayer_.load(std::memory_order_acquire);
        snapshot.previewPlaying = previewPlaying_.load(std::memory_order_acquire);
        snapshot.normalizedPlayhead = normalizedPlayhead_.load(std::memory_order_acquire);
        snapshot.liveSmearEnabled = liveSmearEnabled_.load(std::memory_order_acquire);
        snapshot.variationStored = variationStored_;
        snapshot.smearnovRunning = smearnovRunning_.load(std::memory_order_acquire);
        snapshot.smearedSourceAvailable = smearedSourceAvailable_.load(std::memory_order_acquire);
        snapshot.timelineRevision = timelineRevision_;
        snapshot.timbreRevision = timbreRevision_;
        snapshot.onsetRevision = onsetRevision_;
        snapshot.timeline = timeline_;
        snapshot.timbrePlan = timbrePlan_;
        snapshot.onsetOverlay = onsetOverlay_;
        return snapshot;
    }
    [[nodiscard]] const engine::ImportedSource* source(int slot) const noexcept
    {
        return slot >= 0 && slot < static_cast<int>(sources_.size()) && sources_[static_cast<std::size_t>(slot)]
                   ? &*sources_[static_cast<std::size_t>(slot)]
                   : nullptr;
    }

    // The wrapper calls this from its UI/action domain. Decoding is kept out
    // of process() and accepts only host-neutral paths and slots.
    bool importSource(const Path& path, int slot, engine::SourceImportKind kind,
                      Error& error)
    {
        error = {};
        auto imported = engine::SourceImportService::import(
            sourceGenerations_.next(), sourceCancellation_.token(), { path, slot, kind });
        if (imported.status != JobResult<engine::ImportedSource>::Status::success)
        {
            error = imported.error;
            return false;
        }
        sources_[static_cast<std::size_t>(slot)] = std::move(imported.value);
        ++sourceRevisions_[static_cast<std::size_t>(slot)];
        return true;
    }

    // Non-real-time action entry point. It converts an imported WAV/AIFF slot
    // into an ATS model and, when the host is prepared, publishes a rendered
    // block that process() can consume on the audio thread.
    bool analyzeSource(int slot, Error& error)
    {
        error = {};
        if (slot < 0 || slot >= static_cast<int>(sources_.size()) || !sources_[static_cast<std::size_t>(slot)])
        {
            error = { "missing_source", "select an imported audio source before analysis" };
            return false;
        }
        auto& source = *sources_[static_cast<std::size_t>(slot)];
        if (source.kind != engine::SourceImportKind::audio || source.monoSamples.empty() || source.sampleRate <= 0)
        {
            error = { "source_not_audio", "selected source does not contain audio samples to analyse" };
            return false;
        }
        const auto generation = sourceGenerations_.next();
        auto analysed = AtsEngine::analyze(generation, sourceCancellation_.token(),
            { source.monoSamples.data(), source.monoSamples.size(), source.sampleRate, {} });
        if (analysed.status != JobResult<AnalysisOutput>::Status::success)
        {
            error = analysed.error;
            return false;
        }
        source.sound = std::move(analysed.value.sound);
        source.residual = std::move(analysed.value.residual);
        if (state_ != HostState::prepared)
            return true;

        const auto playbackSlot = playback_.tryAcquireWriteSlot();
        if (playbackSlot == PlaybackBufferPool::kNoBuffer)
        {
            error = { "playback_busy", "no playback buffer is available for the analysed source" };
            return false;
        }
        const auto rendered = AtsEngine::render(generation, sourceCancellation_.token(), source.sound.get(), {},
                                                playback_.writeView(playbackSlot));
        if (rendered.status != JobResult<RenderStats>::Status::success ||
            !playback_.publish(playbackSlot, generation))
        {
            error = rendered.error ? rendered.error
                                   : Error { "playback_publish_failed", "could not publish analysed playback" };
            return false;
        }
        return true;
    }

    // Queues a copy of the decoded source so no worker observes mutable UI
    // ownership. Completion is committed by pollAnalysis() on the host/UI
    // domain; process() remains strictly audio-thread-only.
    bool queueAnalysisSource(int slot, Error& error)
    {
        error = {};
        if (analysisRunning_.load(std::memory_order_acquire))
        {
            error = { "analysis_busy", "an analysis job is already running" };
            return false;
        }
        if (slot < 0 || slot >= static_cast<int>(sources_.size()) || !sources_[static_cast<std::size_t>(slot)])
        {
            error = { "missing_source", "select an imported audio source before analysis" };
            return false;
        }
        const auto& source = *sources_[static_cast<std::size_t>(slot)];
        if (source.kind != engine::SourceImportKind::audio || source.monoSamples.empty() || source.sampleRate <= 0)
        {
            error = { "source_not_audio", "selected source does not contain audio samples to analyse" };
            return false;
        }
        analysisCancellation_.cancel();
        analysisCancellation_ = CancellationSource {};
        analysisSlot_ = slot;
        analysisSourceRevision_ = sourceRevisions_[static_cast<std::size_t>(slot)];
        const auto generation = sourceGenerations_.next();
        auto samples = source.monoSamples;
        const auto sampleRate = source.sampleRate;
        const auto token = analysisCancellation_.token();
        analysisFuture_ = std::async(std::launch::async,
            [generation, token, samples = std::move(samples), sampleRate]() mutable {
                return AtsEngine::analyze(generation, token,
                                          { samples.data(), samples.size(), sampleRate, {} });
            });
        analysisRunning_.store(true, std::memory_order_release);
        return true;
    }

    bool pollAnalysis(Error& error)
    {
        error = {};
        Error smearnovError;
        const bool smearnovCompleted = pollSmearnov(smearnovError);
        if (smearnovCompleted && smearnovError)
            error = std::move(smearnovError);
        if (!analysisRunning_.load(std::memory_order_acquire) || !analysisFuture_.valid() ||
            analysisFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return smearnovCompleted;
        auto analysed = analysisFuture_.get();
        analysisRunning_.store(false, std::memory_order_release);
        if (analysed.status != JobResult<AnalysisOutput>::Status::success)
        {
            error = analysed.error;
            return true;
        }
        if (analysisSlot_ < 0 || !sources_[static_cast<std::size_t>(analysisSlot_)] ||
            sourceRevisions_[static_cast<std::size_t>(analysisSlot_)] != analysisSourceRevision_)
        {
            error = { "stale_analysis", "source changed while analysis was running" };
            return true;
        }
        auto& source = *sources_[static_cast<std::size_t>(analysisSlot_)];
        source.sound = std::move(analysed.value.sound);
        source.residual = std::move(analysed.value.residual);
        if (state_ == HostState::prepared)
        {
            const auto playbackSlot = playback_.tryAcquireWriteSlot();
            const auto rendered = playbackSlot == PlaybackBufferPool::kNoBuffer
                ? JobResult<RenderStats> {}
                : AtsEngine::render(analysed.generation, analysisCancellation_.token(), source.sound.get(), {},
                                    playback_.writeView(playbackSlot));
            if (playbackSlot == PlaybackBufferPool::kNoBuffer ||
                rendered.status != JobResult<RenderStats>::Status::success ||
                !playback_.publish(playbackSlot, analysed.generation))
            {
                error = rendered.error ? rendered.error
                                       : Error { "playback_publish_failed", "could not publish analysed playback" };
            }
        }
        return true;
    }

    void cancelAnalysis() noexcept { analysisCancellation_.cancel(); }
    [[nodiscard]] bool analysisRunning() const noexcept
    { return analysisRunning_.load(std::memory_order_acquire); }

    // Applies the Session subset of EditorAction at the shared host boundary.
    // File import, analysis, and parameter automation deliberately remain
    // wrapper-specific because their host transactions have separate rules.
    // Returns false for a non-Session action so callers can continue their
    // normal action routing without accidentally swallowing it.
    bool applySessionAction(const ui::EditorAction& action, Error& error)
    {
        error = {};
        if (const auto* preview = std::get_if<ui::PreviewAction>(&action))
        {
            if (preview->source < 0 || preview->source >= static_cast<int>(sources_.size()) ||
                preview->layer < 0 || preview->layer > 3)
            {
                error = { "invalid_preview", "preview source or audition layer is invalid" };
                return true;
            }
            previewSource_.store(preview->source, std::memory_order_release);
            previewLayer_.store(preview->layer, std::memory_order_release);
            explicitPreview_.store(true, std::memory_order_release);
            if (!preview->start)
            {
                previewPlaying_.store(false, std::memory_order_release);
                return true;
            }
            if (!renderSourceForPreview(preview->source, error))
                return true;
            previewFrame_.store(0, std::memory_order_release);
            previewPlaying_.store(true, std::memory_order_release);
            return true;
        }
        if (std::holds_alternative<ui::StopPreviewAction>(action))
        {
            explicitPreview_.store(true, std::memory_order_release);
            previewPlaying_.store(false, std::memory_order_release);
            return true;
        }
        if (const auto* seek = std::get_if<ui::SeekPreviewAction>(&action))
        {
            if (!std::isfinite(seek->normalizedTime) || seek->normalizedTime < 0.0f ||
                seek->normalizedTime > 1.0f)
            {
                error = { "invalid_seek", "preview seek must be normalized" };
                return true;
            }
            normalizedPlayhead_.store(seek->normalizedTime, std::memory_order_release);
            previewFrame_.store(static_cast<std::uint64_t>(seek->normalizedTime *
                                static_cast<float>(std::numeric_limits<std::uint32_t>::max())),
                                std::memory_order_release);
            return true;
        }
        if (const auto* smear = std::get_if<ui::SetPreviewSmearAction>(&action))
        {
            liveSmearEnabled_.store(smear->enabled, std::memory_order_release);
            return true;
        }
        if (const auto* variation = std::get_if<ui::MorphVariationAction>(&action))
        {
            if (variation->slot < 0 || variation->slot >= static_cast<int>(variations_.size()))
            {
                error = { "invalid_variation", "variation slot is invalid" };
                return true;
            }
            const auto index = static_cast<std::size_t>(variation->slot);
            if (variation->store)
            {
                variations_[index] = parameters_.snapshot();
                variationStored_[index] = true;
            }
            else
            {
                if (!variationStored_[index] || variations_[index].size() != parameters_.size())
                {
                    error = { "missing_variation", "save this variation before recalling it" };
                    return true;
                }
                for (std::size_t parameter = 0; parameter < variations_[index].size(); ++parameter)
                    (void) parameters_.write(parameter, variations_[index][parameter]);
            }
            return true;
        }
        if (const auto* timeline = std::get_if<ui::SetMorphTimelineAction>(&action))
        {
            if (timeline->normalizedPoints.empty() || timeline->normalizedPoints.size() > 32 ||
                !strictlyOrderedNormalized(timeline->normalizedPoints))
            {
                error = { "invalid_timeline", "timeline points must be ordered normalized values" };
                return true;
            }
            timeline_ = timeline->normalizedPoints;
            ++timelineRevision_;
            return true;
        }
        if (const auto* onsets = std::get_if<ui::EditSmearnovOnsetsAction>(&action))
        {
            if (onsets->normalizedOnsets.size() > 4096 ||
                !strictlyOrderedNormalized(onsets->normalizedOnsets))
            {
                error = { "invalid_onsets", "Smearnov onsets must be ordered normalized values" };
                return true;
            }
            onsetOverlay_ = onsets->normalizedOnsets;
            ++onsetRevision_;
            return true;
        }
        if (std::holds_alternative<ui::DetectSmearnovAction>(action))
            return queueSmearnov(false, error);
        if (std::holds_alternative<ui::ApplySmearnovAction>(action))
            return queueSmearnov(true, error);
        if (const auto* timbre = std::get_if<ui::SetTimbreMotionPlanAction>(&action))
        {
            if (timbre->normalizedPlan.size() > 4096 ||
                !std::all_of(timbre->normalizedPlan.begin(), timbre->normalizedPlan.end(),
                             [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }))
            {
                error = { "invalid_timbre_plan", "timbre curve values must be normalized" };
                return true;
            }
            timbrePlan_ = timbre->normalizedPlan;
            ++timbreRevision_;
            return true;
        }
        return false;
    }

    [[nodiscard]] const std::vector<ParameterDescriptor>& parameterDescriptors() const noexcept
    { return ParameterRegistry::descriptors(); }

    [[nodiscard]] std::vector<HostParameterInfo> parameterInfo() const
    {
        std::vector<HostParameterInfo> result;
        const auto& descriptors = ParameterRegistry::descriptors();
        result.reserve(descriptors.size());
        for (const auto& descriptor : descriptors)
            result.push_back({ descriptor.id, descriptor.name, descriptor.group, descriptor.type,
                               descriptor.minimum, descriptor.maximum, descriptor.defaultValue,
                               descriptor.interval, descriptor.skew, descriptor.hostNumericId,
                               descriptor.automatable, descriptor.readOnly, descriptor.choices,
                               descriptor.choiceCount });
        return result;
    }

    bool applyParameterChange(const ParameterChange& change) noexcept
    {
        if (change.index >= parameters_.size())
            return false;
        const auto& descriptor = ParameterRegistry::descriptors()[change.index];
        return descriptor.automatable && !descriptor.readOnly
            && parameters_.write(change.index, change.value);
    }

    bool setParameterNormalized(std::uint32_t hostNumericId, float normalized) noexcept
    {
        if (!std::isfinite(normalized))
            return false;
        const auto& descriptors = ParameterRegistry::descriptors();
        const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                     [hostNumericId](const auto& descriptor) {
                                         return descriptor.hostNumericId == hostNumericId;
                                     });
        if (it == descriptors.end() || !it->automatable || it->readOnly)
            return false;
        return parameters_.write(it->denseIndex, it->denormalize(normalized));
    }

    [[nodiscard]] std::string parameterText(std::string_view id) const
    {
        const auto* descriptor = ParameterRegistry::find(id);
        return descriptor == nullptr ? std::string {} : descriptor->text(parameters_.read(id));
    }

    bool setParameterText(std::string_view id, std::string_view text) noexcept
    {
        const auto* descriptor = ParameterRegistry::find(id);
        if (descriptor == nullptr || !descriptor->automatable || descriptor->readOnly)
            return false;
        float value = 0.0f;
        return descriptor->fromText(text, value) && parameters_.write(id, value);
    }

    // The native wrapper owns the format-specific state callback. It passes
    // the complete ATSP payload here; validation happens before replacing the
    // previously published chunk so a rejected host restore cannot partially
    // mutate state.
    bool restoreState(const void* data, std::size_t size)
    {
        if (data == nullptr || size > kMaximumStateChunkBytes ||
            !ats::AtsStateCodec::decodeHeader(data, size))
            return false;

        HostStateChunk next(static_cast<const std::byte*>(data),
                            static_cast<const std::byte*>(data) + size);
        stateChunk_ = std::move(next);
        return true;
    }

    bool restoreState(const HostStateChunk& chunk)
    { return restoreState(chunk.data(), chunk.size()); }

    void clearState() noexcept { stateChunk_.clear(); }

    bool initialize(HostFormat format) noexcept
    {
        // Reinitialization is a valid wrapper lifecycle transition. Detach
        // before resetting flags so no hidden editor pointer survives with a
        // logically cold adapter.
        detachEditor();
        cancelAnalysis();
        cancelSmearnov();
        sources_ = {};
        previewSource_.store(0, std::memory_order_release);
        previewLayer_.store(0, std::memory_order_release);
        previewPlaying_.store(false, std::memory_order_release);
        explicitPreview_.store(false, std::memory_order_release);
        normalizedPlayhead_.store(0.0f, std::memory_order_release);
        previewFrame_.store(0, std::memory_order_release);
        liveSmearEnabled_.store(false, std::memory_order_release);
        smearedSourceAvailable_.store(false, std::memory_order_release);
        timeline_ = { 0.0f, 1.0f };
        timbrePlan_.clear();
        onsetOverlay_.clear();
        variations_ = {};
        variationStored_ = {};
        timelineRevision_ = 0;
        timbreRevision_ = 0;
        onsetRevision_ = 0;
        format_ = format;
        state_ = HostState::cold;
        return true;
    }

    bool prepare(const HostPrepareSpec& spec)
    {
        // Preserve the direct neutral-test entry point while making the
        // wrapper lifecycle explicit for native adapters.
        if (spec.sampleRate <= 0.0 || spec.maximumBlockSize == 0 || spec.outputChannels != 2 ||
            spec.inputChannels != 0)
            return false;
        spec_ = spec;
        playback_.prepare(spec.outputChannels, spec.maximumBlockSize);
        outputTrim_.prepare(spec.sampleRate, 20.0);
        outputTrim_.reset(parameters_.read("outputTrim"));
        midi_.reset();
        state_ = HostState::prepared;
        return true;
    }

    void release() noexcept
    {
        cancelAnalysis();
        cancelSmearnov();
        previewPlaying_.store(false, std::memory_order_release);
        explicitPreview_.store(false, std::memory_order_release);
        detachEditor();
        state_ = HostState::released;
        midi_.reset();
    }

    // Native lifecycle entry point. The host owns the editor object; this
    // adapter owns only the attachment/visibility state and forwards every
    // operation through the backend-neutral seam.
    bool attachEditor(ui::IEditor& editor, void* nativeParent, float scale,
                      float logicalWidth, float logicalHeight) noexcept
    {
        if (nativeParent == nullptr || scale <= 0.0f || logicalWidth <= 0.0f || logicalHeight <= 0.0f)
            return false;

        // A wrapper may recreate or reparent its editor during a host view
        // transition. Never leave the previous editor visible or retained by
        // the neutral adapter while installing the replacement.
        if (editor_ != nullptr)
            detachEditor();

        if (!editor.setSize(static_cast<std::uint32_t>(logicalWidth),
                            static_cast<std::uint32_t>(logicalHeight)))
            return false;
        editor.setScale(scale);
        if (!editor.setParent(nativeParent))
            return false;

        editor_ = &editor;
        editorAttached_ = true;
        editorVisible_ = editor.isShowing();
        editorScale_ = scale;
        editorWidth_ = logicalWidth;
        editorHeight_ = logicalHeight;
        return true;
    }

    bool attachEditor(float scale, float logicalWidth, float logicalHeight) noexcept
    {
        if (scale <= 0.0f || logicalWidth <= 0.0f || logicalHeight <= 0.0f)
            return false;
        editorAttached_ = true;
        editorVisible_ = false;
        editorScale_ = scale;
        editorWidth_ = logicalWidth;
        editorHeight_ = logicalHeight;
        return true;
    }

    bool attachStandaloneEditor(ui::IEditor& editor, float scale,
                                float logicalWidth, float logicalHeight) noexcept
    {
        if (format_ != HostFormat::standalone || scale <= 0.0f ||
            logicalWidth <= 0.0f || logicalHeight <= 0.0f)
            return false;
        if (editor_ != nullptr)
            detachEditor();
        if (!editor.setSize(static_cast<std::uint32_t>(logicalWidth),
                            static_cast<std::uint32_t>(logicalHeight)))
            return false;
        editor.setScale(scale);
        if (!editor.showStandalone())
            return false;

        editor_ = &editor;
        editorAttached_ = true;
        editorVisible_ = editor.isShowing();
        editorScale_ = scale;
        editorWidth_ = logicalWidth;
        editorHeight_ = logicalHeight;
        return editorVisible_;
    }

    void detachEditor() noexcept
    {
        if (editor_ != nullptr)
            editor_->hide();
        editor_ = nullptr;
        editorVisible_ = false;
        editorAttached_ = false;
    }

    bool setEditorVisible(bool visible) noexcept
    {
        if (visible && !editorAttached_) return false;
        if (editor_ != nullptr)
        {
            if (visible)
                editor_->show();
            else
                editor_->hide();
        }
        editorVisible_ = visible;
        return true;
    }

    bool setEditorLogicalSize(float width, float height) noexcept
    {
        if (!editorAttached_ || width <= 0.0f || height <= 0.0f) return false;
        if (editor_ != nullptr &&
            !editor_->setSize(static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height)))
            return false;
        editorWidth_ = width;
        editorHeight_ = height;
        return true;
    }

    bool publishPlayback(std::int32_t slot, JobGeneration generation) noexcept
    {
        return state_ == HostState::prepared && playback_.publish(slot, generation);
    }

    [[nodiscard]] std::int32_t acquirePlaybackWriteSlot() const noexcept
    { return state_ == HostState::prepared ? playback_.tryAcquireWriteSlot() : PlaybackBufferPool::kNoBuffer; }

    [[nodiscard]] AudioBufferView playbackWriteView(std::int32_t slot) noexcept
    { return state_ == HostState::prepared ? playback_.writeView(slot) : AudioBufferView {}; }

    bool process(AudioBufferView output, MidiEventList midiEvents) noexcept
    {
        if (state_ != HostState::prepared || !output.valid() || output.channelCount == 0)
        {
            output.clear();
            return false;
        }
        for (const auto& event : midiEvents)
            midi_.consume(event);

        if (explicitPreview_.load(std::memory_order_acquire) &&
            !previewPlaying_.load(std::memory_order_acquire))
        {
            output.clear();
            return true;
        }
        const auto slot = playback_.beginAudioRead();
        const auto* source = playback_.readBuffer(slot);
        const auto trimTarget = parameters_.read("outputTrim");
        outputTrim_.setTarget(trimTarget);
        if (source != nullptr)
        {
            const auto frames = std::min(output.frameCount, source->frameCount());
            const auto channels = std::min(output.channelCount, source->channelCount());
            const auto sourceFrames = source->frameCount();
            const auto startFrame = explicitPreview_.load(std::memory_order_relaxed) && sourceFrames > 0
                ? previewFrame_.load(std::memory_order_relaxed) % sourceFrames : 0;
            for (std::uint32_t frame = 0; frame < frames; ++frame)
            {
                const auto gain = outputTrim_.process();
                const auto readFrame = sourceFrames == 0 ? 0 : (startFrame + frame) % sourceFrames;
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                    if (output.channels[channel] != nullptr && source->channel(channel) != nullptr)
                        output.channels[channel][frame] = source->channel(channel)[readFrame] * gain;
            }
            if (explicitPreview_.load(std::memory_order_relaxed) && sourceFrames > 0)
                previewFrame_.fetch_add(frames, std::memory_order_release);
        }
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel)
            if (output.channels[channel] != nullptr)
                for (std::uint32_t frame = source == nullptr ? 0 : std::min(output.frameCount, source->frameCount());
                     frame < output.frameCount; ++frame)
                    output.channels[channel][frame] = 0.0f;
        playback_.endAudioRead(slot);
        return true;
    }

    [[nodiscard]] int semitoneOffset() const noexcept { return midi_.semitoneOffset(); }

private:
    struct SmearnovWorkResult
    {
        bool apply = false;
        int slot = -1;
        std::uint64_t sourceRevision = 0;
        std::vector<AtsOnset> onsets;
        AtsSoundPtr transformed;
        Error error;
    };

    static bool strictlyOrderedNormalized(const std::vector<float>& values) noexcept
    {
        float previous = -1.0f;
        for (const auto value : values)
        {
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f || value < previous)
                return false;
            previous = value;
        }
        return true;
    }

    [[nodiscard]] SmearnovDetectParams smearnovDetectParams() const noexcept
    {
        SmearnovDetectParams params;
        params.weightDetFlux = parameters_.read("smearnovWeightDetFlux");
        params.weightResFlux = parameters_.read("smearnovWeightResFlux");
        params.weightHfcFlux = parameters_.read("smearnovWeightHfcFlux");
        params.weightBirth = parameters_.read("smearnovWeightBirth");
        params.weightPhaseDev = parameters_.read("smearnovWeightPhaseDev");
        params.usePhaseDeviation = parameters_.read("smearnovUsePhaseDeviation") >= 0.5f;
        params.thresholdLambda = parameters_.read("smearnovThresholdLambda");
        params.thresholdAutoScale = parameters_.read("smearnovThresholdAutoScale");
        params.peakWindowRadiusFrames = static_cast<int>(parameters_.read("smearnovPeakWindowFrames"));
        params.minGapFramesOverride = static_cast<int>(parameters_.read("smearnovMinGapFrames"));
        params.preMarginSeconds = parameters_.read("smearnovPreMarginMs") / 1000.0;
        params.maxAttackFrames = static_cast<int>(parameters_.read("smearnovMaxAttackFrames"));
        return params;
    }

    [[nodiscard]] SmearnovParams smearnovParams() const noexcept
    {
        SmearnovParams params;
        params.blurMs = parameters_.read("smearnovBlurMs");
        params.sharpenAmount = parameters_.read("smearnovSharpenAmount");
        params.exaggerateDb = parameters_.read("smearnovExaggerateDb");
        params.mix = parameters_.read("smearnovMix") / 100.0;
        params.maxDb = parameters_.read("smearnovMaxDb");
        switch (static_cast<int>(parameters_.read("smearnovTargets")))
        {
        case 0: params.targets = SmearnovTarget::Partials; break;
        case 1: params.targets = SmearnovTarget::Residual; break;
        default: params.targets = SmearnovTarget::Both; break;
        }
        return params;
    }

    bool queueSmearnov(bool apply, Error& error)
    {
        if (smearnovRunning_.load(std::memory_order_acquire))
        {
            error = { "smearnov_busy", "Smearnov work is already in progress" };
            return true;
        }
        const auto slot = previewSource_.load(std::memory_order_acquire);
        if (slot < 0 || slot >= static_cast<int>(sources_.size()) || !sources_[static_cast<std::size_t>(slot)] ||
            !sources_[static_cast<std::size_t>(slot)]->sound)
        {
            error = { "missing_model", "analyse a source before running Smearnov" };
            return true;
        }
        auto* copied = copy_sound(sources_[static_cast<std::size_t>(slot)]->sound.get());
        if (copied == nullptr)
        {
            error = { "copy_failed", "could not snapshot the source model for Smearnov" };
            return true;
        }
        const auto sourceRevision = sourceRevisions_[static_cast<std::size_t>(slot)];
        const auto detect = smearnovDetectParams();
        const auto transform = smearnovParams();
        const auto requested = onsetOverlay_;
        smearnovCancellation_.cancel();
        smearnovCancellation_ = CancellationSource {};
        const auto token = smearnovCancellation_.token();
        smearnovFuture_ = std::async(std::launch::async,
            [apply, slot, sourceRevision, snapshot = AtsSoundPtr { copied }, detect, transform,
             requested, token]() mutable {
                SmearnovWorkResult result;
                result.apply = apply;
                result.slot = slot;
                result.sourceRevision = sourceRevision;
                if (token.isCancelled())
                {
                    result.error = { "cancelled", "Smearnov work was cancelled" };
                    return result;
                }
                auto onsets = detectOnsets(snapshot.get(), detect);
                if (!requested.empty())
                {
                    onsets.clear();
                    const auto frameCount = std::max(1, snapshot.get()->frames - 1);
                    for (const auto normalized : requested)
                    {
                        const auto frame = static_cast<int>(std::lround(normalized * frameCount));
                        onsets.push_back({ frame, frame, frame, 0.0 });
                    }
                }
                if (token.isCancelled())
                {
                    result.error = { "cancelled", "Smearnov work was cancelled" };
                    return result;
                }
                result.onsets = std::move(onsets);
                if (apply)
                {
                    if (!smearAttacks(snapshot.get(), result.onsets, transform))
                    {
                        result.error = { "smearnov_no_change", "Smearnov did not modify the selected source" };
                        return result;
                    }
                    result.transformed = std::move(snapshot);
                }
                return result;
            });
        smearnovRunning_.store(true, std::memory_order_release);
        return true;
    }

    bool pollSmearnov(Error& error)
    {
        error = {};
        if (!smearnovRunning_.load(std::memory_order_acquire) || !smearnovFuture_.valid() ||
            smearnovFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        auto result = smearnovFuture_.get();
        smearnovRunning_.store(false, std::memory_order_release);
        if (result.error)
        {
            error = std::move(result.error);
            return true;
        }
        if (result.slot < 0 || result.slot >= static_cast<int>(sources_.size()) ||
            !sources_[static_cast<std::size_t>(result.slot)] ||
            sourceRevisions_[static_cast<std::size_t>(result.slot)] != result.sourceRevision)
        {
            error = { "stale_smearnov", "source changed while Smearnov work was running" };
            return true;
        }
        const auto frameCount = std::max(1, sources_[static_cast<std::size_t>(result.slot)]->sound.get()->frames - 1);
        onsetOverlay_.clear();
        onsetOverlay_.reserve(result.onsets.size());
        for (const auto& onset : result.onsets)
            onsetOverlay_.push_back(std::clamp(static_cast<float>(onset.frame) / frameCount, 0.0f, 1.0f));
        ++onsetRevision_;
        if (result.apply)
        {
            sources_[static_cast<std::size_t>(result.slot)]->sound = std::move(result.transformed);
            ++sourceRevisions_[static_cast<std::size_t>(result.slot)];
            smearedSourceAvailable_.store(true, std::memory_order_release);
            if (state_ == HostState::prepared && result.slot == previewSource_.load(std::memory_order_acquire))
                (void) renderSourceForPreview(result.slot, error);
        }
        return true;
    }

    void cancelSmearnov() noexcept { smearnovCancellation_.cancel(); }

    bool renderSourceForPreview(int slot, Error& error)
    {
        error = {};
        if (state_ != HostState::prepared)
        {
            error = { "host_not_prepared", "prepare the audio host before starting preview" };
            return false;
        }
        if (slot < 0 || slot >= static_cast<int>(sources_.size()) || !sources_[static_cast<std::size_t>(slot)] ||
            !sources_[static_cast<std::size_t>(slot)]->sound)
        {
            error = { "missing_model", "analyse the selected source before starting preview" };
            return false;
        }
        const auto playbackSlot = playback_.tryAcquireWriteSlot();
        if (playbackSlot == PlaybackBufferPool::kNoBuffer)
        {
            error = { "playback_busy", "no playback buffer is available for preview" };
            return false;
        }
        const auto generation = sourceGenerations_.next();
        const auto rendered = AtsEngine::render(generation, sourceCancellation_.token(),
                                                sources_[static_cast<std::size_t>(slot)]->sound.get(), {},
                                                playback_.writeView(playbackSlot));
        if (rendered.status != JobResult<RenderStats>::Status::success ||
            !playback_.publish(playbackSlot, generation))
        {
            error = rendered.error ? rendered.error
                                   : Error { "playback_publish_failed", "could not publish preview audio" };
            return false;
        }
        return true;
    }

    HostIdentity identity_ = HostIdentity::current();
    HostFormat format_ = HostFormat::standalone;
    HostPrepareSpec spec_{};
    HostState state_ = HostState::cold;
    ParameterStore parameters_;
    std::array<std::optional<engine::ImportedSource>, 2> sources_;
    GenerationSource sourceGenerations_;
    CancellationSource sourceCancellation_;
    CancellationSource analysisCancellation_;
    std::future<JobResult<AnalysisOutput>> analysisFuture_;
    int analysisSlot_ = -1;
    std::uint64_t analysisSourceRevision_ = 0;
    std::atomic<bool> analysisRunning_ { false };
    CancellationSource smearnovCancellation_;
    std::future<SmearnovWorkResult> smearnovFuture_;
    std::atomic<bool> smearnovRunning_ { false };
    std::array<std::uint64_t, 2> sourceRevisions_ {};
    HostStateChunk stateChunk_;
    PlaybackBufferPool playback_;
    MidiNoteRouter midi_;
    OnePoleSmoother outputTrim_;
    ui::IEditor* editor_ = nullptr;
    bool editorAttached_ = false;
    bool editorVisible_ = false;
    float editorScale_ = 1.0f;
    float editorWidth_ = 0.0f;
    float editorHeight_ = 0.0f;
    std::atomic<int> previewSource_ { 0 };
    std::atomic<int> previewLayer_ { 0 };
    std::atomic<bool> previewPlaying_ { false };
    std::atomic<bool> explicitPreview_ { false };
    std::atomic<float> normalizedPlayhead_ { 0.0f };
    std::atomic<bool> liveSmearEnabled_ { false };
    std::atomic<std::uint64_t> previewFrame_ { 0 };
    std::atomic<bool> smearedSourceAvailable_ { false };
    std::vector<float> timeline_ { 0.0f, 1.0f };
    std::vector<float> timbrePlan_;
    std::vector<float> onsetOverlay_;
    std::array<std::vector<float>, 3> variations_;
    std::array<bool, 3> variationStored_ {};
    std::uint64_t timelineRevision_ = 0;
    std::uint64_t timbreRevision_ = 0;
    std::uint64_t onsetRevision_ = 0;
};

} // namespace ats::host

#pragma once

#include "../core/MidiNoteRouter.h"
#include "../core/ParameterRegistry.h"
#include "../core/PlaybackBuffers.h"
#include "../ui/Editor.h"

#include <algorithm>

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

class HostAdapter
{
public:
    HostAdapter() = default;
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

    bool initialize(HostFormat format) noexcept
    {
        format_ = format;
        state_ = HostState::cold;
        editorAttached_ = false;
        editorVisible_ = false;
        return true;
    }

    bool prepare(const HostPrepareSpec& spec)
    {
        // Preserve the direct neutral-test entry point while making the
        // wrapper lifecycle explicit for native adapters.
        if (spec.sampleRate <= 0.0 || spec.maximumBlockSize == 0 || spec.outputChannels != 2 ||
            (spec.inputChannels != 0 && spec.inputChannels != 2))
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

        const auto slot = playback_.beginAudioRead();
        const auto* source = playback_.readBuffer(slot);
        const auto trimTarget = parameters_.read("outputTrim");
        outputTrim_.setTarget(trimTarget);
        if (source != nullptr)
        {
            const auto frames = std::min(output.frameCount, source->frameCount());
            const auto channels = std::min(output.channelCount, source->channelCount());
            for (std::uint32_t frame = 0; frame < frames; ++frame)
            {
                const auto gain = outputTrim_.process();
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                    if (output.channels[channel] != nullptr && source->channel(channel) != nullptr)
                        output.channels[channel][frame] = source->channel(channel)[frame] * gain;
            }
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
    HostIdentity identity_ = HostIdentity::current();
    HostFormat format_ = HostFormat::standalone;
    HostPrepareSpec spec_{};
    HostState state_ = HostState::cold;
    ParameterStore parameters_;
    PlaybackBufferPool playback_;
    MidiNoteRouter midi_;
    OnePoleSmoother outputTrim_;
    ui::IEditor* editor_ = nullptr;
    bool editorAttached_ = false;
    bool editorVisible_ = false;
    float editorScale_ = 1.0f;
    float editorWidth_ = 0.0f;
    float editorHeight_ = 0.0f;
};

} // namespace ats::host

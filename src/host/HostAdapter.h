#pragma once

#include "../core/MidiNoteRouter.h"
#include "../core/ParameterRegistry.h"
#include "../core/PlaybackBuffers.h"

#include <algorithm>

namespace ats::host
{

enum class HostState : std::uint8_t { cold, prepared, released };

struct HostPrepareSpec
{
    double sampleRate = 0.0;
    std::uint32_t maximumBlockSize = 0;
    std::uint32_t outputChannels = 0;
};

class HostAdapter
{
public:
    HostAdapter() = default;
    HostAdapter(const HostAdapter&) = delete;
    HostAdapter& operator=(const HostAdapter&) = delete;

    [[nodiscard]] HostState state() const noexcept { return state_; }
    [[nodiscard]] const HostPrepareSpec& prepareSpec() const noexcept { return spec_; }
    [[nodiscard]] const ParameterStore& parameters() const noexcept { return parameters_; }
    [[nodiscard]] ParameterStore& parameters() noexcept { return parameters_; }

    bool prepare(const HostPrepareSpec& spec)
    {
        if (spec.sampleRate <= 0.0 || spec.maximumBlockSize == 0 || spec.outputChannels == 0)
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
        state_ = HostState::released;
        midi_.reset();
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
    HostPrepareSpec spec_{};
    HostState state_ = HostState::cold;
    ParameterStore parameters_;
    PlaybackBufferPool playback_;
    MidiNoteRouter midi_;
    OnePoleSmoother outputTrim_;
};

} // namespace ats::host

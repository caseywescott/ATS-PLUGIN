#include "PlaybackEngine.h"

#include <algorithm>

namespace ats::engine
{

bool PlaybackEngine::prepare(const PlaybackConfig& config)
{
    if (config.sampleRate <= 0.0 || config.maximumBlockSize == 0 || config.outputChannels == 0)
        return false;

    config_ = config;
    for (auto& layer : layers_)
        layer.prepare(config.outputChannels, config.maximumBlockSize);
    partialGain_.prepare(config.sampleRate, 20.0);
    residualGain_.prepare(config.sampleRate, 20.0);
    outputTrim_.prepare(config.sampleRate, 20.0);
    partialGain_.reset(1.0f);
    residualGain_.reset(1.0f);
    outputTrim_.reset(1.0f);
    idxPartGain_  = ParameterRegistry::indexOf("partGain");
    idxResGain_   = ParameterRegistry::indexOf("resGain");
    idxOutputTrim_ = ParameterRegistry::indexOf("outputTrim");
    midi_.reset();
    previewMode_.store(PreviewMode::both, std::memory_order_release);
    layerMix_.store(false, std::memory_order_release);
    prepared_.store(true, std::memory_order_release);
    return true;
}

void PlaybackEngine::release() noexcept
{
    prepared_.store(false, std::memory_order_release);
    midi_.reset();
}

std::int32_t PlaybackEngine::tryAcquireWriteSlot(PlaybackLayer layer) const noexcept
{
    if (!prepared()) return PlaybackBufferPool::kNoBuffer;
    return layers_[index(layer)].tryAcquireWriteSlot();
}

AudioBufferView PlaybackEngine::writeView(PlaybackLayer layer, std::int32_t slot) noexcept
{
    if (!prepared()) return {};
    return layers_[index(layer)].writeView(slot);
}

bool PlaybackEngine::publish(PlaybackLayer layer, std::int32_t slot, JobGeneration generation) noexcept
{
    return prepared() && layers_[index(layer)].publish(slot, generation);
}

JobGeneration PlaybackEngine::activeGeneration(PlaybackLayer layer) const noexcept
{
    if (!prepared()) return {};
    return layers_[index(layer)].activeGeneration();
}

bool PlaybackEngine::process(AudioBufferView output,
                             MidiEventList midiEvents,
                             const ParameterStore& parameters) noexcept
{
    if (!prepared() || !output.valid() || output.channelCount == 0)
    {
        output.clear();
        return false;
    }

    for (const auto& event : midiEvents)
        midi_.consume(event);

    output.clear();
    partialGain_.setTarget(parameters.read(idxPartGain_));
    residualGain_.setTarget(parameters.read(idxResGain_));
    outputTrim_.setTarget(parameters.read(idxOutputTrim_));

    std::array<std::int32_t, kLayerCount> readSlots {};
    std::array<const OwnedAudioBuffer*, kLayerCount> sources {};
    for (std::size_t i = 0; i < kLayerCount; ++i)
    {
        readSlots[i] = layers_[i].beginAudioRead();
        sources[i] = layers_[i].readBuffer(readSlots[i]);
    }

    const auto mode = previewMode();
    const bool mix = layerMix() && mode == PreviewMode::both;
    const auto frames = output.frameCount;
    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto trim = outputTrim_.process();
        const auto partial = partialGain_.process() * trim;
        const auto residual = residualGain_.process() * trim;
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel)
        {
            if (output.channels[channel] == nullptr) continue;
            float value = 0.0f;
            const auto add = [&](PlaybackLayer layer, float gain) noexcept {
                const auto* source = sources[index(layer)];
                if (source != nullptr && channel < source->channelCount() && frame < source->frameCount() &&
                    source->channel(channel) != nullptr)
                    value += source->channel(channel)[frame] * gain;
            };

            if (mix)
            {
                add(PlaybackLayer::partials, partial);
                add(PlaybackLayer::residual, residual);
            }
            else
            {
                switch (mode)
                {
                case PreviewMode::both: add(PlaybackLayer::combined, trim); break;
                case PreviewMode::partialsOnly: add(PlaybackLayer::partials, partial); break;
                case PreviewMode::residualOnly: add(PlaybackLayer::residual, residual); break;
                case PreviewMode::smearOnly: add(PlaybackLayer::smear, trim); break;
                }
            }
            output.channels[channel][frame] = value;
        }
    }

    for (std::size_t i = 0; i < kLayerCount; ++i)
        layers_[i].endAudioRead(readSlots[i]);
    return true;
}

} // namespace ats::engine

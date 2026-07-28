#pragma once

#include "../core/PlaybackBuffers.h"

#include <array>

namespace ats::audio
{

struct PlaybackCacheKey
{
    std::uint64_t sourceRevision = 0;
    std::uint64_t modelRevision = 0;
    std::uint64_t morphRevision = 0;
    std::uint64_t displayRevision = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t frameCount = 0;
    std::uint8_t layer = 0;
    bool usePhase = false;
    bool residualOnly = false;

    friend bool operator==(const PlaybackCacheKey& a, const PlaybackCacheKey& b) noexcept
    {
        return a.sourceRevision == b.sourceRevision && a.modelRevision == b.modelRevision &&
               a.morphRevision == b.morphRevision && a.displayRevision == b.displayRevision &&
               a.sampleRate == b.sampleRate && a.frameCount == b.frameCount && a.layer == b.layer &&
               a.usePhase == b.usePhase && a.residualOnly == b.residualOnly;
    }
};

class PlaybackCache
{
public:
    static constexpr std::size_t kSlotCount = 3;

    void prepare(std::uint32_t channels, std::uint32_t frames)
    {
        for (auto& slot : slots_)
        {
            slot.audio.prepare(channels, frames);
            slot.valid = false;
        }
        next_ = 0;
    }

    [[nodiscard]] const ats::OwnedAudioBuffer* lookup(const PlaybackCacheKey& key) const noexcept
    {
        for (const auto& slot : slots_)
            if (slot.valid && slot.key == key) return &slot.audio;
        return nullptr;
    }

    bool publish(const PlaybackCacheKey& key, ats::AudioBufferView source, ats::JobGeneration generation)
    {
        if (!source.valid() || next_ >= slots_.size()) return false;
        auto& slot = slots_[next_++];
        if (next_ == slots_.size()) next_ = 0;
        const auto destination = slot.audio.view();
        if (destination.channelCount != source.channelCount || destination.frameCount != source.frameCount)
            return false;
        for (std::uint32_t ch = 0; ch < source.channelCount; ++ch)
            if (source.channels[ch] != nullptr && destination.channels[ch] != nullptr)
                std::copy_n(source.channels[ch], source.frameCount, destination.channels[ch]);
        slot.key = key;
        slot.generation = generation;
        slot.valid = true;
        return true;
    }

    void invalidate() noexcept
    {
        for (auto& slot : slots_) slot.valid = false;
    }

    [[nodiscard]] ats::JobGeneration generation(const PlaybackCacheKey& key) const noexcept
    {
        for (const auto& slot : slots_)
            if (slot.valid && slot.key == key) return slot.generation;
        return {};
    }

private:
    struct Slot
    {
        ats::OwnedAudioBuffer audio;
        PlaybackCacheKey key{};
        ats::JobGeneration generation{};
        bool valid = false;
    };

    std::array<Slot, kSlotCount> slots_;
    std::size_t next_ = 0;
};

} // namespace ats::audio

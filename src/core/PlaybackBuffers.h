#pragma once

#include "AtsPrimitives.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace ats
{

class OwnedAudioBuffer
{
public:
    void prepare(std::uint32_t channelCount, std::uint32_t frameCount)
    {
        channels_ = std::min<std::uint32_t>(channelCount, static_cast<std::uint32_t>(pointers_.size()));
        frames_ = frameCount;
        storage_.assign(static_cast<std::size_t>(channels_) * frames_, 0.0f);
        for (std::uint32_t ch = 0; ch < channels_; ++ch)
            pointers_[ch] = storage_.data() + static_cast<std::size_t>(ch) * frames_;
    }

    [[nodiscard]] std::uint32_t channelCount() const noexcept { return channels_; }
    [[nodiscard]] std::uint32_t frameCount() const noexcept { return frames_; }
    [[nodiscard]] AudioBufferView view() noexcept { return { pointers_.data(), channels_, frames_ }; }
    [[nodiscard]] const float* channel(std::uint32_t channel) const noexcept
    { return channel < channels_ ? pointers_[channel] : nullptr; }
    [[nodiscard]] float* channel(std::uint32_t channel) noexcept
    { return channel < channels_ ? pointers_[channel] : nullptr; }

private:
    std::vector<float> storage_;
    std::array<float*, 16> pointers_ {};
    std::uint32_t channels_ = 0;
    std::uint32_t frames_ = 0;
};

class PlaybackBufferPool
{
public:
    static constexpr std::uint32_t kBufferCount = 3;
    static constexpr std::int32_t kNoBuffer = -1;

    void prepare(std::uint32_t channels, std::uint32_t frames)
    {
        for (auto& buffer : buffers_) buffer.prepare(channels, frames);
        active_.store(0, std::memory_order_release);
        reader_.store(kNoBuffer, std::memory_order_release);
        for (auto& generation : generations_) generation.store(0, std::memory_order_release);
    }

    [[nodiscard]] std::int32_t tryAcquireWriteSlot() const noexcept
    {
        const auto active = active_.load(std::memory_order_acquire);
        const auto reader = reader_.load(std::memory_order_acquire);
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(kBufferCount); ++slot)
            if (slot != active && slot != reader) return slot;
        return kNoBuffer;
    }

    [[nodiscard]] AudioBufferView writeView(std::int32_t slot) noexcept
    { return validSlot(slot) ? buffers_[static_cast<std::size_t>(slot)].view() : AudioBufferView {}; }

    bool publish(std::int32_t slot, JobGeneration generation) noexcept
    {
        if (!validSlot(slot) || slot == reader_.load(std::memory_order_acquire)) return false;
        const auto active = active_.load(std::memory_order_acquire);
        if (validSlot(active) &&
            generations_[static_cast<std::size_t>(active)].load(std::memory_order_acquire) > generation.value)
            return false;
        generations_[static_cast<std::size_t>(slot)].store(generation.value, std::memory_order_relaxed);
        active_.store(slot, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::int32_t beginAudioRead() noexcept
    {
        const auto slot = active_.load(std::memory_order_acquire);
        reader_.store(slot, std::memory_order_release);
        // If a writer published between the first load and reader claim, keep
        // the original slot for this callback; it remains immutable until end.
        return slot;
    }

    void endAudioRead(std::int32_t slot) noexcept
    {
        if (reader_.load(std::memory_order_acquire) == slot)
            reader_.store(kNoBuffer, std::memory_order_release);
    }

    [[nodiscard]] const OwnedAudioBuffer* readBuffer(std::int32_t slot) const noexcept
    { return validSlot(slot) ? &buffers_[static_cast<std::size_t>(slot)] : nullptr; }

    [[nodiscard]] JobGeneration generation(std::int32_t slot) const noexcept
    { return validSlot(slot) ? JobGeneration { generations_[static_cast<std::size_t>(slot)].load(std::memory_order_acquire) } : JobGeneration {}; }

    [[nodiscard]] JobGeneration activeGeneration() const noexcept
    {
        const auto slot = active_.load(std::memory_order_acquire);
        return generation(slot);
    }

private:
    [[nodiscard]] static bool validSlot(std::int32_t slot) noexcept
    { return slot >= 0 && slot < static_cast<std::int32_t>(kBufferCount); }

    std::array<OwnedAudioBuffer, kBufferCount> buffers_;
    std::array<std::atomic<std::uint64_t>, kBufferCount> generations_ {};
    std::atomic<std::int32_t> active_ { 0 };
    std::atomic<std::int32_t> reader_ { kNoBuffer };
};

class GenerationGate
{
public:
    void publish(JobGeneration generation) noexcept
    { latest_.store(generation.value, std::memory_order_release); }
    [[nodiscard]] bool isCurrent(JobGeneration generation) const noexcept
    { return latest_.load(std::memory_order_acquire) == generation.value; }

private:
    std::atomic<std::uint64_t> latest_ { 0 };
};

} // namespace ats

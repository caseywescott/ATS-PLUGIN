#pragma once

#include "PlaybackSnapshot.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace ats::engine
{

// Three immutable slots provide a bounded publication protocol. Writers copy
// only into a slot that is neither active nor claimed by an audio reader.
// Readers claim before returning a slot and must release it after consuming
// the snapshot. No reader path allocates or blocks.
class SnapshotPublisher
{
public:
    static constexpr std::uint32_t kSlotCount = 3;
    static constexpr std::int32_t kNoSlot = -1;

    bool publish(const PlaybackSnapshot& snapshot) noexcept
    {
        const auto active = active_.load(std::memory_order_acquire);
        const auto reader = reader_.load(std::memory_order_acquire);
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(kSlotCount); ++slot)
        {
            if (slot == active || slot == reader)
                continue;
            slots_[static_cast<std::size_t>(slot)] = snapshot;
            active_.store(slot, std::memory_order_release);
            return true;
        }
        return false;
    }

    [[nodiscard]] const PlaybackSnapshot* beginAudioRead() noexcept
    {
        for (;;)
        {
            const auto slot = active_.load(std::memory_order_acquire);
            reader_.store(slot, std::memory_order_release);
            // A publication racing the claim invalidates this attempt. The
            // claimed slot remains protected until the next iteration clears it.
            if (active_.load(std::memory_order_acquire) == slot)
                return validSlot(slot) ? &slots_[static_cast<std::size_t>(slot)] : nullptr;
            reader_.store(kNoSlot, std::memory_order_release);
        }
    }

    void endAudioRead() noexcept { reader_.store(kNoSlot, std::memory_order_release); }

    // Safe for non-real-time readers that need an owned copy.
    bool read(PlaybackSnapshot& destination) noexcept
    {
        const auto* snapshot = beginAudioRead();
        if (snapshot == nullptr)
        {
            endAudioRead();
            return false;
        }
        destination = *snapshot;
        endAudioRead();
        return true;
    }

private:
    [[nodiscard]] static bool validSlot(std::int32_t slot) noexcept
    { return slot >= 0 && slot < static_cast<std::int32_t>(kSlotCount); }

    std::array<PlaybackSnapshot, kSlotCount> slots_ {};
    std::atomic<std::int32_t> active_ { kNoSlot };
    std::atomic<std::int32_t> reader_ { kNoSlot };
};

} // namespace ats::engine

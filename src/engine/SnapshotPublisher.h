#pragma once

#include "PlaybackSnapshot.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace ats::engine
{

// Three immutable slots provide a bounded publication protocol for any scalar
// snapshot. Writers copy only into a slot that is neither active nor claimed
// by an audio reader. Readers claim before returning a slot and must release
// it after consuming the snapshot. No reader path allocates or blocks.
template <typename Snapshot, std::uint32_t SlotCount = 3>
class AtomicSnapshotPublisher
{
public:
    static constexpr std::uint32_t kSlotCount = SlotCount;
    static constexpr std::int32_t kNoSlot = -1;

    bool publish(const Snapshot& snapshot) noexcept
    {
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(kSlotCount); ++slot)
        {
            std::uint8_t expected = kFree;
            if (!states_[static_cast<std::size_t>(slot)].compare_exchange_strong(
                    expected, kWriting, std::memory_order_acquire, std::memory_order_relaxed))
                continue;
            slots_[static_cast<std::size_t>(slot)] = snapshot;
            states_[static_cast<std::size_t>(slot)].store(kReady, std::memory_order_release);
            const auto previous = active_.exchange(slot, std::memory_order_acq_rel);
            releaseReady(previous);
            return true;
        }
        return false;
    }

    [[nodiscard]] const Snapshot* beginAudioRead() noexcept
    {
        for (;;)
        {
            const auto slot = active_.load(std::memory_order_acquire);
            if (!validSlot(slot)) return nullptr;
            std::uint8_t expected = kReady;
            if (!states_[static_cast<std::size_t>(slot)].compare_exchange_strong(
                    expected, kReading, std::memory_order_acquire, std::memory_order_relaxed))
                continue;
            if (active_.load(std::memory_order_acquire) != slot)
            {
                states_[static_cast<std::size_t>(slot)].store(kFree, std::memory_order_release);
                continue;
            }
            readerSlot_.store(slot, std::memory_order_release);
            return &slots_[static_cast<std::size_t>(slot)];
        }
    }

    void endAudioRead() noexcept
    {
        const auto slot = readerSlot_.exchange(kNoSlot, std::memory_order_acq_rel);
        if (validSlot(slot))
        {
            const auto active = active_.load(std::memory_order_acquire);
            states_[static_cast<std::size_t>(slot)].store(active == slot ? kReady : kFree,
                                                          std::memory_order_release);
            // A writer may have promoted another slot between the active
            // check and the state store. Do not strand the old slot as ready.
            if (active_.load(std::memory_order_acquire) != slot)
                releaseReady(slot);
        }
    }

    // Safe for non-real-time readers that need an owned copy.
    bool read(Snapshot& destination) noexcept
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
    static constexpr std::uint8_t kFree = 0;
    static constexpr std::uint8_t kWriting = 1;
    static constexpr std::uint8_t kReady = 2;
    static constexpr std::uint8_t kReading = 3;

    [[nodiscard]] static bool validSlot(std::int32_t slot) noexcept
    { return slot >= 0 && slot < static_cast<std::int32_t>(kSlotCount); }

    void releaseReady(std::int32_t slot) noexcept
    {
        if (!validSlot(slot)) return;
        std::uint8_t expected = kReady;
        states_[static_cast<std::size_t>(slot)].compare_exchange_strong(
            expected, kFree, std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    std::array<Snapshot, kSlotCount> slots_ {};
    std::array<std::atomic<std::uint8_t>, kSlotCount> states_ {};
    std::atomic<std::int32_t> active_ { kNoSlot };
    std::atomic<std::int32_t> readerSlot_ { kNoSlot };
};

using SnapshotPublisher = AtomicSnapshotPublisher<PlaybackSnapshot>;

} // namespace ats::engine

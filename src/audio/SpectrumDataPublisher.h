#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <mutex>

namespace ats::audio
{

struct SpectrumSnapshot
{
    static constexpr std::size_t kMaxPoints = 720;
    std::array<float, kMaxPoints> values {};
    std::uint32_t count = 0;
    std::uint64_t revision = 0;
};

// The writer is non-real-time (typically a render worker), but bounded storage
// makes publication deterministic and keeps the reader allocation-free.
class SpectrumDataPublisher
{
public:
    void publish(const float* values, std::uint32_t count, std::uint64_t revision)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto bounded = std::min<std::uint32_t>(count, SpectrumSnapshot::kMaxPoints);
        const auto next = static_cast<std::uint32_t>(1u - active_.load(std::memory_order_relaxed));
        for (std::uint32_t i = 0; i < bounded; ++i) slots_[next].values[i] = values == nullptr ? 0.0f : values[i];
        slots_[next].count = bounded;
        slots_[next].revision = revision;
        active_.store(next, std::memory_order_release);
    }

    [[nodiscard]] SpectrumSnapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SpectrumSnapshot result;
        const auto slot = active_.load(std::memory_order_acquire);
        result = slots_[slot];
        return result;
    }

private:
    std::array<SpectrumSnapshot, 2> slots_ {};
    std::atomic<std::uint32_t> active_ { 0 };
    mutable std::mutex mutex_;
};

} // namespace ats::audio

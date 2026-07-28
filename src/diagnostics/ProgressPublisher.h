#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ats::diagnostics
{

struct ProgressSnapshot
{
    std::uint64_t generation = 0;
    float fraction = 0.0f;
    bool running = false;
    bool cancelled = false;
};

class ProgressPublisher
{
public:
    void begin(std::uint64_t generation) noexcept
    {
        generation_.store(generation, std::memory_order_relaxed);
        fraction_.store(0.0f, std::memory_order_relaxed);
        cancelled_.store(false, std::memory_order_relaxed);
        running_.store(true, std::memory_order_release);
    }

    void update(float fraction) noexcept
    {
        if (!running_.load(std::memory_order_acquire)) return;
        if (!std::isfinite(fraction)) fraction = 0.0f;
        fraction_.store(std::clamp(fraction, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    void finish() noexcept { running_.store(false, std::memory_order_release); }
    void cancel() noexcept
    {
        cancelled_.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_release);
    }

    [[nodiscard]] ProgressSnapshot snapshot() const noexcept
    {
        return { generation_.load(std::memory_order_acquire),
                 fraction_.load(std::memory_order_relaxed),
                 running_.load(std::memory_order_acquire),
                 cancelled_.load(std::memory_order_acquire) };
    }

private:
    std::atomic<std::uint64_t> generation_ { 0 };
    std::atomic<float> fraction_ { 0.0f };
    std::atomic<bool> running_ { false };
    std::atomic<bool> cancelled_ { false };
};

} // namespace ats::diagnostics

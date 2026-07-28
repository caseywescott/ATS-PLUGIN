#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace ats::audio
{

struct MeterSnapshot
{
    float peak = 0.0f;
    float rms = 0.0f;
    std::uint64_t frameCount = 0;
    std::uint64_t revision = 0;
    bool finite = true;
};

// Audio writes are scalar atomic stores; UI reads are a consistent value
// snapshot and never require a lock or allocation.
class MeterPublisher
{
public:
    void publishAudioRt(float peak, float rms, std::uint64_t frameCount,
                        std::uint64_t revision, bool finite) noexcept
    {
        peak_.store(std::isfinite(peak) ? peak : 0.0f, std::memory_order_relaxed);
        rms_.store(std::isfinite(rms) ? rms : 0.0f, std::memory_order_relaxed);
        frameCount_.store(frameCount, std::memory_order_relaxed);
        finite_.store(finite && std::isfinite(peak) && std::isfinite(rms), std::memory_order_relaxed);
        revision_.store(revision, std::memory_order_release);
    }

    [[nodiscard]] MeterSnapshot snapshot() const noexcept
    {
        MeterSnapshot result;
        result.revision = revision_.load(std::memory_order_acquire);
        result.peak = peak_.load(std::memory_order_relaxed);
        result.rms = rms_.load(std::memory_order_relaxed);
        result.frameCount = frameCount_.load(std::memory_order_relaxed);
        result.finite = finite_.load(std::memory_order_relaxed);
        return result;
    }

private:
    std::atomic<float> peak_ { 0.0f };
    std::atomic<float> rms_ { 0.0f };
    std::atomic<std::uint64_t> frameCount_ { 0 };
    std::atomic<std::uint64_t> revision_ { 0 };
    std::atomic<bool> finite_ { true };
};

} // namespace ats::audio

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ats::audio
{

struct PlayheadSnapshot
{
    std::uint64_t revision = 0;
    float normalizedTime = 0.0f;
    bool playing = false;
};

class PlayheadPublisher
{
public:
    void publishAudioRt(float normalizedTime, bool playing, std::uint64_t revision) noexcept
    {
        if (!std::isfinite(normalizedTime)) normalizedTime = 0.0f;
        normalizedTime_.store(std::clamp(normalizedTime, 0.0f, 1.0f), std::memory_order_relaxed);
        playing_.store(playing, std::memory_order_relaxed);
        revision_.store(revision, std::memory_order_release);
    }

    [[nodiscard]] PlayheadSnapshot snapshot() const noexcept
    {
        const auto revision = revision_.load(std::memory_order_acquire);
        return { revision, normalizedTime_.load(std::memory_order_relaxed),
                 playing_.load(std::memory_order_relaxed) };
    }

private:
    std::atomic<std::uint64_t> revision_ { 0 };
    std::atomic<float> normalizedTime_ { 0.0f };
    std::atomic<bool> playing_ { false };
};

} // namespace ats::audio

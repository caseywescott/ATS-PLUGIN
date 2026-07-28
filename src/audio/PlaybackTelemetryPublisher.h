#pragma once

#include "MeterPublisher.h"
#include "PlayheadPublisher.h"
#include "../engine/SnapshotPublisher.h"

#include <algorithm>
#include <cmath>

namespace ats::audio
{

struct PlaybackTelemetrySnapshot
{
    ats::engine::PlaybackSnapshot playback{};
    MeterSnapshot meter{};
    PlayheadSnapshot playhead{};
};

// Bounded composition of the scalar playback, meter, and playhead publishers.
// Audio publication performs only fixed-size copies and atomic stores; readers
// take owned scalar snapshots and never access mutable model/audio arrays.
class PlaybackTelemetryPublisher
{
public:
    bool publishAudioRt(const ats::engine::PlaybackSnapshot& playback,
                        float normalizedTime, bool playing,
                        float peak, float rms, std::uint64_t frameCount,
                        std::uint64_t revision, bool finite) noexcept
    {
        PlaybackTelemetrySnapshot snapshot;
        snapshot.playback = playback;
        snapshot.meter.peak = std::isfinite(peak) ? peak : 0.0f;
        snapshot.meter.rms = std::isfinite(rms) ? rms : 0.0f;
        snapshot.meter.frameCount = frameCount;
        snapshot.meter.revision = revision;
        snapshot.meter.finite = finite && std::isfinite(peak) && std::isfinite(rms);
        snapshot.playhead.revision = revision;
        snapshot.playhead.normalizedTime = std::isfinite(normalizedTime)
                                          ? std::clamp(normalizedTime, 0.0f, 1.0f) : 0.0f;
        snapshot.playhead.playing = playing;
        return publication_.publish(snapshot);
    }

    [[nodiscard]] bool read(PlaybackTelemetrySnapshot& destination) noexcept
    {
        return publication_.read(destination);
    }

    [[nodiscard]] MeterSnapshot meter() const noexcept
    {
        PlaybackTelemetrySnapshot snapshot;
        return publication_.read(snapshot) ? snapshot.meter : MeterSnapshot {};
    }
    [[nodiscard]] PlayheadSnapshot playhead() const noexcept
    {
        PlaybackTelemetrySnapshot snapshot;
        return publication_.read(snapshot) ? snapshot.playhead : PlayheadSnapshot {};
    }

private:
    mutable ats::engine::AtomicSnapshotPublisher<PlaybackTelemetrySnapshot> publication_;
};

} // namespace ats::audio

#pragma once

#include "SpectralTraceBuilder.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace ats::ui
{

class SpectrumViewModel
{
public:
    bool accept(SpectralTraceSnapshot snapshot) noexcept
    {
        // Revisions are monotonic only within one source. Switching A/B (or
        // returning to Current) starts a new revision stream.
        if (hasSnapshot_ && snapshot.source == snapshot_.source
            && snapshot.sourceRevision < snapshot_.sourceRevision)
            return false;
        if (hasSnapshot_ && snapshot.source == snapshot_.source
            && snapshot.sourceRevision == snapshot_.sourceRevision
            && snapshot.displayShapeRevision < snapshot_.displayShapeRevision)
            return false;
        snapshot_ = std::move(snapshot);
        hasSnapshot_ = true;
        mode_ = snapshot_.mode;
        return true;
    }

    void setMode(SpectralTraceMode mode) noexcept
    {
        if (mode_ == mode)
            return;
        mode_ = mode;
        if (hasSnapshot_)
            snapshot_.mode = mode;
        ++cacheEpoch_;
    }
    [[nodiscard]] SpectralTraceMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool hasSnapshot() const noexcept { return hasSnapshot_; }
    [[nodiscard]] const SpectralTraceSnapshot& snapshot() const noexcept { return snapshot_; }

    [[nodiscard]] std::uint64_t cacheRevision() const noexcept
    {
        if (!hasSnapshot_) return 0;
        std::uint64_t value = 1469598103934665603ULL;
        mix(value, snapshot_.sourceRevision);
        mix(value, snapshot_.displayShapeRevision);
        mix(value, static_cast<std::uint64_t>(snapshot_.source));
        mix(value, static_cast<std::uint64_t>(snapshot_.mode));
        mix(value, snapshot_.frameCount);
        mix(value, snapshot_.partials.size());
        mix(value, snapshot_.residualBands.size());
        mix(value, snapshot_.onsets.size());
        mix(value, snapshot_.smearnovDetectionCurve.size());
        mix(value, snapshot_.smearnovThresholdCurve.size());
        mix(value, static_cast<std::uint64_t>(snapshot_.frequencyShapeMode));
        mix(value, static_cast<std::uint64_t>(std::llround(snapshot_.frequencyShapeAmount * 100000.0)));
        mix(value, static_cast<std::uint64_t>(std::llround(snapshot_.frequencyShapePivotHz * 100000.0)));
        mix(value, static_cast<std::uint64_t>(std::llround(snapshot_.resolvedFrequencyShapePivotHz * 100000.0)));
        mix(value, cacheEpoch_);
        return value == 0 ? 1 : value;
    }

    void invalidateCache() noexcept { ++cacheEpoch_; }

    void clear() noexcept
    {
        snapshot_ = {};
        hasSnapshot_ = false;
        ++cacheEpoch_;
    }

private:
    static void mix(std::uint64_t& value, std::uint64_t input) noexcept
    {
        value ^= input + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
    }

    SpectralTraceMode mode_ = SpectralTraceMode::combined;
    SpectralTraceSnapshot snapshot_ {};
    bool hasSnapshot_ = false;
    std::uint64_t cacheEpoch_ = 0;
};

} // namespace ats::ui

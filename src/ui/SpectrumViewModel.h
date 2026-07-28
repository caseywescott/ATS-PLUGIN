#pragma once

#include "SpectralTraceBuilder.h"

namespace ats::ui
{

class SpectrumViewModel
{
public:
    bool accept(SpectralTraceSnapshot snapshot) noexcept
    {
        if (hasSnapshot_ && snapshot.sourceRevision < snapshot_.sourceRevision)
            return false;
        if (hasSnapshot_ && snapshot.sourceRevision == snapshot_.sourceRevision
            && snapshot.displayShapeRevision < snapshot_.displayShapeRevision)
            return false;
        snapshot_ = std::move(snapshot);
        hasSnapshot_ = true;
        return true;
    }

    void setMode(SpectralTraceMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] SpectralTraceMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool hasSnapshot() const noexcept { return hasSnapshot_; }
    [[nodiscard]] const SpectralTraceSnapshot& snapshot() const noexcept { return snapshot_; }

    void clear() noexcept
    {
        snapshot_ = {};
        hasSnapshot_ = false;
    }

private:
    SpectralTraceMode mode_ = SpectralTraceMode::combined;
    SpectralTraceSnapshot snapshot_ {};
    bool hasSnapshot_ = false;
};

} // namespace ats::ui

#pragma once

#include "AtsPrimitives.h"

#include <chrono>

namespace ats
{

// Cadences are policy, not timer callbacks. The owner polls these gates from
// its own domain and schedules typed work; no policy here implies completion.
struct SchedulerCadence
{
    std::chrono::milliseconds processor { 33 }; // 30 Hz
    std::chrono::milliseconds editor { 100 }; // 10 Hz
    std::chrono::milliseconds spectrum { 100 }; // 10 Hz
    std::chrono::milliseconds playhead { 16 }; // approximately 60 Hz
    std::chrono::milliseconds synthesis { 250 }; // 4 Hz
    std::chrono::milliseconds frequencyShapeDebounce { 80 };
    std::chrono::milliseconds morphDraftDebounce { 120 };
    std::chrono::milliseconds morphFinalizeDebounce { 350 };
    std::chrono::milliseconds undoGrouping { 500 };
};

class CadenceGate
{
public:
    explicit CadenceGate(std::chrono::milliseconds interval) noexcept : interval_(interval) {}

    [[nodiscard]] bool due(MonotonicClock::TimePoint now) noexcept
    {
        if (!primed_ || now - last_ >= interval_)
        {
            last_ = now;
            primed_ = true;
            return true;
        }
        return false;
    }

    void reset() noexcept { primed_ = false; }

private:
    std::chrono::milliseconds interval_;
    MonotonicClock::TimePoint last_{};
    bool primed_ = false;
};

} // namespace ats

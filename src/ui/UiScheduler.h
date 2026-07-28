#pragma once

#include "../core/SchedulerPolicy.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace ats::ui
{

enum class UiRefreshSurface : std::uint8_t
{
    processor,
    editor,
    spectrum,
    playhead,
    synthesis
};

struct UiRefreshInput
{
    bool visible = true;
    bool focusedText = false;
    bool pointerCapture = false;
    bool pendingGesture = false;
    bool sourceReady = false;
    std::uint64_t sourceRevision = 0;
};

struct UiRefreshEvent
{
    UiRefreshSurface surface = UiRefreshSurface::editor;
    std::uint64_t sourceRevision = 0;
    ats::MonotonicClock::TimePoint at{};
};

class UiScheduler
{
public:
    using RegistrationId = std::uint32_t;
    using Callback = std::function<void(const UiRefreshEvent&)>;
    static constexpr RegistrationId kInvalidRegistration = 0;
    static constexpr std::size_t kMaximumRegistrations = 8;

    RegistrationId registerSurface(UiRefreshSurface surface,
                                   std::chrono::milliseconds cadence,
                                   Callback callback)
    {
        for (std::size_t i = 0; i < registrations_.size(); ++i)
        {
            auto& registration = registrations_[i];
            if (registration.active)
                continue;
            registration = Registration { static_cast<RegistrationId>(i + 1), surface,
                                          ats::CadenceGate(cadence), std::move(callback), true };
            return registration.id;
        }
        return kInvalidRegistration;
    }

    bool post(RegistrationId id, ats::MonotonicClock::TimePoint now,
              const UiRefreshInput& input)
    {
        auto* registration = find(id);
        if (registration == nullptr || !registration->active || stopped_ || !input.visible
            || input.focusedText || input.pointerCapture || input.pendingGesture
            || !input.sourceReady)
            return false;
        if (!registration->cadence.due(now))
            return false;

        registration->callback(UiRefreshEvent { registration->surface, input.sourceRevision, now });
        return true;
    }

    void cancel(RegistrationId id) noexcept
    {
        if (auto* registration = find(id))
            registration->active = false;
    }

    void shutdown() noexcept
    {
        stopped_ = true;
        for (auto& registration : registrations_)
            registration.active = false;
    }

private:
    struct Registration
    {
        RegistrationId id = kInvalidRegistration;
        UiRefreshSurface surface = UiRefreshSurface::editor;
        ats::CadenceGate cadence { std::chrono::milliseconds(0) };
        Callback callback;
        bool active = false;
    };

    Registration* find(RegistrationId id) noexcept
    {
        if (id == kInvalidRegistration || id > registrations_.size())
            return nullptr;
        auto& registration = registrations_[id - 1];
        return registration.id == id ? &registration : nullptr;
    }

    std::array<Registration, kMaximumRegistrations> registrations_ {};
    bool stopped_ = false;
};

} // namespace ats::ui

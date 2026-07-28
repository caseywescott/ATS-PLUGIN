#include "../src/ui/UiScheduler.h"

#include <cassert>

int main()
{
    ats::ui::UiScheduler scheduler;
    const auto start = ats::MonotonicClock::now();
    int refreshes = 0;
    std::uint64_t revision = 0;
    const auto id = scheduler.registerSurface(ats::ui::UiRefreshSurface::spectrum,
        std::chrono::milliseconds(100), [&](const ats::ui::UiRefreshEvent& event) {
            ++refreshes;
            revision = event.sourceRevision;
        });
    assert(id != ats::ui::UiScheduler::kInvalidRegistration);
    const ats::ui::UiRefreshInput ready { true, false, false, false, true, 12 };
    assert(scheduler.post(id, start, ready));
    assert(!scheduler.post(id, start + std::chrono::milliseconds(99), ready));
    assert(scheduler.post(id, start + std::chrono::milliseconds(100), ready));
    assert(refreshes == 2 && revision == 12);

    assert(!scheduler.post(id, start + std::chrono::milliseconds(200), { true, true, false, false, true, 13 }));
    assert(!scheduler.post(id, start + std::chrono::milliseconds(200), { true, false, true, false, true, 13 }));
    assert(!scheduler.post(id, start + std::chrono::milliseconds(200), { true, false, false, true, true, 13 }));
    assert(!scheduler.post(id, start + std::chrono::milliseconds(200), { true, false, false, false, false, 13 }));
    assert(scheduler.post(id, start + std::chrono::milliseconds(200), ready));
    scheduler.cancel(id);
    assert(!scheduler.post(id, start + std::chrono::milliseconds(300), ready));

    const auto second = scheduler.registerSurface(ats::ui::UiRefreshSurface::playhead,
                                                  std::chrono::milliseconds(16), {});
    assert(second != ats::ui::UiScheduler::kInvalidRegistration);
    scheduler.shutdown();
    assert(!scheduler.post(second, start, ready));
    return 0;
}

#include "../src/ui/UiScheduler.h"

#include <cassert>

int main()
{
    ats::ui::UiScheduler scheduler;
    const auto start = ats::MonotonicClock::now();
    int editorRefreshes = 0;
    std::uint64_t lastRevision = 0;
    const auto editor = scheduler.registerSurface(ats::ui::UiRefreshSurface::editor,
                                                  std::chrono::milliseconds(100),
                                                  [&editorRefreshes, &lastRevision](const ats::ui::UiRefreshEvent& event) {
                                                      ++editorRefreshes;
                                                      lastRevision = event.sourceRevision;
                                                  });
    assert(editor != ats::ui::UiScheduler::kInvalidRegistration);
    const ats::ui::UiRefreshInput ready { true, false, false, false, true, 9 };
    assert(scheduler.post(editor, start, ready));
    assert(!scheduler.post(editor, start + std::chrono::milliseconds(99), ready));
    assert(scheduler.post(editor, start + std::chrono::milliseconds(100), ready));
    assert(editorRefreshes == 2 && lastRevision == 9);

    const ats::ui::UiRefreshInput focused { true, true, false, false, true, 10 };
    assert(!scheduler.post(editor, start + std::chrono::milliseconds(200), focused));
    assert(scheduler.post(editor, start + std::chrono::milliseconds(200), ready));

    const auto playhead = scheduler.registerSurface(ats::ui::UiRefreshSurface::playhead,
                                                    std::chrono::milliseconds(16),
                                                    [](const ats::ui::UiRefreshEvent&) {});
    assert(playhead != ats::ui::UiScheduler::kInvalidRegistration);
    assert(!scheduler.post(playhead, start, { true, false, false, false, false, 1 }));
    assert(!scheduler.post(playhead, start, { true, false, true, false, true, 1 }));
    assert(scheduler.post(playhead, start, ready));
    scheduler.cancel(playhead);
    assert(!scheduler.post(playhead, start + std::chrono::milliseconds(16), ready));
    scheduler.shutdown();
    assert(!scheduler.post(editor, start + std::chrono::milliseconds(300), ready));
    return 0;
}

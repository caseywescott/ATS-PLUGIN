#include "../src/engine/JobCoordinator.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

int main()
{
    ats::engine::JobCoordinator coordinator(1);
    std::atomic<int> completions { 0 };
    std::atomic<int> cancelled { 0 };

    const auto first = coordinator.begin();
    assert(coordinator.isCurrent(first));
    const auto second = coordinator.begin();
    assert(second.generation != first.generation);
    assert(first.cancellation.isCancelled());
    assert(!coordinator.isCurrent(first));
    assert(coordinator.isCurrent(second));

    assert(coordinator.submit([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return 42;
    }, [&](ats::JobResult<int> result) {
        ++completions;
        if (result.status == ats::JobResult<int>::Status::cancelled) ++cancelled;
    }));
    assert(coordinator.submit([] { return 7; }, [&](ats::JobResult<int> result) {
        ++completions;
        if (result.status == ats::JobResult<int>::Status::cancelled) ++cancelled;
        else assert(result.status == ats::JobResult<int>::Status::success && result.value == 7);
    }));
    coordinator.waitIdle();
    assert(completions == 2);
    assert(cancelled >= 1);

    const auto ticket = coordinator.begin();
    coordinator.cancel();
    assert(ticket.cancellation.isCancelled());
    assert(!coordinator.isCurrent(ticket));
    coordinator.shutdown();
    return 0;
}

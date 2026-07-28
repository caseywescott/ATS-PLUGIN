#include "JobScheduler.h"

#include <atomic>
#include <cassert>
#include <iostream>

int main()
{
    ats::JobScheduler scheduler;
    ats::CancellationToken live;
    std::atomic<int> observed { 0 };
    assert(scheduler.submit({ 4 }, live, [] { return 42; }, [&observed](ats::JobResult<int> result) {
        assert(result.generation.value == 4);
        assert(result.status == ats::JobResult<int>::Status::success);
        observed.store(result.value, std::memory_order_release);
    }));

    ats::CancellationToken cancelled;
    cancelled.cancel();
    std::atomic<bool> sawCancelled { false };
    assert(scheduler.submit({ 5 }, cancelled, [] { return 99; }, [&sawCancelled](ats::JobResult<int> result) {
        sawCancelled.store(result.status == ats::JobResult<int>::Status::cancelled, std::memory_order_release);
    }));
    scheduler.waitIdle();
    assert(observed.load(std::memory_order_acquire) == 42);
    assert(sawCancelled.load(std::memory_order_acquire));
    scheduler.shutdown();
    assert(!scheduler.submit({ 6 }, live, [] { return 1; }, [](ats::JobResult<int>) {}));
    std::cout << "job_scheduler_test: typed completion and cancellation ok\n";
}

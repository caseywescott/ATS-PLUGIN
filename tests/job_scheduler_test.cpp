#include "JobScheduler.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

#include "../src/core/SchedulerPolicy.h"

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

    // The scheduler must retain cancellation state independently of the
    // caller's token object while a queued task is waiting for a worker.
    std::atomic<bool> retainedTokenCompleted { false };
    {
        ats::CancellationSource source;
        assert(scheduler.submit({ 7 }, source.token(), [] { return 7; },
                                 [&retainedTokenCompleted](ats::JobResult<int> result) {
                                     retainedTokenCompleted.store(result.status == ats::JobResult<int>::Status::success,
                                                                   std::memory_order_release);
                                 }));
    }
    scheduler.waitIdle();
    assert(retainedTokenCompleted.load(std::memory_order_acquire));

    ats::CancellationSource runningSource;
    std::atomic<bool> started { false };
    std::atomic<bool> sawRunningCancellation { false };
    assert(scheduler.submit({ 8 }, runningSource.token(), [&started] {
        started.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return 8;
    }, [&sawRunningCancellation](ats::JobResult<int> result) {
        sawRunningCancellation.store(result.status == ats::JobResult<int>::Status::cancelled,
                                     std::memory_order_release);
    }));
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    runningSource.cancel();
    scheduler.waitIdle();
    assert(sawRunningCancellation.load(std::memory_order_acquire));

    const ats::SchedulerCadence cadence;
    assert(cadence.processor.count() == 33);
    ats::CadenceGate gate(cadence.frequencyShapeDebounce);
    const auto now = ats::MonotonicClock::now();
    assert(gate.due(now));
    assert(!gate.due(now + std::chrono::milliseconds(79)));
    assert(gate.due(now + std::chrono::milliseconds(80)));
    scheduler.shutdown();
    assert(!scheduler.submit({ 6 }, live, [] { return 1; }, [](ats::JobResult<int>) {}));
    std::cout << "job_scheduler_test: typed completion and cancellation ok\n";
}

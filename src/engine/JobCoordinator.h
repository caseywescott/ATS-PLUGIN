#pragma once

#include "../core/JobScheduler.h"

#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

namespace ats::engine
{

// Owns the generation/cancellation boundary around JobScheduler. Starting a
// new job cancels the previous token; completion callbacks receive a cancelled
// result when work finishes after being superseded, so stale model snapshots
// cannot be installed by accident.
class JobCoordinator
{
public:
    explicit JobCoordinator(std::size_t workerCount = 1) : scheduler_(workerCount) {}
    JobCoordinator(const JobCoordinator&) = delete;
    JobCoordinator& operator=(const JobCoordinator&) = delete;

    struct Ticket
    {
        JobGeneration generation{};
        CancellationToken cancellation{};
    };

    [[nodiscard]] Ticket begin() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_.cancel();
        cancellation_ = CancellationSource {};
        current_ = generations_.next();
        return { current_, cancellation_.token() };
    }

    void cancel() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancellation_.cancel();
        current_ = generations_.next();
    }

    [[nodiscard]] JobGeneration current() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_;
    }

    [[nodiscard]] bool isCurrent(Ticket ticket) const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ticket.generation == current_ && !ticket.cancellation.isCancelled();
    }

    template <typename Work, typename Complete>
    bool submit(Work&& work, Complete&& complete)
    {
        using Value = std::invoke_result_t<Work>;
        const auto ticket = begin();
        return scheduler_.submit(ticket.generation, ticket.cancellation,
            std::forward<Work>(work),
            [this, ticket, complete = std::forward<Complete>(complete)](JobResult<Value> result) mutable {
                if (!isCurrent(ticket))
                    result.status = JobResult<Value>::Status::cancelled;
                complete(std::move(result));
            });
    }

    void waitIdle() { scheduler_.waitIdle(); }
    void shutdown() { scheduler_.shutdown(); }

private:
    mutable std::mutex mutex_;
    GenerationSource generations_;
    CancellationSource cancellation_;
    JobGeneration current_{};
    JobScheduler scheduler_;
};

} // namespace ats::engine

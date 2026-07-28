#pragma once

#include "AtsPrimitives.h"

#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>

namespace ats
{

class JobScheduler
{
public:
    explicit JobScheduler(std::size_t workerCount = 1)
    {
        workerCount = std::max<std::size_t>(1, workerCount);
        workers_.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    }

    JobScheduler(const JobScheduler&) = delete;
    JobScheduler& operator=(const JobScheduler&) = delete;

    ~JobScheduler() { shutdown(); }

    template <typename Work, typename Complete>
    bool submit(JobGeneration generation, const CancellationToken& token, Work&& work, Complete&& complete)
    {
        using Value = std::invoke_result_t<Work>;
        std::function<void()> task = [generation,
                                      &token,
                                      work = std::forward<Work>(work),
                                      complete = std::forward<Complete>(complete)]() mutable {
            JobResult<Value> result;
            result.generation = generation;
            if (token.isCancelled())
            {
                result.status = JobResult<Value>::Status::cancelled;
                complete(std::move(result));
                return;
            }
            try
            {
                result.value = work();
                result.status = token.isCancelled() ? JobResult<Value>::Status::cancelled
                                                    : JobResult<Value>::Status::success;
            }
            catch (const std::exception& error)
            {
                result.status = JobResult<Value>::Status::failed;
                result.error = { "exception", error.what() };
            }
            catch (...)
            {
                result.status = JobResult<Value>::Status::failed;
                result.error = { "unknown_exception", "worker threw a non-standard exception" };
            }
            complete(std::move(result));
        };

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return false;
            queue_.push(std::move(task));
        }
        workAvailable_.notify_one();
        return true;
    }

    void waitIdle()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return queue_.empty() && activeWorkers_ == 0; });
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) return;
            stopping_ = true;
        }
        workAvailable_.notify_all();
        for (auto& worker : workers_)
            if (worker.joinable()) worker.join();
        workers_.clear();
    }

private:
    void workerLoop()
    {
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                workAvailable_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
                ++activeWorkers_;
            }
            task();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --activeWorkers_;
            }
            idle_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable idle_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    std::size_t activeWorkers_ = 0;
    bool stopping_ = false;
};

} // namespace ats

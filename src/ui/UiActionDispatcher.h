#pragma once

#include "EditorHost.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace ats::ui
{

class UiActionDispatcher
{
public:
    explicit UiActionDispatcher(std::size_t capacity = 256) : capacity_(capacity) {}

    bool submit(EditorAction action)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) return false;
        queue_.push_back(std::move(action));
        return true;
    }

    [[nodiscard]] std::optional<EditorAction> tryPop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        auto action = std::move(queue_.front());
        queue_.pop_front();
        return action;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<EditorAction> queue_;
};

class TelemetryPublisher
{
public:
    void publish(TelemetrySnapshot snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = std::move(snapshot);
    }

    [[nodiscard]] TelemetrySnapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    TelemetrySnapshot snapshot_;
};

} // namespace ats::ui
